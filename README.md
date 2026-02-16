# 智能音箱项目 (Smart Speaker)

基于 OrangePi 5 Plus 的智能音箱项目，集成了语音识别(ASR)、关键词检测(KWS)、语音合成(TTS)和 AI 对话功能。

## 项目结构

```
smart-speaker-orangepi5plus/
├── smart-speaker-client/
│   ├── voice-assistant/     # 负责 ASR 和 KWS
│   ├── player/              # 播放器和主控制程序(算是主程序)
│   ├── qwen/                # 千问 AI 对话接口
│   ├── sherpa-tts/          # 语音合成 (TTS)
│   └── 3rdparty/            # 第三方库和模型
│       ├── git-sherpa-onnx/ # Git 子模块：sherpa-onnx
│       ├── Matcha-TTS/      # Git 子模块：Matcha-TTS
│       ├── sherpa-onnx/     # sherpa-onnx 编译文件
│       └── model/           # 模型文件目录
└── smart-speaker-server/    # 服务器端 (可选)
```

## 下载依赖

### 模型文件（模型比较大，无法上传到 Git）

#### 1. ASR 模型
```bash
cd smart-speaker-client/3rdparty/model/asr
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2
tar -jxf sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2
```

#### 2. VAD 模型
```bash
cd smart-speaker-client/3rdparty/model/asr
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx
```

#### 3. KWS 模型
```bash
cd smart-speaker-client/3rdparty/model/kws
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20.tar.bz2
tar -jxf sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20.tar.bz2
```

### 框架文件

#### 1. JNI 版本
```bash
cd smart-speaker-client/3rdparty
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.12.25/sherpa-onnx-v1.12.25-linux-aarch64-jni.tar.bz2
tar -jxf sherpa-onnx-v1.12.25-linux-aarch64-jni.tar.bz2
```

#### 2. 共享库版本（CPU）
```bash
cd smart-speaker-client/3rdparty
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.12.25/sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu.tar.bz2
tar -jxf sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu.tar.bz2
```
