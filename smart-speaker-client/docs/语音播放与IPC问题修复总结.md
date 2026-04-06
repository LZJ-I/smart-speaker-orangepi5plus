# 语音播放与 IPC 问题修复总结

> 部署与日常运行见 [README.md](../README.md)。

本文档汇总近期对话中解决的所有问题及对应方案，便于后续维护与排查。

---

## 一、TTS 与 ASR 进程间通信 (IPC)

### 1.1 TTS 收不到 ASR 的消息（type=2/type=0）

**现象**：ASR 日志显示「发送 TTS 命令成功」，TTS 端只收到 type=2/type=3，收不到后续 type=0（播放 LLM 回复）。

**原因**：两进程工作目录可能不同，`./fifo/tts_fifo` 指向不同路径，实际读写的是不同 FIFO。

**方案**：TTS 命令管道改为绝对路径，与协议头一致。
- `voice-assistant/common/ipc_protocol.h`：`TTS_FIFO_PATH` 改为 `"/tmp/tts_fifo"`。
- TTS、ASR 的 `open_pipes()` 中对该路径做 `mkfifo`（若不存在），保证共用同一 FIFO。

### 1.2 唤醒词播完后 ASR 才进入识别（避免固定延时）

**现象**：用固定 2200ms 等唤醒词播完再开 ASR，要么等太久要么仍采到喇叭声。

**方案**：用 FIFO 做「唤醒播完」同步，去掉固定延时。
- 新增 `TTS_WAKE_DONE_FIFO_PATH`（如 `/tmp/tts_wake_done_fifo`），在协议与两进程 `open_pipes` 中创建。
- TTS：处理 type=3 时，`play_wake_response()` 后 `pthread_join(playback_thread)`，播完后 `open(O_WRONLY|O_NONBLOCK)` 写一字节再 `close`。
- ASR：发 type=2、type=3 后，`open(TTS_WAKE_DONE_FIFO_PATH, O_RDONLY)` 阻塞读一字节，再切到 `STATE_ASR`。
- 唤醒 FIFO 使用绝对路径，避免 cwd 不同导致阻塞或连错管道。

### 1.3 读取 IPC 消息失败 (Invalid argument) / 协议错位

**现象**：TTS 报「读取 IPC 消息失败: Invalid argument」，或只处理到 type=2 就断，后续 type=0 被当非法头。

**原因**：读 body 时若对端关闭或读到的字节不足，原先对 EOF/短读处理不当，只消费了 header 未消费 body，下次把 body 当前一个消息的 header 解析，触发 EINVAL。

**方案**：
- `ipc/ipc_message.c`：`ipc_read_all` 在已读 `total>0` 时若 `read()==0`，设 `errno=EPIPE` 并返回 -1；body 读若返回 1（EOF），先按 `header->body_len` 从 fd 中读掉剩余字节再返回 -1，保证流对齐。
- 避免「只读了 header 就返回」导致后续解析错位。

---

## 二、TTS 播放逻辑

### 2.1 整段播放、不按句切分

**需求**：参考 c-api-examples，整段生成、整段播放，不要按句切分再逐句播。

**方案**：
- `main_tts` 中 type=0 处理：对整段 body 只调用一次 `generate_tts_audio_full(text, ...)`，再起一次 `playback_text_thread` 播放。
- `init_sherpa_tts_with_chunk_size(-1)`，与 offline-tts-c-api 的「-1 表示一次处理所有句子」一致。

### 2.2 支持打断，且不阻塞主循环收命令

**现象**：播放 LLM 回复时无法打断；或主循环在 `pthread_join` 阻塞，收不到 type=2/type=3。

**方案**：
- type=0 处理中：只 `pthread_create(playback_text_thread)`，不再 `pthread_join`，主循环继续 `poll` 读 IPC。
- `playback_text_thread` 结束时用 mutex 将全局 `playback_thread` 置 0。
- type=2 时主线程设 `playback_should_stop=1` 并 `snd_pcm_drop`；播放线程检测到 stop 即退出并在退出前 `snd_pcm_drop`（见下文尾音清理）。

### 2.3 KWS 管道无读者时的告警

**现象**：每次检测到关键词都打「打开管道失败 ./fifo/kws_fifo: No such device or address」。

**方案**：`write_text_pipe` 中，若打开失败且 `errno==ENXIO`（无读者），不打印 LOGW，静默返回。

---

## 三、尾音与 ALSA 缓冲

### 3.1 上一句尾音带到下一句 / 打断后仍有尾音（约半秒）

**现象**：切换或打断后，能听到上一句末尾约半秒（或 1 个 period）被重复播放。

**原因**：尾巴长度≈一个 period 时长；欠载(XRUN)/环形缓冲残留导致最后一个 period 被再次播放。且原 `PERIOD_SIZE=8192`，16kHz 下约 0.5s。

**方案**：
- **缩小 period**：`voice-assistant/tts/alsa_output.h` 中 `PERIOD_SIZE` 改为 256（约 16ms），单 period 残留可接受。
- **写前再查 stop**：在 `playback_text_thread` 与 `playback_wav_thread` 中，每次 `snd_pcm_writei` 前再检查一次 `playback_should_stop`，若已置位则 `break` 不写本块，减少「stop 后仍写一块」的竞态。
- **打断时由播放线程自己 drop**：两播放线程在因 `playback_should_stop` 退出时，在 return 前调用 `snd_pcm_drop(g_pcm_handle)`，再在 type=2/type=3 等切段处主线程也 drop；必要时在 type=3 的 join 后再 drop 一次再播唤醒。

### 3.2 用静音覆盖环形缓冲 + 统一 write 与 XRUN 处理

**需求**：彻底清掉残留，且写 PCM 时统一处理 -EPIPE(XRUN) 和短写。

**方案**：
- **pcm_write_all**（`voice-assistant/tts/alsa_output.c`）：封装 `snd_pcm_writei`，循环写满请求帧数；遇 -EPIPE 则 `snd_pcm_prepare` 后继续；其它负值用 `snd_pcm_recover(..., 1)` 后继续；按 `n*CHANNELS` 前进指针。
- **pcm_write_silence**：在每次 **drop+prepare** 之后写静音覆盖环形缓冲；静音缓冲**复用**（static 分配，按需扩大），覆盖帧数用 **snd_pcm_get_params** 取 **period_size**，只写 **2*period_size** 帧（不写满 buffer_size），避免预灌过多静音导致「要等很久才出声」。
- 所有原先直接 `snd_pcm_writei` 的地方改为 `pcm_write_all`；切段/打断处 drop+prepare 后调用 `pcm_write_silence(g_pcm_handle)`。

### 3.3 播放前感觉要等很久（预灌静音过多）

**现象**：清尾音时写了整缓冲静音，新一句要等这段静音播完才出声。

**方案**：`pcm_write_silence` 不再按 `buffer_size` 写满，改为只写 **2*period_size**（且不超过 buffer_size），用 `snd_pcm_get_params` 取 period_size；静音缓冲按该长度复用。既清 1～2 个 period 残留，又避免长时间静音延迟。

---

## 四、相关文件与常量

| 项目           | 位置 / 值 |
|----------------|-----------|
| TTS 命令管道   | `TTS_FIFO_PATH` → `/tmp/tts_fifo` |
| 唤醒完成管道   | `TTS_WAKE_DONE_FIFO_PATH` → `/tmp/tts_wake_done_fifo` |
| 播放 period    | `voice-assistant/tts/alsa_output.h` → `PERIOD_SIZE` 256 |
| PCM 写封装     | `voice-assistant/tts/alsa_output.c` → `pcm_write_all`、`pcm_write_silence` |
| IPC 读 body 对齐 | `ipc/ipc_message.c` → body 读失败时按 body_len 丢弃剩余字节 |
| 板载 3.5mm 设备 | 见 `docs/orangepi-3.5mm-音频驱动说明.md`（hw:3,0、立体声等） |

---

## 五、可选后续优化（未在本次实现）

- **播放队列/环形缓冲**：段与段之间续写，无数据时补静音防断流，仅在**强制打断**时做 drop+prepare+静音，减少频繁 drop/prepare。
- **静音覆盖帧数**：当前已用 `snd_pcm_get_params` 的 period_size，只写 2*period；若需可再按实际 buffer_size 做一次精确覆盖（注意不要写满整缓冲导致延迟）。
