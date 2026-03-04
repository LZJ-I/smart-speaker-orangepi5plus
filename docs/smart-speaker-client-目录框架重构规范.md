# smart-speaker-client 目录框架重构规范

## 目标

- 模块边界清晰：识别、控制、播放、第三方依赖解耦。
- 运行产物统一：可执行文件、日志、运行时 FIFO 位置固定。
- 可维护：新增功能优先加模块，不在单文件堆逻辑。

## 推荐目录（目标态）

```text
smart-speaker-client/
  Makefile
  3rdparty/
  config/
  docs/
  runtime/
    fifo/
    logs/
  src/
    supervisor/
    voice/
      asr_kws/
      tts/
      llm/
      common/
    player/
    nlu/
    ipc/
    framework/
  tools/
  tests/
  build/
    bin/
    obj/
```

## 模块职责

- `src/supervisor`：进程拉起、健康检查、SIGCHLD 回收。
- `src/voice/asr_kws`：音频采集、KWS/ASR 推理、输出识别文本。
- `src/nlu`：规则匹配、LLM 兜底路由。
- `src/player`：音乐控制状态机（父子孙模型保留）。
- `src/ipc`：统一消息头、读写工具、重连策略。
- `src/framework`：路径、配置、日志、进程工具等公共能力。

## 重构原则

- 单向依赖：`业务模块 -> ipc/framework`，禁止业务模块互相跨层调用。
- 配置外置：设备、FIFO、挂载点、策略阈值写入 `config`。
- 运行目录固定：所有 FIFO 放 `runtime/fifo`。
- 第三方只读：`3rdparty/model` 路径保持稳定，不在业务代码散落硬编码。

## 编译与产物规范

- 顶层 `Makefile` 统一入口。
- 子模块维护各自 `Makefile`，产出 `.o/.a`。
- 最终可执行统一写入 `build/bin`。
- 调试与发布参数分离：`BUILD=debug/release`。

## 本阶段执行策略

- 先稳链路：ASR/KWS -> 规则/LLM -> TTS/Player。
- 再搬目录：代码稳定后再做物理迁移，避免边改边搬导致回归成本激增。

