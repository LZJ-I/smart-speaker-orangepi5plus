# smart-speaker-client

基于 OrangePi 5 Plus 的智能音箱客户端，集成语音识别(ASR)、关键词检测(KWS)、语音合成(TTS)与 AI 对话，支持规则匹配与音乐播放控制。

## 克隆与进入

从仓库根目录进入本目录：

```bash
git clone https://github.com/LZJ-I/smart-speaker-orangepi5plus
cd smart-speaker-orangepi5plus/smart-speaker-client
```

## 下载依赖

### 模型文件（体积较大，需自行下载）

#### 1. ASR 模型
```bash
cd 3rdparty/model/asr
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2
tar -jxf sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2
```

#### 2. VAD 模型
```bash
cd 3rdparty/model/asr
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx
```

#### 3. KWS 模型
```bash
cd 3rdparty/model/kws
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20.tar.bz2
tar -jxf sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20.tar.bz2
```

#### 4. TTS 模型
```bash
cd 3rdparty/model/tts
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/matcha-icefall-zh-en.tar.bz2
tar -jxf matcha-icefall-zh-en.tar.bz2
```

#### 5. Vocoder 模型
```bash
cd 3rdparty/model/tts
wget https://hk.gh-proxy.org/https://github.com/k2-fsa/sherpa-onnx/releases/download/vocoder-models/vocos-16khz-univ.onnx
```

### 框架文件（sherpa-onnx）

#### JNI 版本（推荐）
```bash
cd 3rdparty
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.12.25/sherpa-onnx-v1.12.25-linux-aarch64-jni.tar.bz2
tar -jxf sherpa-onnx-v1.12.25-linux-aarch64-jni.tar.bz2
```

#### 共享库版本（CPU）
```bash
cd 3rdparty
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.12.25/sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu.tar.bz2
tar -jxf sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu.tar.bz2
```

以上 `cd` 均从 `smart-speaker-client` 目录为起点执行。

## 构建与运行

- 构建：`make`
- 运行：`make run`
- 停止：`make stop`
- 测试工具：`build/bin/ipc_inject`、`build/bin/fifo_watch`
- 模式提示 WAV 生成：执行 `./tools/gen_mode_tts_wav.sh`（需先能编译 `voice-assistant/tts/example/tts_test`），生成 `assets/tts/mode_order.wav`、`assets/tts/mode_single.wav`

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

## 播放模式与语音恢复

- **播放模式**：仅顺序播放、单曲循环（已移除随机播放）。说「顺序播放」「单曲循环」时切换模式并播放预生成「已切换到xxx模式」提示音。
- **ASR 超时**：唤醒后未说话导致 ASR 超时时，自动恢复此前暂停的音乐。
- **控制类命令**：音量加减、模式切换等执行后，若唤醒前在播则恢复播放。
- **规则与 LLM**：ASR 文本由 `player/select.c` 规则匹配；未命中则 LLM 兜底，LLM 播报结束后按需恢复音乐。

## 参考技术文档

- [docs/smart-speaker-client-技术文档索引.md](docs/smart-speaker-client-技术文档索引.md)（入口）
- [docs/smart-speaker-client-播放模式与语音恢复策略更新说明.md](docs/smart-speaker-client-播放模式与语音恢复策略更新说明.md)
- [docs/smart-speaker-client-语音链路状态机与恢复播放方案.md](docs/smart-speaker-client-语音链路状态机与恢复播放方案.md)
- [docs/smart-speaker-client-TTS与音乐输出冲突方案.md](docs/smart-speaker-client-TTS与音乐输出冲突方案.md)
- [docs/smart-speaker-client-目录框架重构规范.md](docs/smart-speaker-client-目录框架重构规范.md)
