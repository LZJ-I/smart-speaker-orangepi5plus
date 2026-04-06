# 播放模式与语音恢复策略更新说明

> 部署与日常运行见 [README.md](../README.md)。

本文档汇总“播放模式精简”“ASR 超时恢复音乐”“模式切换 TTS 与短 WAV 播放路径统一”等改动的设计、实现与代码位置，供后续维护与论文/报告引用。

---

## 1. 播放模式精简（删除随机播放）

### 1.1 目标

- 仅保留**顺序播放**、**单曲循环**两种模式，移除随机播放以简化逻辑与用户体验。

### 1.2 涉及文件与修改

| 文件 | 修改内容 |
|------|----------|
| `player/core/player_types.h` | 枚举删除 `RANDOM_PLAY`，保留 `ORDER_PLAY`、`SINGLE_PLAY` |
| `player/rules/rule_match.h` | 删除 `RULE_CMD_MODE_RANDOM` |
| `player/rules/rule_match.c` | 删除 `match_mode_random`、规则表中随机项、控制词中「随机播放」、`rule_cmd_to_string` 对应 case |
| `player/net/link.c` | `link_get_next_music()` 中删除 `RANDOM_PLAY` 分支及原 `list_count()` 等未用逻辑 |
| `player/core/player.c` | `player_set_mode()` 仅接受 `SINGLE_PLAY`/`ORDER_PLAY` |
| `player/select_loop/select.c` | 删除 `RULE_CMD_MODE_RANDOM` 分支；`Parse_server_cmd` 中删除 `app_random_mode` 处理 |
| `player/select_loop/select_text.c` | 控制意图关键词中删除「随机播放」 |
| `player/net/socket.c` | 删除 `socket_set_random_mode()` |
| `player/net/socket.h` | 删除 `socket_set_random_mode` 声明 |
| `player/core/shm.h` | `current_mode` 注释改为「0-顺序播放 1-单曲循环」 |

### 1.3 当前播放模式语义

- **0 - 顺序播放（ORDER_PLAY）**：当前曲目结束后播下一首，到列表末尾停止（或按现有列表逻辑）。
- **1 - 单曲循环（SINGLE_PLAY）**：当前曲目结束后重复播放当前曲。

---

## 2. 模式切换 TTS 反馈（顺序/单曲）

### 2.1 设计

- 用户通过 ASR 说「顺序播放」「单曲循环」时，除切换模式外，播放预生成的短句 WAV，与唤醒词反馈形式一致（短提示、同一播放管线）。

### 2.2 资源与路径

- **顺序播放**：`./assets/tts/mode_order.wav`，内容为「已切换到顺序播放模式」。
- **单曲循环**：`./assets/tts/mode_single.wav`，内容为「已切换到单曲循环模式」。

### 2.3 生成方式

- **脚本**：`tools/gen_mode_tts_wav.sh`
  - 在项目根目录执行；会尝试编译 `voice-assistant/tts/example/tts_test`，再生成上述两个 WAV 到 `assets/tts/`。
- **手动**：使用 `voice-assistant/tts/example/tts_test`，例如：
  - `-t "已切换到顺序播放模式" -o ./assets/tts/mode_order.wav`
  - `-t "已切换到单曲循环模式" -o ./assets/tts/mode_single.wav`

### 2.4 代码逻辑

- `player/select_loop/select.c` 中定义 `MODE_ORDER_WAV_PATH`、`MODE_SINGLE_WAV_PATH`。
- 规则命中 `RULE_CMD_MODE_ORDER` / `RULE_CMD_MODE_SINGLE` 时：先 `player_set_mode(...)`，再 `tts_play_audio_file(MODE_ORDER_WAV_PATH)` 或 `tts_play_audio_file(MODE_SINGLE_WAV_PATH)`。
- 上述调用经 IPC 发 `IPC_CMD_PLAY_AUDIO_FILE` 给 TTS 进程，由**统一短 WAV 播放路径**（见第 4 节）播放，与唤醒词同一套 PCM/线程管线。

---

## 3. ASR 超时恢复音乐与 g_need_continue

### 3.1 问题

- KWS 唤醒后进入 ASR，若用户未说话导致 **ASR 超时（如 5 秒）**，asr_kws 仅切回 KWS 监听，不向 player 通知；音乐在唤醒时已被暂停，若不通知则一直不恢复。

### 3.2 方案概览

- **asr_kws**：ASR 超时且无识别结果时，向 asr_fifo 写入固定哨兵字符串。
- **player**：从 asr_fifo 读到哨兵时，若此前因 TTS 暂停过（`g_resume_after_tts`），则恢复播放并清除标志。
- **g_need_continue**：在收到 `tts:start`（唤醒导致 TTS 开始）时，根据“当前是否正在播放且未暂停”置 1 或 0，供后续音量/模式等控制命令执行后决定是否恢复播放。

### 3.3 协议与常量

- **ipc_protocol.h**：`ASR_TIMEOUT_SENTINEL "__asr_timeout__"`，asr_kws 与 player 共用。

### 3.4 asr_kws 侧

- **voice-assistant/main_asr_kws/main.c** 的 `check_asr_timeout()`：
  - 超时且 `strlen(g_last_asr_text) == 0` 时，向 asr_fifo 写入一行 `ASR_TIMEOUT_SENTINEL\n`（使用现有 `asr_fd`）。
  - 有识别结果时逻辑不变，仍写识别文本。

### 3.5 player 侧

- **player/select_loop/select.c**  
  - **select_read_asr()**：读 asr_fifo 后去掉行尾换行；若内容等于 `ASR_TIMEOUT_SENTINEL`，则若 `g_resume_after_tts` 则 `player_continue_play()`，清 `g_resume_after_tts`，return（不进行规则匹配与 LLM）。  
  - **select_read_player_ctrl()**：收到 `tts:start` 时，若当前为「播放且未暂停」则置 `g_need_continue = 1` 并暂停、置 `g_resume_after_tts = 1`；否则置 `g_need_continue = 0`。

### 3.6 行为小结

- **仅唤醒、未说话超时**：asr_kws 写哨兵 → player 识别哨兵 → 恢复音乐（若曾因 TTS 暂停）。
- **唤醒后说了音量/模式等**：`had_music_before_wakeup = g_need_continue` 已在 tts:start 时正确设置，规则执行后若 `resume_after_handle` 则恢复播放。

---

## 4. 短 WAV 播放路径统一（与唤醒词一致）

### 4.1 问题

- 原先 `IPC_CMD_PLAY_AUDIO_FILE` 在 TTS 侧用 `system("aplay ...")` 播放，与唤醒词使用的内部 `playback_wav_thread` + 同一 PCM 管线不一致，设备与时序行为不统一。

### 4.2 方案

- 所有“短 WAV 文件”播放（模式切换提示、fallback_unmatched 等）统一走与唤醒词相同的内部播放路径：`playback_wav_thread` + 同一 ALSA PCM。

### 4.3 实现

- **voice-assistant/main_tts/tts_playback.c**
  - 新增 `tts_playback_play_wav_file(const char *path)`：与 `tts_playback_wake_response()` 相同的设备与线程逻辑（`snd_pcm_drop` / prepare / 静音、设 `playback_wav_filename`、启动 `playback_wav_thread`），仅路径由参数指定。
- **voice-assistant/main_tts/tts_playback.h**
  - 声明 `tts_playback_play_wav_file`。
- **voice-assistant/main_tts/tts_ipc_handler.c**
  - `IPC_CMD_PLAY_AUDIO_FILE` 处理：不再调用 `system("aplay ...")`，改为 `tts_playback_notify_player("tts:start")` → `tts_playback_play_wav_file(path)` → `tts_playback_join()` → `tts_playback_notify_player("tts:done")`。

### 4.4 效果

- 模式切换提示（mode_order.wav / mode_single.wav）、fallback_unmatched.wav 等均与唤醒词走同一套 WAV 播放管线，日志中可见“播放WAV文件(同唤醒路径): …”。

---

## 5. 论文/报告可引用要点

- **播放模式**：仅保留顺序播放与单曲循环，移除随机播放，简化状态与切歌逻辑，便于形式化与实现一致性。
- **语音恢复策略**：  
  - 唤醒时若正在播放则暂停并记录“需恢复”；  
  - ASR 有结果时按规则或 LLM 执行，控制类命令（音量、模式）执行后恢复播放；  
  - ASR 超时无结果时通过 asr_fifo 哨兵通知 player 恢复播放，避免“只唤醒不说话”导致音乐长期暂停。
- **协议设计**：asr_fifo 复用，用固定哨兵 `__asr_timeout__` 表示“超时无结果”，player 与 asr_kws 无需额外管道。
- **TTS 与音乐协同**：TTS 开始/结束通过 player_ctrl_fifo 通知 player 暂停/恢复；短提示 WAV（唤醒、模式切换、兜底句）统一走同一 ALSA 播放管线，避免子进程与设备抢占。

---

## 6. 相关文档

- `smart-speaker-client-语音链路状态机与恢复播放方案.md`：整体状态机与恢复策略（已按本文更新）。
- `smart-speaker-client-TTS与音乐输出冲突方案.md`：TTS 与音乐共用 dmix、tts:start/tts:done 语义。
- `smart-speaker-client-player-规则匹配示例清单.md`：规则与示例（含模式类）。
