# smart-speaker-client 本次改动文件清单

## 当前模块划分（分文件拆分后）

与下文「近期改动」「代码改动」对应的实现，部分已拆到独立文件，路径对应关系如下：

- **player**：`player_constants.h`、`player_types.h`、`player.h`、`player.c`、`player_fifo.c`（init_*_fifo）、`player_gst.c`、`select.c`、`select_text.c`、`select_text.h`、`select_music_llm.c`、`select_music_llm.h`、`socket.c`、`socket_report.c`、`socket_report.h`、`link.c`、`device.c`、`shm.c`、`rule_match.c`、`music_lib_bridge.c`、`main.c`。
- **voice-assistant/main_tts**：`main.c`（薄壳）、`tts_playback.c/h`、`tts_ipc_handler.c/h`；TTS 播放与 IPC 处理逻辑在上述子模块。
- **voice-assistant/main_asr_kws**：`main.c`、`asr_kws_constants.h`、`asr_kws_types.h`、`asr_kws_pipe.c/h`；管道与状态枚举在 pipe/types/constants。
- **ipc**：`ipc_message.c/h`（未变）。

---

## 近期改动（TTS/音乐冲突、音量、继续播放、下一首）

- **player/player.c**  
  - 停止状态下“继续播放”(4) 视为开始播放：`player_continue_play()` 在 `g_current_state == PLAY_STATE_STOP` 时调用 `player_start_play()` 并 return。  
  - 下一首/上一首：切歌时不再强制 `g_current_suspend = PLAY_SUSPEND_NO`，保留当前暂停态；否则在 TTS 暂停时切歌会导致 tts:done 后 `player_continue_play()` 误判未暂停而不发 `cycle pause`，新曲目一直不播。  
  - **下一首/上一首与 shm 一致**：`player_start_play()` 不再固定用链表第一首；先 `shm_get(&s)`，若 `s.current_music[0] != '\0'` 则用 `s.current_singer`/`s.current_music` 拼出曲目名再 `player_play_music()`，否则用 `g_music_head->next->music_name`。这样在 STOP 下先按下一首/上一首再开始播放时，会播 shm 里已更新的曲目。  
  - **下一首/上一首在线切歌**：在线模式下优先用 `music_lib_get_url_for_music(next_music/prev_music, music_path, size)` 取完整 URL，失败再回退 `ONNINE_URL+singer+music`；`music_path` 改为 2048、写入 FIFO 的 `loadfile` 缓冲改为 4096，避免长 URL 被截断导致切歌无效。

- **player/player_gst.c**  
  - FIFO 读命令改为按行缓冲：`GstPlayerData` 增加 `line_buf[FIFO_LINE_MAX]`、`line_len`，读到 `\n` 再整行交给 `process_line`，避免长 `loadfile '...url...'` 被一次 read 截断导致切歌无效。`FIFO_LINE_MAX` 设为 4096 以容纳长 URL 整行。

- **player/link.c**  
  - 歌曲名入库与输出前做尾部 `\r\n`/空格 trim（`trim_trailing_crlf`）；`link_add_music_lib`、`link_add` 写入 `music_name` 后 trim，`link_get_next_music`/`link_get_prev_music` 写入 next_music/prev_music 后 trim 再 LOG，避免 API 或本地名带换行导致“下一首歌是”等日志被拆成两行。输出用 `strncpy(..., cap-1)` 并补 `\0` 保证不越界。

- **player/device.h**  
  - 音量 CARD 改为 `"hw:3"`（3.5mm 为 card 3）；ELEM 仍为 `"lineout volume"`，控件缺失时由 device.c 按优先级选第一个播放音量控件。

- **player/device.c**  
  - 控件选择：ELEM 不存在时按优先级（PCM / Playback / Master / Speaker / Line / Headphone）选第一个有播放音量的控件。  
  - `device_get_volume`：从 ALSA 读当前音量并同步到 `g_current_vol`。  
  - 0–100 与硬件映射：用幂曲线 `VOLUME_EXP=2`（设：硬件比例=(音量/100)^2；读：音量=100*硬件比例^0.5），使听感更接近线性。

- **voice-assistant/main_tts**（逻辑现分布在 `tts_playback.c`、`tts_ipc_handler.c`、`main.c`）  
  - 唤醒回复 WAV 播完不向 player 发 `tts:done`（`s_wav_play_is_wake` + `playback_wav_thread` 结尾判断），避免唤醒后多一次“恢复再暂停”。  
  - 文本 TTS 与 WAV 在设备采样率与源不一致时做线性插值重采样。  
  - 内容会话标志 `s_tts_content_session`：PLAY_TEXT 进入即置 1，文本线程结束前清 0；`IPC_CMD_PLAY_AUDIO_FILE` 在会话内直接跳过，避免双重 tts:start/tts:done 与提前恢复音乐。

- **voice-assistant/tts/alsa_output.c / alsa_output.h**  
  - 初始化时保存实际采样率到 `g_alsa_playback_rate`，供 TTS/WAV 重采样使用。

- **docs/smart-speaker-client-TTS与音乐输出冲突方案.md**  
  - 已实现：tts:start/tts:done、唤醒不发 tts:done、重采样、避免双重事件的“内容会话”与 PLAY_AUDIO_FILE 跳过说明。

- **docs/orangepi-3.5mm-音频驱动说明.md**  
  - “音量控制”小节：CARD=`hw:3`，ELEM 及 `amixer -c 3 scontrols` 说明。

---

## 代码改动

- `smart-speaker-client/player/select.c`
  - 调整 ASR 处理流程为“规则优先，未命中走 LLM”。
  - 新增恢复播放判定：LLM 执行后恢复；规则按规则行为处理。
  - KWS 在线处理改为“记录恢复态+暂停播放”。

- `smart-speaker-client/voice-assistant/main_asr_kws/main.c`
  - 启用 `asr_fifo` 与 `kws_fifo` 写入。
  - 新增 `write_text_pipe()` 管道写入辅助函数（非阻塞打开 + 断链重连）。
  - ASR 最终文本改为写入 `asr_fifo`，由 player 统一做规则/LLM逻辑。

- `smart-speaker-client/voice-assistant/main_tts/main.c`
  - TTS FIFO 收包改为固定头+变长body协议读取。
  - 播放命令解析改为 `type + payload`。

- `smart-speaker-client/ipc/ipc_message.h`
- `smart-speaker-client/ipc/ipc_message.c`
  - 新增统一 IPC 报文协议实现。

- `smart-speaker-client/player/player.c`
  - 在线/离线模式切换改为通过 `asr_ctrl_fifo` 通知 ASR。
  - 新增 SD 卡 `mmcblk*p*` 自动探测并挂载。

- `smart-speaker-client/player/main.c`
- `smart-speaker-client/player/init.sh`
  - 统一启动路径与 FIFO 初始化（新增 `asr_ctrl_fifo`）。

- `smart-speaker-client/Makefile`
- `smart-speaker-client/supervisor/main.c`
- `smart-speaker-client/supervisor/Makefile`
- `smart-speaker-client/tools/ipc_inject.c`
- `smart-speaker-client/tools/fifo_watch.c`
- `smart-speaker-client/tools/Makefile`
  - 完成主子构建体系、supervisor、测试工具落地。

## 新增技术文档

- `docs/smart-speaker-client-语音链路状态机与恢复播放方案.md`
- `docs/smart-speaker-client-目录框架重构规范.md`
- `docs/smart-speaker-client-本次改动文件清单.md`
- `docs/smart-speaker-client-全量重构实施记录.md`
- `docs/smart-speaker-client-技术文档索引.md`

## 后续计划中待落地

- 规则链配置化（规则表 + 外置词典）。
- 彻底下线业务信号路径，保留 SIGCHLD。
- 统一音频设备配置抽象与自检脚本。

