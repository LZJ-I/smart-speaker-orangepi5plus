# smart-speaker-client 全量重构实施记录

## 本轮完成项

- 播放器技术基线文档：
  - 新增 `docs/smart-speaker-client-player-三进程播放与分页状态机技术基线.md`。
  - 固化三进程模型、`music-lib` 分页契约、播放模式语义与共享内存一致性策略。
- 离线兜底资产：
  - 新增固定音频 `assets/tts/fallback_unmatched.wav`。
  - 由 `voice-assistant/tts/example/tts_test` 生成，文案固定为“我没有理解您的意思，请换个说法吧。”。
- player 兜底入口收敛：
  - `player/select.c` 新增 `tts_play_audio_file(const char *path)` 单点入口。
  - `RULE_CMD_PLAY_QUERY` 与 `RULE_CMD_NONE` 离线未命中统一改为播放固定 wav。
  - 新增 TTS FIFO 发送前重试打开，修复启动时序导致的 `g_tts_fd` 失效。
- TTS 失败路径补发：
  - `voice-assistant/main_tts/main.c` 的 `IPC_CMD_PLAY_AUDIO_FILE` 分支补齐失败路径 `tts:done`（body 为空、path 为空、path 不存在、执行失败）。
- SD 卡示例：
  - 新增 `player/example/sdcard_mount_example.c`。
  - 实现设备识别、挂载、扫描、卸载全流程与异常路径处理。
  - `player/example/Makefile` 新增 `sdcard_mount_example` 构建目标。
- 联调结果：
  - 三进程由 `supervisor` 拉起：`build/bin/asr_kws_process`、`build/bin/tts_process`、`build/bin/player_run`（`make install_bins` 从 `player/run` 拷贝）。
  - 离线未命中链路已验证走固定 wav，并可看到 `tts:start` / `tts:done` 事件。
  - （后续拆分）TTS 进程已拆为 `main_tts/main.c` + `tts_playback.c` + `tts_ipc_handler.c`；ASR_KWS 已拆出 `asr_kws_pipe.c`；player 已拆出 `player_constants.h`、`player_types.h`、`player_fifo.c`、`select_text.c`、`select_music_llm.c`、`socket_report.c`。

- 语义策略：
  - 规则匹配优先，未命中走 LLM。
  - 唤醒打断后，LLM 路径执行完成恢复播放。
  - 规则命中路径按规则动作执行。
- IPC 协议：
  - 新增固定头 + 变长 body 协议实现：
    - `smart-speaker-client/ipc/ipc_message.h`
    - `smart-speaker-client/ipc/ipc_message.c`
  - ASR_KWS -> TTS 改为协议化消息收发。
- 通信链路：
  - `asr_kws_process` 启用 `asr_fifo/kws_fifo` 输出文本事件。
  - 新增 `asr_ctrl_fifo`，用于 player -> asr_kws 在线/离线模式切换。
- 播放与恢复：
  - `player/select.c` 重构 ASR 规则链和恢复策略。
  - 补齐随机播放规则命中分支。
- 存储介质：
  - 离线模式设备检测新增 `mmcblk*p*` 自动探测。
  - 挂载失败时回退自动文件系统探测挂载。
- 构建系统：
  - 顶层 `Makefile` 统一构建 `asr_kws`、`tts`、`player`、`supervisor`、`tools`，再执行 `install_bins`。
  - 可执行统一拷贝到 `build/bin`：`asr_kws_process`、`tts_process`、`player_run`、`supervisor`、`ipc_inject`、`fifo_watch`。
  - `make run` 会先执行 `player/init.sh`，再运行 `./build/bin/supervisor`。
- 测试工具：
  - `tools/ipc_inject.c`：向 IPC FIFO 注入结构化消息。
  - `tools/fifo_watch.c`：监听文本 FIFO。

## 当前已知运行前提

- ALSA 录音设备关键字需可用，否则 `asr_kws_process` 无法启动识别链路。
- 运行入口建议使用：
  - `make -C smart-speaker-client`
  - `make -C smart-speaker-client run`

## 播放引擎改为 GStreamer

- 播放器使用 **GStreamer**（playbin + alsasink），通过 `./fifo/cmd_fifo` 收命令：`quit`、`cycle pause`、`loadfile 'path'`。
- **修改**：`player/player.h` 定义 `GST_CMD_FIFO`、`GST_ALSA_DEVICE`；`player/player_gst.c` 实现 `run_gst_player(uri)`（GMainLoop + 总线 EOS/错误 + FIFO 命令）；`player/player.c` 孙进程调用 `run_gst_player`，父进程通过 `player_write_fifo` 写 FIFO；`init.sh` 创建 `cmd_fifo`；`makefile` 链入 `gstreamer-1.0`。
- **依赖**：`sudo apt-get install -y libgstreamer1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-alsa`。

## 后续建议

- 将 `player` 与 `voice-assistant` 的路径常量进一步收敛到单一配置层。
- 将 `SIGUSR1/SIGUSR2` 兼容处理逐步完全下线，仅保留 `SIGCHLD`。
- 将 `player/select.c` 规则链拆成规则表，进一步降低维护成本。

