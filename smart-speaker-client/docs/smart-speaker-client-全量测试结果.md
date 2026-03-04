# smart-speaker-client 全量测试结果

## 测试范围

本次测试覆盖：

1. 主工程全量构建
2. `voice-assistant` 四个 example 子工程构建
3. 三大主进程启动冒烟（`player` / `asr_kws_process` / `tts_process`）
4. 运行目录约束校验（正确目录与错误目录）
5. `TTS FIFO IPC` 端到端消息接收与执行链路
6. 音频配置文件生效验证（麦克风与扬声器）
7. 关键词打断 TTS（生成中/输出中）验证

## 测试命令与结果

### 1) 主工程构建

- 命令：
  - `make clean && make`
  - `make -C player clean && make -C player`
  - `make -C voice-assistant/main_asr_kws`
  - `make -C voice-assistant/main_tts`
- 结果：通过

### 2) example 构建

- 命令：
  - `make -C voice-assistant/asr/example clean && make -C voice-assistant/asr/example`
  - `make -C voice-assistant/kws/example clean && make -C voice-assistant/kws/example`
  - `make -C voice-assistant/llm/example clean && make -C voice-assistant/llm/example`
  - `make -C voice-assistant/tts/example clean && make -C voice-assistant/tts/example`
- 结果：通过  
- 说明：`asr/example` 原先链接失败已修复（`KWS_TEST_MODE` 符号补齐）。

### 3) 正确目录启动冒烟（在 `smart-speaker-client` 下）

- `timeout 10s ./tts_process`
  - 结果：成功启动，进入“等待命令”
- `timeout 10s ./player/player`
  - 结果：成功启动并完成初始化（被 timeout 截断）
- `timeout 10s ./asr_kws_process`
  - 结果：成功启动并进入关键词识别模式（被 timeout 截断）

### 4) 错误目录启动校验（在仓库根目录）

- `./smart-speaker-client/tts_process` -> 退出，提示 cwd 错误
- `./smart-speaker-client/asr_kws_process` -> 退出，提示 cwd 错误
- `./smart-speaker-client/player/player` -> 退出，提示 cwd 错误

结论：运行目录约束校验生效，行为符合设计。

### 5) TTS IPC 端到端验证

步骤：

1. 启动 `./tts_process`
2. 通过 Python 向 `./fifo/tts_fifo` 写入两条 `IPCMessage`：
   - `type=0`（`IPC_CMD_PLAY_TEXT`，文本“这是一次IPC测试”）
   - `type=2`（`IPC_CMD_STOP_PLAYING`）

结果（来自 `tts_process` 日志）：

- 收到并解析完整 IPC 消息
- 正确识别 `PLAY_TEXT` 并开始生成
- 正确识别 `STOP_PLAYING`（重复 stop 已被去重逻辑拦截）
- 音频播放线程正常结束

结论：`TTS FIFO IPC` 链路可用，协议收发与执行正常。

### 6) 音频配置生效验证

配置文件：

- `smart-speaker-client/config/audio.conf`
  - `capture_device_keyword=MICCM379`
  - `playback_device=plughw:3,0`

验证结果：

- `tts_process` 启动日志显示：
  - `使用播放设备: plughw:3,0 (resolved from plughw:3,0)`  
  说明扬声器配置已被正确读取并生效。
- `asr_kws_process` 启动日志显示：
  - `找到录音设备: plughw:4,0`  
  与 `arecord -l` 中的 `MICCM379` 设备匹配，说明麦克风配置生效。

### 7) 关键词打断 TTS 验证

测试目标：

- 当 TTS 正在生成或输出时，被关键词打断；
- 被打断后播放唤醒音（三选一：`nihao.wav`/`wozai.wav`/`zaine.wav`）。

测试方法（实测）：

1. 启动 `tts_process` 与 `player/player`；
2. 先发送长文本到 `tts_fifo`（触发长时间 TTS 生成/播放）；
3. 随后向 `kws_fifo` 注入关键词 `小米小米`。

关键日志证据：

- `tts_process`：
  - `收到播放文本命令 ...`
  - `TTS生成线程启动`
  - `收到SIGUSR1信号，停止当前播放`
  - `检测到停止请求，丢弃生成的音频`
- `player`：
  - `[KWS在线]读取kws管道数据[小米小米]`
  - `播放唤醒响应: ./voice-assistant/wake_audio/wozai.wav`

结论：

- 打断链路生效（TTS 在生成阶段被中断并丢弃音频）；
- 唤醒响应已触发，且实际命中三选一中的 `wozai.wav`。

## 额外检查

- 对重构涉及文件执行 `ReadLints`，无新增 linter 错误。

## 最终结论

本次“框架级重构 + 全量测试”结果为通过：

- 构建链路通过
- 示例链路通过
- 主进程启动链路通过
- 运行目录约束通过
- 关键 IPC（TTS）链路通过
- 音频配置文件读取与设备解析通过
- 关键词打断 TTS 与唤醒音输出通过

当前版本可作为后续功能迭代与扩展的稳定基线。
