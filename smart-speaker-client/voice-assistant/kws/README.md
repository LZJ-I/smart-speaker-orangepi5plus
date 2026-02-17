# KWS 示例程序

本目录包含基于 Sherpa-onnx 的 KWS（关键词检测）示例程序。

## 功能特性

- 从 ALSA 录音设备实时采集音频
- 实时检测预设关键词
- 低延迟关键词唤醒
- 支持多种在线模式切换

## 编译

```bash
make
```

## 运行

```bash
./kws_test
```

## 程序说明

### 工作流程

1. 初始化 ALSA 录音设备
2. 初始化 Sherpa-onnx KWS 模型
3. 循环采集音频：
   - 从 ALSA 读取音频数据
   - 转换为浮点数格式
   - 送入 KWS 模型检测
   - 检测到关键词时输出日志

### 在线模式

程序支持多种在线模式（`g_current_online_mode`）：
- `ONLINE_MODE_YES`：当前模式

### 配置

主要配置在 `../common/alsa.h` 中：
- `MODEL_SAMPLE_RATE`：模型采样率
- `CHANNELS`：声道数
- `PERIOD_SIZE`：ALSA 周期大小

模型配置和关键词列表详见 `sherpa_kws.c`。

## 查看可用 ALSA 设备

使用以下命令查看可用的录音设备：

```bash
arecord -l
```

## 信号处理

程序支持 `SIGINT`（Ctrl+C）信号，用于安全退出。
