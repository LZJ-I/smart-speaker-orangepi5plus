# player

音乐播放进程：与 ASR/KWS/TTS 通过 FIFO 协同，维护播放状态机（含 GStreamer 子进程），支持本地曲库与在线曲库（`music_source`），TCP 上报与规则/LLM 路由在 `select_loop` 中处理。

## 目录

| 路径 | 职责 |
|------|------|
| `core/` | 入口 `main`、播放状态机 `player`、共享内存 `shm`、GStreamer `player_gst`、控制 FIFO `player_fifo`、常量与类型头文件 |
| `select_loop/` | `select` 主循环、文本与 LLM 分支（`select_text`、`select_music_llm`） |
| `net/` | TCP 客户端、上报、曲库索引 `link` |
| `device/` | 输入设备（音量等） |
| `rules/` | 本地规则匹配 `rule_match` |
| `bridge/` | 曲库桥接 `music_lib_bridge`（在线列表由 **server** 的 `music-lib` 搜索并返回 `play_url`） |
| `music_source/` | 本地/服务端音乐源后端与统一选择 |
| `example/` | 规则匹配、SD 卡挂载等独立小例子 |

## 构建

在仓库根目录 `smart-speaker-client` 下：

```bash
make -C player
```

产物为当前目录下的 `run`。顶层 `make install_bins` 会将其安装为 `build/bin/player_run`。

依赖（编译期）：`pkg-config gstreamer-1.0`、`json-c`、pthread、ALSA；并链接同仓库的 `ipc/ipc_message` 与 `voice-assistant/llm/llm`。

## 运行前置

须在 **`smart-speaker-client` 工程根目录** 的工作环境下启动（与 FIFO、相对路径一致）。启动前执行：

```bash
./player/init.sh
```

用于清理指定 SysV 共享内存/信号量键，并在 `../fifo`（相对 `player` 目录）及 `/tmp` 下创建 ASR/KWS/TTS/控制等命名管道。详细路径见 `init.sh` 与 `core/player_constants.h`。

## 预生成 TTS 提示音（`assets/tts/*.wav`）

`assets/` 已列入客户端根目录 `.gitignore`，Fork 克隆后须本地生成（与仓库根 `README.md`「assets 资源」一致）。

播放器会**直接播文件**（经 TTS 进程 `IPC_CMD_PLAY_AUDIO_FILE`），不经过在线合成。以下路径相对 **`smart-speaker-client` 工程根目录**：

- 模式/连接/兜底：`mode_order.wav`、`mode_single.wav`、`mode_online.wav`、`fallback_unmatched.wav`、`online_music_unsupported.wav`、`server_disconnect_offline.wav`、`server_connect_failed.wav`
- 无指令忽略（`RULE_CMD_NOOP`）：`noop_reply_recall.wav`、`noop_reply_leave.wav`、`noop_reply_ok.wav`（运行时随机选一条）

**依赖**：与本仓库 `voice-assistant/tts` 一致，需已下载/放置 Sherpa-ONNX TTS 模型；编译好的 `voice-assistant/tts/example/tts_test`；运行时可加载 `3rdparty/sherpa-onnx/.../lib`（脚本里已设置 `LD_LIBRARY_PATH`）。

**生成**：在工程根目录执行：

```bash
./tools/gen_mode_tts_wav.sh
```

会调用 `tts_test` 批量写出上述 wav。若缺少模型或库，请先按 `voice-assistant/tts/example/README.md` 准备环境后再执行。

## 语音：音量相关规则（`rules/rule_match.c`）

匹配顺序上，**绝对音量**优先于其它规则：`RULE_CMD_VOL_SET` 在 `rule_match_text` 开头单独判断，避免与「增大/减小 + 音量」类说法冲突；命中后不再走歌单/搜歌。

| 类型 | 条件思路（子串匹配） | 鲁棒性说明 |
|------|----------------------|------------|
| **绝对音量** | 大量「把/将/帮…/调整/设置/调到/**调至**/**改为**/开到/设为…」+ `音量`/`声音`；flex：`音量设置`/`声音设置`/`音量调整`/`声音调整` + 可选 ASCII 空白 + **`到`/`成`/`为`**；无命中时对**去 ASCII 空格**副本再匹配 | 后缀经 `parse_volume_pct_number` 校验取最前合法；`百分之`、半角全角数字、`%`/`％`、分隔含 **`。`** |
| **减小一步** | （`减`/`降低`/`调低` 之一且含 `音量`/`声音`）**或**（含 `音量`/`声音` 且含 `太大`/`有点大`/`小点`/`小一点`/`轻一点`/`调小`/`轻点` 之一） | 必须出现「音量/声音」之一，避免纯「减小」类歧义；`减` 可覆盖「减小音量」 |
| **增大一步** | （`增大`/`增加`/`提高`/`调高` 之一且含 `音量`/`声音`）**或**（含 `音量`/`声音` 且含 `太小`/`有点小`/`大点`/`大一点`/`响一点`/`调大`/`响点` 之一） | 同上 |

执行上：`VOL_UP` / `VOL_DOWN` 每次约 **±10%**（`device_adjust_volume` **先** `device_get_volume` 与硬件对齐再算新值）；`VOL_SET` 走 `device_set_volume`。识别成功后由 `select.c` 用 `device_get_volume` 播报（与唤醒前是否在播无关时的恢复逻辑见该文件 `resume_after_handle`）。

**0～100 与 ALSA 步进**（`device/device.c`）：`ui_percent_to_alsa_step` 与旧版一致（有 dB 范围则走对数感知曲线，否则线性）；读回 `alsa_step_to_ui_percent` 对 0～100 **用同一前向函数**枚举步进取最小误差，并列时优先 `g_current_vol`，避免「设 80 读 79」且避免仅用线性前向导致响度异常。`device_report`/Qt 与 TTS 均走 `device_get_volume`。

## 辅助目标

- `make -C player test_online_music_chain`：仅测在线搜歌 TCP + HTTP URL 链（小工具，不依赖完整 `run`）。

## 相关文档

工程内总览与协议见上级 [`README.md`](../README.md)、[`ipc`](../ipc/) 与 `docs/smart-speaker-client-目录框架重构规范.md`。
