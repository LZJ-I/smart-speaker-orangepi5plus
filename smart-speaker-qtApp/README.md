# smart-speaker-qtApp

![Qt 控制端（霓虹赛博）](../readme-illustrations/neon-qtapp-focus.svg)

**桌面控制端**：与已部署的 **smart-speaker-server** 对话，查看设备状态与播放队列，发送控制指令。板端音箱本体仍运行在 **smart-speaker-client**。

## 应用做什么

- 连接同一套 TCP+JSON 服务，展示服务端缓存的设备状态与列表。
- 不替代板端播放；适合开发联调或局域网内用电脑操作。

## 部署提要

1. 安装 Qt 开发环境（Widgets、Network 等，与 `smart_speaker_app.pro` 一致）。
2. 用 **qmake** 打开 `smart_speaker_app.pro`，按本机 Kit 编译。
3. 先启动 **smart-speaker-server**，在界面中填写可达的服务端地址与端口（与 `server.toml` 中 `bind_*` 一致）。

## 实现组成

| 文件 | 说明 |
|------|------|
| `smart_speaker_app.pro` | 工程入口 |
| `main.cpp` | 程序入口 |
| `widget.*` / `widget.ui` | 主窗口 |
| `bind.*` / `bind.ui` | 绑定相关界面 |
| `player.*` / `player.ui` | 播放相关界面 |
| `socket.cpp` / `socket.h` | TCP、重连、JSON 读写 |

`smart_speaker_app.pro.user` 为本机 Qt Creator 配置，换机通常需重新生成。

协议字段与示例见 **`smart-speaker-server/docs/接口定义.txt`**；队列与快照语义见 **`smart-speaker-server/docs/音乐链路协议说明.md`**。
