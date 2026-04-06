# TTS 示例程序

> 模型路径与 `3rdparty` 准备见 [smart-speaker-client/README.md](../../../README.md)；批量生成板端 `assets/tts` 用上级 `tools/gen_mode_tts_wav.sh`。

本目录包含基于 Sherpa-onnx 的 TTS（文本到语音）和 WAV 播放示例程序。

## 功能特性

- 支持两种输出模式的 TTS 合成
- **批量模式（`-b`）**：只加载一次模型，按清单连续合成多条，适合批量生成 `assets/tts` 提示音
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
| `--batch` | `-b` | - | 批量合成清单文件路径（与 `-t`/`-o` 互斥） |
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

#### 4. 批量合成（推荐用于多条固定文案）

在**本目录**下执行（模型路径相对 `voice-assistant/tts/example` 解析）。清单为 UTF-8 文本文件：

- 每行一条：`输出 WAV 绝对路径` + **制表符（Tab）** + `合成文本`
- 空行、以 `#` 开头的行忽略
- 程序**只初始化 / 释放 TTS 一次**，避免每条文案都重复加载模型

```bash
# 示例 manifest.txt（路径请按实际修改）
# /path/to/out.wav<TAB>要说的字
printf '%s\t%s\n' /tmp/a.wav "第一句" /tmp/b.wav "第二句" > /tmp/manifest.txt
./tts_test -b /tmp/manifest.txt
```

#### 5. 生成智能音箱预合成提示音（项目内使用）

在仓库 `smart-speaker-client` 根目录执行：

```bash
./tools/gen_mode_tts_wav.sh
```

脚本会编译 `tts_test`、写入临时批量清单并调用 **`./tts_test -b <清单>`**，一次性生成 `assets/tts/` 下模式切换、兜底、暂停/停止随机句等全部 WAV（详见脚本内 `add_line` 列表）。

## play_wav 使用说明

### 功能说明

播放 WAV 文件到指定 ALSA 设备

### 命令行参数

| 参数 | 说明 |
|-----|------|
| `<wav文件>` | 要播放的 WAV 文件路径（必填） |
| `[ALSA设备]` | ALSA 音频设备（可选，默认为 default） |

### 使用示例

#### 1. 播放到默认设备
```bash
./play_wav generated.wav
```

#### 2. 播放到指定设备
```bash
./play_wav generated.wav hw:0,0
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
