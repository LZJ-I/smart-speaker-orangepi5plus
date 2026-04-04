# smart-speaker-client 目录框架重构规范

## 目标

- 模块边界清晰：识别、控制、播放、第三方依赖解耦。
- 运行产物统一：可执行文件、日志、运行时 FIFO 位置固定。
- 可维护：新增功能优先加模块，不在单文件堆逻辑。

## 当前目录结构

```text
smart-speaker-client/
  Makefile
  3rdparty/
  config/
  docs/
  assets/
  fifo/                 # 运行时 FIFO（init.sh 创建）
  ipc/                  # ipc_message.c/h 统一 IPC 协议
  player/               # 播放器（见下子目录）
  player/core/          # main、player、shm、fifo、gst、constants、types
  player/net/           # link、socket、socket_report
  player/select_loop/   # select、select_text、select_music_llm
  player/device/
  player/rules/         # rule_match
  player/bridge/        # music_lib_bridge
  player/music_source/  # local/server/manager
  voice-assistant/      # main_asr_kws、main_tts、asr/kws/tts/llm/common
  music-lib/            # Rust 音乐库
  supervisor/
  tools/
  build/
    bin/                # 可执行：asr_kws_process, tts_process, player_run, supervisor, ipc_inject, fifo_watch
```

## 模块职责（当前）

- `supervisor`：进程拉起、健康检查、SIGCHLD 回收；产物 `build/bin/supervisor`。
- `voice-assistant/main_asr_kws`：音频采集、KWS/ASR 推理、输出识别文本；产物根目录 `asr_kws_process`，安装到 `build/bin/`。
- `voice-assistant/main_tts`：TTS 进程（main + tts_playback + tts_ipc_handler）；产物根目录 `tts_process`，安装到 `build/bin/`。
- `player`：音乐控制状态机（父子孙模型）、select 多路 IO、规则/LLM 路由；产物 `player/run`，安装为 `build/bin/player_run`。
- `ipc`：统一消息头与读写（ipc_message.c/h）。
- 规则与 LLM：`player/rules/rule_match.c`、`player/select_loop/select_music_llm.c`，LLM 在 `voice-assistant/llm/`。

## 重构原则

- 单向依赖：业务模块依赖 ipc/公共头，避免反向依赖。
- 配置外置：设备、挂载点等可写入 `config`；FIFO 路径见 `ipc_protocol.h`、`player/core/player_constants.h`。
- 运行目录：FIFO 当前在 `./fifo`（player/init.sh 创建）；可执行由 `make install_bins` 装入 `build/bin`。
- 第三方只读：`3rdparty/` 路径保持稳定。

## 编译与产物规范

- 顶层 `Makefile`：`all` 依次构建 asr_kws、tts、player、supervisor、tools，`install_bins` 拷贝到 `build/bin`。
- 子模块各自 `Makefile`，产出 `.o` 或根目录/子目录可执行文件。
- 最终可执行统一在 `build/bin`（asr_kws_process、tts_process、player_run、supervisor、ipc_inject、fifo_watch）。

## 可选未来态（未实施）

- 若后续做物理迁移，可考虑 `src/` 下按 supervisor、voice、player、ipc、framework 等再分子目录；当前仍保持上述扁平/分层混合结构。

