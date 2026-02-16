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

### 1. 下载 ASR 模型 (重要！)

ASR 模型文件很大，没有包含在 Git 仓库中，需要手动下载：

```bash
cd smart-speaker-client/3rdparty/model/asr

# 使用代理下载
wget "https://hk.gh-proxy.org/https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2"

# 或者直接下载
# wget "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2"

# 解压
tar -jxf sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2
```
