# TTS 与音乐输出冲突方案

> 部署与日常运行见 [README.md](../README.md)。

## 问题

TTS（语音播报）与 音乐播放 共用 3.5mm 声卡（rockchipes8388）。若一方使用 `hw:3,0` 直连，会独占设备，另一方打开时报 “Device or resource busy”。

## 方案：统一走 dmix

- **TTS**：`voice-assistant/main_tts/main.c` 中播放设备设为 `dmix:CARD=rockchipes8388,DEV=0`。
- **音乐**：`player/core/player.h` 中 `GST_ALSA_DEVICE` 设为 `dmix:CARD=rockchipes8388,DEV=0`。

两路都经 dmix 后，ALSA 负责混音，可同时打开、同时出声。

## 已实现：TTS 时暂停音乐

- **TTS 进程**：在开始播放前写 `./fifo/player_ctrl_fifo` 发送 `tts:start`，播放结束后发送 `tts:done`（见 `notify_player_tts_event`）。
- **唤醒回复**：播放“我在”等唤醒 WAV 结束时**不**发送 `tts:done`，避免音乐多一次“恢复再暂停”；仅在实际内容 TTS（LLM 回复等）结束时发送 `tts:done`，再恢复音乐。
- **Player**：`select_read_player_ctrl()` 收到 `tts:start` 时若正在播放则 `player_suspend_play()` 并置 `g_resume_after_tts`；收到 `tts:done` 时若 `g_resume_after_tts` 则 `player_continue_play()`。

## 已实现：TTS 与设备采样率不一致时的重采样

当 ALSA 实际采样率（如 48kHz）与 TTS/WAV 源（如 16kHz）不一致时，直接写入会导致播放变快/变慢。

- **`voice-assistant/tts/alsa_output.c`**：初始化时保存实际采样率到 `g_alsa_playback_rate`。
- **文本 TTS 播放**（`playback_text_thread`）：若 `g_alsa_playback_rate != g_tts_sample_rate`，对每块数据做线性插值重采样后再写 PCM。
- **WAV 播放**（`playback_wav_thread`）：若 WAV 的 `header.sample_rate != g_alsa_playback_rate`，对每块读入数据重采样后再写。

## 已实现：避免双重 tts:start/tts:done

同一次对话中 player 可能先发 `PLAY_TEXT`（asr_kws 的 LLM 结果）再发 `PLAY_AUDIO_FILE`（如 fallback_unmatched.wav）。若两者都发 tts:start/tts:done，会多一次“恢复再暂停”且可能提前恢复音乐。

- **内容会话标志** `s_tts_content_session`：进入 `IPC_CMD_PLAY_TEXT` 时立即置 1（在 `generate_tts_audio_full` 之前），`playback_text_thread` 结束（正常或被打断）发 tts:done 前清 0。
- **`IPC_CMD_PLAY_AUDIO_FILE`**：若 `s_tts_content_session == 1` 则直接跳过（不播、不发事件），避免与文本播放的 start/done 成对重复。
- **STOP 打断**：逻辑未改，仍只设 `playback_should_stop`，打断及时性不变。

## 已实现：短 WAV 与唤醒词同一播放路径

- **`IPC_CMD_PLAY_AUDIO_FILE`** 不再使用 `system("aplay ...")`，改为内部 `tts_playback_play_wav_file(path)` + `playback_wav_thread`，与唤醒词使用同一 ALSA PCM 管线。
- 模式切换提示（mode_order.wav / mode_single.wav）、fallback_unmatched.wav 等均走该路径。详见 `smart-speaker-client-播放模式与语音恢复策略更新说明.md`。
