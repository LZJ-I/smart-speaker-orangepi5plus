# ASR 示例程序

本目录包含基于 Sherpa-onnx 的 ASR（自动语音识别）示例程序。

## 功能特性

- 从 ALSA 录音设备实时采集音频
- 支持 VAD（语音活动检测）
- 使用离线识别器进行语音转文字
- 支持音频重采样

## 编译

```bash
make
```

## 运行

```bash
./asr_test
```

## 程序说明

### 工作流程

1. 初始化 ALSA 录音设备
2. 初始化音频重采样器
3. 初始化 Sherpa-onnx ASR 模型（包括 VAD 和识别器）
4. 循环采集音频：
   - 从 ALSA 读取音频数据
   - 重采样到模型所需采样率
   - 送入 ASR 处理

### 配置

主要配置在 `../common/alsa.h` 中：
- `MODEL_SAMPLE_RATE`：模型采样率
- `CHANNELS`：声道数
- `PERIOD_SIZE`：ALSA 周期大小

模型配置详见 `sherpa_asr.c`。

## 查看可用 ALSA 设备

使用以下命令查看可用的录音设备：

```bash
arecord -l
```

## 信号处理

程序支持 `SIGINT`（Ctrl+C）信号，用于安全退出。
