# smart-speaker-client 全量重构实施记录

## 本轮完成项

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
  - 顶层 `Makefile` 统一构建 `asr_kws/tts/player/supervisor/tools`。
  - 统一产物输出到 `smart-speaker-client/build/bin`。
  - 新增 `supervisor` 进程（拉起与回收子进程）。
- 测试工具：
  - `tools/ipc_inject.c`：向 IPC FIFO 注入结构化消息。
  - `tools/fifo_watch.c`：监听文本 FIFO。

## 当前已知运行前提

- ALSA 录音设备关键字需可用，否则 `asr_kws_process` 无法启动识别链路。
- 运行入口建议使用：
  - `make -C smart-speaker-client`
  - `make -C smart-speaker-client run`

## 后续建议

- 将 `player` 与 `voice-assistant` 的路径常量进一步收敛到单一配置层。
- 将 `SIGUSR1/SIGUSR2` 兼容处理逐步完全下线，仅保留 `SIGCHLD`。
- 将 `player/select.c` 规则链拆成规则表，进一步降低维护成本。

