# smart-speaker-client 语音链路状态机与恢复播放方案

## 目标

- 唤醒时若音乐正在播放，先打断再进入语音交互。
- 若 ASR 走 LLM 闲聊，LLM 语音播报后恢复原音乐播放。
- 若 ASR 命中规则，按规则执行，不做额外强制恢复。

## 进程职责

- `asr_kws_process`
  - KWS 命中后发送唤醒事件到 `kws_fifo`。
  - ASR 最终文本发送到 `asr_fifo`。
  - 仅负责音频前端与识别，不再直接做规则判定。
- `player/run`
  - 监听 `kws_fifo`、`asr_fifo`。
  - 在 `select.c` 内做规则匹配和 LLM 兜底。
  - 维护 `g_need_continue`，控制“是否需要恢复播放”。
- `tts_process`
  - 接收 TTS 命令并播放。

## 状态机

### 播放状态（player）

- `PLAY_STATE_STOP`
- `PLAY_STATE_PLAY + PLAY_SUSPEND_NO`
- `PLAY_STATE_PLAY + PLAY_SUSPEND_YES`

### 语音交互状态（逻辑）

1. `KWS_LISTEN`
2. `WAKE_HIT`
3. `ASR_LISTEN`
4. `INTENT_MATCH`
5. `RULE_EXEC` 或 `LLM_FALLBACK`
6. `RESUME_IF_NEEDED`

## 恢复播放策略

### KWS 阶段

- 若唤醒前是“正在播放且未暂停”，记录 `g_need_continue = 1`。
- 否则 `g_need_continue = 0`。
- 然后执行 `player_suspend_play()`，避免录音被音乐污染。

### ASR 阶段

- 规则命中：
  - 停止/暂停/继续/切歌/点歌/模式切换等按规则本身执行。
  - 音量、播放模式这类“非切换播放流”的命令，若唤醒前在播，执行后恢复播放。
- LLM 兜底：
  - 执行 `../qwen/run "<asr文本>"`。
  - 命令返回后若 `g_need_continue == 1`，恢复播放。

## 当前规则范围（对齐 player 现有能力）

- 播放控制：开始、停止、暂停、继续、上一首、下一首。
- 音量控制：增大/减小音量（含同义词）。
- 播放模式：单曲循环、顺序播放、随机播放。
- 指定歌手：周杰伦、刘宇宁、薛之谦、张杰、其他/流行。
- 系统模式：离线模式切换。
- 其他：换音色（保持原入口）。

## 已落地代码点

- `smart-speaker-client/player/select.c`
  - 重构 `select_read_asr()` 的规则与 LLM 分流逻辑。
  - 实现“LLM 播报后恢复播放”策略。
  - KWS 在线处理改为仅做“记录恢复态 + 暂停播放”。
- `smart-speaker-client/voice-assistant/main_asr_kws/main.c`
  - 新增向 `asr_fifo`、`kws_fifo` 的文本写入能力。
  - ASR 最终文本由 `player` 统一做规则/LLM处理。

