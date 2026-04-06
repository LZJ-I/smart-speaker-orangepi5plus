# smart-speaker-client 文档索引

日常安装、编译、运行、联调以 **[上级 README.md](../README.md)** 为准。下表按**用途**归类，便于查实现细节与历史记录。

## 部署与设备

| 文档 | 内容 |
|------|------|
| [smart-speaker-client-SD卡加载与卸载实操示例.md](smart-speaker-client-SD卡加载与卸载实操示例.md) | SD 挂载/卸载与曲库路径 |
| [orangepi-3.5mm-音频驱动说明.md](orangepi-3.5mm-音频驱动说明.md) | 板载输出与设备号相关说明 |

## 播放与规则

| 文档 | 内容 |
|------|------|
| [smart-speaker-client-player-规则匹配示例清单.md](smart-speaker-client-player-规则匹配示例清单.md) | 规则匹配示例 |
| [smart-speaker-client-player-离线在线切换与兜底策略.md](smart-speaker-client-player-离线在线切换与兜底策略.md) | 离线/在线切换与兜底 |
| [smart-speaker-client-播放模式与语音恢复策略更新说明.md](smart-speaker-client-播放模式与语音恢复策略更新说明.md) | 播放模式、语音恢复与 TTS 协同 |
| [smart-speaker-client-player-三进程播放与分页状态机技术基线.md](smart-speaker-client-player-三进程播放与分页状态机技术基线.md) | 播放器进程与分页语义基线 |

## 语音与音频

| 文档 | 内容 |
|------|------|
| [smart-speaker-client-语音链路状态机与恢复播放方案.md](smart-speaker-client-语音链路状态机与恢复播放方案.md) | 语音状态机与恢复 |
| [smart-speaker-client-TTS与音乐输出冲突方案.md](smart-speaker-client-TTS与音乐输出冲突方案.md) | TTS 与音乐共用输出 |
| [语音播放与IPC问题修复总结.md](语音播放与IPC问题修复总结.md) | IPC、打断、尾音等修复汇总 |

## 目录、链路与排障

| 文档 | 内容 |
|------|------|
| [smart-speaker-client-目录框架重构规范.md](smart-speaker-client-目录框架重构规范.md) | 当前目录与模块划分 |
| [smart-speaker-音乐申请链路与存储.md](smart-speaker-音乐申请链路与存储.md) | 歌曲/列表请求链路与存储（与服务端协同） |
| [smart-speaker-client-player-运行时调试与框架知识总结.md](smart-speaker-client-player-运行时调试与框架知识总结.md) | 运行期排障与约束 |
| [smart-speaker-client-框架重构说明.md](smart-speaker-client-框架重构说明.md) | 框架层设计（未全文落地部分见文内说明） |

## 变更与测试记录

| 文档 | 内容 |
|------|------|
| [smart-speaker-client-本次改动文件清单.md](smart-speaker-client-本次改动文件清单.md) | 改动与模块对应 |
| [smart-speaker-client-全量重构实施记录.md](smart-speaker-client-全量重构实施记录.md) | 重构实施记录 |
| [smart-speaker-client-全量测试结果.md](smart-speaker-client-全量测试结果.md) | 测试结果 |

## 与服务端约定

- 设备↔服务 TCP/JSON 字段与示例：仓库 **`smart-speaker-server/docs/接口定义.txt`**
- 音乐链路角色与快照：**`smart-speaker-server/docs/音乐链路协议说明.md`**
