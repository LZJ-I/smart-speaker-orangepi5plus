# OrangePi 3.5mm 音频驱动说明

> 部署与日常运行见 [README.md](../README.md)。

## 本次处理结论

- 已检查 `/boot/orangepiEnv.txt` 与当前系统音频设备列表。
- **本次未修改** `/boot/orangepiEnv.txt`。

## 检查依据

1. `aplay -l` 已存在模拟音频编解码卡（3.5mm 对应）：

- `card 3: rockchipes8388`

2. 读取的 `/boot/orangepiEnv.txt` 当前关键内容：

- `fdtfile=rockchip/rk3588-orangepi-5-plus.dtb`
- `overlays=uart1-m1`

3. 系统 overlay 目录（`/boot/dtb/rockchip/overlay`）中未发现通用的“audio/codec”命名 overlay 可直接追加启用。

## 当前推荐方案

- 不改 boot 层，**TTS 与 音乐播放共用 3.5mm**：均使用 **dmix** 设备，避免“Device or resource busy”。
- **TTS**（`voice-assistant/main_tts/main.c`）：`playback_device = "dmix:CARD=rockchipes8388,DEV=0"`。
- **音乐**（`player/core/player.h`）：`GST_ALSA_DEVICE = "dmix:CARD=rockchipes8388,DEV=0"`（GStreamer alsasink）。
- 两路同时输出时由 ALSA dmix 混音；“播 TTS 时暂停音乐、播完恢复”已通过 `./fifo/player_ctrl_fifo` 的 `tts:start` / `tts:done` 实现。

## 音量控制（键盘 7/8 或按键）

- 音量加减由 **ALSA 混音器** 控制，配置在 `player/device/device.h`：
  - **CARD**：`"hw:3"`（3.5mm 为 card 3，即 rockchipes8388；用卡名字符串会报 “Invalid CTL”）。
  - **ELEM**：`"lineout volume"`（控件名以本机 `amixer -c 3 scontrols` 为准）；若不存在则由 `player/device/device.c` 按优先级选第一个有播放音量的控件。
  - 0–100 与硬件映射在 `device.c` 中为幂曲线（`VOLUME_EXP=2`），使听感更接近线性。

## hw:3,0 立体声要求（AI 必读）

**rockchipes8388 (card 3) 使用 `hw:3,0` 直连时仅支持立体声，不支持单声道。**

- 若用 `hw:3,0` 且输出 mono：`snd_pcm_hw_params_set_channels` 会失败，报「设置声道数失败」
- 处理方式（二选一）：
  1. 使用 `plughw:3,0`：ALSA plug 自动做格式转换，mono 可直接播放
  2. 使用 `hw:3,0`：须在应用层做 mono→stereo 转换后再写入 ALSA

**项目内已实现：**

- `voice-assistant/tts/alsa_output.h`：`CHANNELS=2`
- `voice-assistant/tts/example/play_wav.c`、`voice-assistant/main_tts/main.c`、`voice-assistant/tts/example/tts_test.c`：播放前将 mono 转为 interleaved stereo（L R L R...）再调用 `snd_pcm_writei`

## 若后续必须改 boot 层

建议流程（需谨慎，改后需重启）：

1. 备份 `/boot/orangepiEnv.txt`
2. 按官方内核/板卡文档确认可用 audio overlay 名称
3. 修改 `overlays=` 后重启
4. 复测 `aplay -l` 与 `speaker-test`

在未确认 overlay 名称前，不建议盲改 `orangepiEnv.txt`。
