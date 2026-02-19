# TTS 示例程序

本目录包含基于 Sherpa-onnx 的 TTS（文本到语音）和 WAV 播放示例程序。

## 功能特性

- 支持两种输出模式的 TTS 合成
- 支持 ALSA 音频设备输出
- 支持 WAV 文件输出
- 命令行参数配置
- WAV 文件播放功能

## 编译

```bash
make
```

编译后会生成两个可执行文件：
- `tts_test`: TTS 文本转语音测试程序
- `play_wav`: WAV 文件播放程序

## tts_test 使用说明

### 输出模式

程序支持以下两种输出模式：

| 模式编号 | 模式名称 | 说明 |
|---------|---------|------|
| 1 | 单次输出到文件 | 等所有音频生成完成后，一次性写入 WAV 文件 |
| 2 | 单次输出到 ALSA | 等所有音频生成完成后，一次性播放 |

### 命令行参数

| 参数 | 简写 | 默认值 | 说明 |
|-----|------|--------|------|
| `--mode` | `-m` | 1 | 输出模式 (1-2) |
| `--text` | `-t` | 预设文本 | 要合成的文本 |
| `--output` | `-o` | generated.wav | 输出 WAV 文件名 |
| `--device` | `-d` | - | ALSA 音频设备 (使用 aplay -l 查看) |
| `--help` | `-h` | - | 显示帮助信息 |

### 使用示例

#### 1. 使用默认设置（模式1：单次输出到文件）

```bash
./tts_test
```

#### 2. 输出到指定 ALSA 设备

```bash
./tts_test -m 2 -t "你好世界" -d hw:5,0
```

#### 3. 使用自定义文本和输出文件

```bash
./tts_test -t "你好世界，这是一段测试语音。" -o output.wav
```

## play_wav 使用说明

### 功能说明

播放 WAV 文件到默认 ALSA 设备

### 使用示例

```bash
./play_wav generated.wav
```

## 查看可用 ALSA 设备

使用以下命令查看可用的音频设备：

```bash
aplay -l
```

根据输出结果确定设备编号，例如 `card 5, device 0` 对应的设备为 `hw:5,0`。

## 模型配置

TTS 模型文件位于 ../../3rdparty/model/tts/matcha-icefall-zh-en/ 目录下：
- MatchaTTS 声学模型
- Vocos 声码器
- 词典和 token 文件

配置详见 `../sherpa_tts.c`。
