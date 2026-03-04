# OrangePi 3.5mm 音频驱动说明

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

- 不改 boot 层，优先通过应用配置固定播放设备：
  - `smart-speaker-client/config/audio.conf`
  - `playback_device=plughw:3,0`

该方案已通过本次 `tts_process` 启动日志验证生效。

## 若后续必须改 boot 层

建议流程（需谨慎，改后需重启）：

1. 备份 `/boot/orangepiEnv.txt`
2. 按官方内核/板卡文档确认可用 audio overlay 名称
3. 修改 `overlays=` 后重启
4. 复测 `aplay -l` 与 `speaker-test`

在未确认 overlay 名称前，不建议盲改 `orangepiEnv.txt`。
