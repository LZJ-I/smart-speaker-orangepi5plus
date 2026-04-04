# smart-speaker-client 框架重构说明

**说明**：本文描述的是计划中的 framework 公共层与目录调整方案，**当前代码库未按此全文落地**。`player` 已拆为 `core/`、`net/`、`select_loop/`、`device/`、`rules/`、`bridge/`、`music_source/` 等，详见 `docs/smart-speaker-client-目录框架重构规范.md`。

---

## 重构目标

本次在 `smart-speaker-client` 内进行大幅重构，目标是：

- 统一公共能力，减少重复代码与模块耦合
- 强化运行约束与错误边界，降低“偶发可用”问题
- 让后续扩展（新增子进程、替换模型、调整 IPC）仅改少数公共文件

## 核心架构调整

### 1) 新增 `framework` 公共层

新增目录：`smart-speaker-client/framework`

- `app_paths.h/.c`
  - 统一路径常量（FIFO、唤醒音、LLM 脚本、模型前缀）
  - 提供 `app_paths_require_project_root()` 统一运行目录校验
- `fifo_utils.h/.c`
  - 提供 FIFO 创建、打开、可靠写入、断连重开能力
  - 统一 `EPIPE/ENXIO/EAGAIN` 场景处理
- `process_utils.h/.c`
  - 提供基于命令的 PID 查找与多候选进程信号发送能力

### 2) 进程入口统一校验

在以下进程入口加入统一 cwd 校验（必须在 `smart-speaker-client` 目录运行）：

- `player/core/main.c`
- `voice-assistant/main_asr_kws/main.c`
- `voice-assistant/main_tts/main.c`

若目录不正确，直接 fail-fast，避免后续出现隐式错误。

### 3) IPC 与信号链路公共化

- `player/core/player.c`
  - 管道创建/打开改为复用 `fifo_utils`
  - `asr_kws` 信号通知改为复用 `process_utils`（多候选进程名匹配）
- `voice-assistant/main_asr_kws/main.c`
  - 管道创建、非阻塞打开、写入重连逻辑改为复用 `fifo_utils`
- `player/select_loop/select.c`（及 `select_text`、`select_music_llm`）
  - `tts_process` PID 获取改为复用 `process_utils`（计划中）

### 4) 路径与模型配置收敛

- `voice-assistant/common/ipc_protocol.h` 改为引用 `framework/app_paths.h` 的统一路径常量
- `player/core/player.h` 管道路径改为引用公共常量
- `voice-assistant/asr/sherpa_asr.c`
- `voice-assistant/kws/sherpa_kws.c`
- `voice-assistant/tts/sherpa_tts.c`
- `voice-assistant/llm/llm.c`
  - 模型根路径和脚本路径统一，减少 `./`、`../` 分支混乱

### 5) 示例工程兼容性修复

- `voice-assistant/asr/sherpa_asr.c`
  - 为 `KWS_TEST_MODE` 补齐 `g_last_asr_text` 等符号定义，修复 `asr/example` 链接失败

### 6) 音频配置独立化

- 新增 `smart-speaker-client/config/audio.conf`，将通用音频参数独立管理：
  - `capture_device_keyword`
  - `playback_device`
- 新增 `framework/audio_config.h/.c` 作为统一配置读取入口
- 接入点：
  - `voice-assistant/common/alsa.c`（录音设备关键字读取）
  - `voice-assistant/main_tts/main.c`（播放设备读取与解析）
- 设备解析逻辑升级：
  - `arecord -l`/`aplay -l` 由 `popen + 行解析` 实现，避免旧版一次性读取缓冲区截断
  - 播放设备支持 `default` / `hw:*` / `plughw:*` / 关键字匹配

## 构建系统调整

为主进程构建链路接入 `framework`：

- `player/makefile`
- `voice-assistant/main_asr_kws/Makefile`
- `voice-assistant/main_tts/Makefile`
- 音频配置模块：`framework/audio_config.o`

新增对 `framework/*.o` 的编译与清理规则。

## 重构收益

- 路径策略、FIFO 处理、进程信号不再散落在各模块，维护点明显减少
- 运行目录错误会被立即识别，不再出现“运行了但行为异常”
- IPC 行为更稳定，重连与异常场景处理更一致
- 后续新增模块时可直接复用 `framework` 能力层
