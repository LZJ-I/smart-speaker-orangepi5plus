# smart-speaker-client 本次改动文件清单

## 代码改动

- `smart-speaker-client/player/select.c`
  - 调整 ASR 处理流程为“规则优先，未命中走 LLM”。
  - 新增恢复播放判定：LLM 执行后恢复；规则按规则行为处理。
  - KWS 在线处理改为“记录恢复态+暂停播放”。

- `smart-speaker-client/voice-assistant/main_asr_kws/main.c`
  - 启用 `asr_fifo` 与 `kws_fifo` 写入。
  - 新增 `write_text_pipe()` 管道写入辅助函数（非阻塞打开 + 断链重连）。
  - ASR 最终文本改为写入 `asr_fifo`，由 player 统一做规则/LLM逻辑。

- `smart-speaker-client/voice-assistant/main_tts/main.c`
  - TTS FIFO 收包改为固定头+变长body协议读取。
  - 播放命令解析改为 `type + payload`。

- `smart-speaker-client/ipc/ipc_message.h`
- `smart-speaker-client/ipc/ipc_message.c`
  - 新增统一 IPC 报文协议实现。

- `smart-speaker-client/player/player.c`
  - 在线/离线模式切换改为通过 `asr_ctrl_fifo` 通知 ASR。
  - 新增 SD 卡 `mmcblk*p*` 自动探测并挂载。

- `smart-speaker-client/player/main.c`
- `smart-speaker-client/player/init.sh`
  - 统一启动路径与 FIFO 初始化（新增 `asr_ctrl_fifo`）。

- `smart-speaker-client/Makefile`
- `smart-speaker-client/supervisor/main.c`
- `smart-speaker-client/supervisor/Makefile`
- `smart-speaker-client/tools/ipc_inject.c`
- `smart-speaker-client/tools/fifo_watch.c`
- `smart-speaker-client/tools/Makefile`
  - 完成主子构建体系、supervisor、测试工具落地。

## 新增技术文档

- `docs/smart-speaker-client-语音链路状态机与恢复播放方案.md`
- `docs/smart-speaker-client-目录框架重构规范.md`
- `docs/smart-speaker-client-本次改动文件清单.md`
- `docs/smart-speaker-client-全量重构实施记录.md`
- `docs/smart-speaker-client-技术文档索引.md`

## 后续计划中待落地

- 规则链配置化（规则表 + 外置词典）。
- 彻底下线业务信号路径，保留 SIGCHLD。
- 统一音频设备配置抽象与自检脚本。

