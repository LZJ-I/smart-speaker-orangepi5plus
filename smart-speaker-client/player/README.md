# player

板端上的**音乐播放主进程**：驱动扬声器输出，处理语音侧传来的文本指令（规则与可选 LLM），在本地曲库与已部署的 **smart-speaker-server** 之间切换音源。

## 应用做什么

- 维护播放状态（含与 TTS 的协同），按配置选择本地目录或服务端列表/URL。
- 通过命名管道与同机的 ASR/KWS/TTS 进程配合；向服务端上报状态供桌面端展示。

## 部署提要

- 在 **`smart-speaker-client` 根目录** 下：`make -C player`，产物为 `player/run`。
- 每次重启前在 **`player/`** 执行 `./init.sh`，创建 FIFO 与共享内存键（与 `init.sh`、`core/player_constants.h` 一致）。
- 预置播报用的 `assets/tts/*.wav` 须在客户端根目录执行 `./tools/gen_mode_tts_wav.sh`（依赖 TTS 模型，见上级 `README.md`）。

## 实现组成（目录）

| 路径 | 职责 |
|------|------|
| `core/` | 入口、状态机、GStreamer、FIFO、常量 |
| `select_loop/` | 主循环与文本/LLM 分支 |
| `net/` | TCP、上报、曲库索引 |
| `device/` | 音量等输入 |
| `rules/` | 本地规则匹配 |
| `bridge/`、`music_source/` | 与服务端曲库、本地目录对接 |
| `example/` | 独立小例子 |

依赖（编译期）：`pkg-config gstreamer-1.0`、`json-c`、pthread、ALSA；并链接同仓库的 `ipc`、`voice-assistant/llm`。顶层 `make install_bins` 可将 `run` 安装为 `build/bin/player_run`。

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

## 更多文档

上级 [`README.md`](../README.md)、[`docs/smart-speaker-client-文档索引.md`](../docs/smart-speaker-client-文档索引.md)。
