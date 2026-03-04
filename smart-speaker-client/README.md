# smart-speaker-client

## 当前运行模块

- `asr_kws_process`：关键词唤醒与语音识别，输出到 FIFO；ASR 超时无结果时向 asr_fifo 写哨兵，供 player 恢复音乐。
- `tts_process`：语音播报；短 WAV（唤醒、模式切换、兜底句）统一走内部 playback_wav_thread 管线。
- `player/run`：音乐播放控制与语义规则处理；监听 asr_fifo、player_ctrl_fifo，维护恢复播放状态。
- `build/bin/supervisor`：统一拉起与回收子进程。

## 当前核心 FIFO

- `fifo/kws_fifo`：KWS 唤醒事件
- `fifo/asr_fifo`：ASR 文本或超时哨兵 `__asr_timeout__`
- `fifo/player_ctrl_fifo`：TTS 事件 `tts:start` / `tts:done`
- `fifo/asr_ctrl_fifo`：ASR 在线/离线控制
- TTS 命令走 IPC（`/tmp/tts_fifo` 等），见 `voice-assistant/common/ipc_protocol.h`

## 构建与运行

- 构建：`make`
- 运行：`make run`
- 停止：`make stop`
- 测试工具：`build/bin/ipc_inject`、`build/bin/fifo_watch`
- 模式提示 WAV 生成：在项目根目录执行 `./tools/gen_mode_tts_wav.sh`（需先能编译 `voice-assistant/tts/example/tts_test`），生成 `assets/tts/mode_order.wav`、`assets/tts/mode_single.wav`

## 播放模式与语音恢复

- **播放模式**：仅顺序播放、单曲循环（已移除随机播放）。说「顺序播放」「单曲循环」时切换模式并播放预生成「已切换到xxx模式」提示音。
- **ASR 超时**：唤醒后未说话导致 ASR 超时时，自动恢复此前暂停的音乐。
- **控制类命令**：音量加减、模式切换等执行后，若唤醒前在播则恢复播放。
- **规则与 LLM**：ASR 文本由 `player/select.c` 规则匹配；未命中则 LLM 兜底，LLM 播报结束后按需恢复音乐。

## 参考技术文档

- `docs/smart-speaker-client-技术文档索引.md`（入口）
- `docs/smart-speaker-client-播放模式与语音恢复策略更新说明.md`（模式精简、ASR 超时恢复、TTS 播放路径统一）
- `docs/smart-speaker-client-语音链路状态机与恢复播放方案.md`
- `docs/smart-speaker-client-TTS与音乐输出冲突方案.md`
- `docs/smart-speaker-client-目录框架重构规范.md`

