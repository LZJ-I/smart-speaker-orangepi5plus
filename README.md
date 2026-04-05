# smart-speaker-orangepi5plus

![架构示意（霓虹赛博）](readme-illustrations/neon-architecture-overview.svg)

基于 OrangePi 5 Plus 的智能音箱项目仓库。主代码与使用说明在 **smart-speaker-client**（嵌入式端）；另有 **smart-speaker-server**（服务端）、**smart-speaker-qtApp**（Qt 客户端），详见各自目录。

- **克隆**：`git clone https://github.com/LZJ-I/smart-speaker-orangepi5plus && cd smart-speaker-orangepi5plus`
- **client**：见 [smart-speaker-client/README.md](smart-speaker-client/README.md)
- **server**：见 [smart-speaker-server/README.md](smart-speaker-server/README.md)
- **当前重构结果**：
  - `client/player`：本地曲库 + server 双后端；连接与音量等见 **`data/config/client.toml`**（首次运行生成）
  - `smart-speaker-server`：`include/src/tests/docs`，另含 **`music-service/`**（Node）、**`data/config/*.toml`**（首次运行生成）
- **协议摘要**：`smart-speaker-server/docs/接口定义.txt`
- **系统依赖**：生产环境所需 `apt` 包见各目录 README（`smart-speaker-client/README.md`、`smart-speaker-server/README.md`）；安装齐后再恢复标准技术栈实现。
- **技术文档索引**：[smart-speaker-client/docs/smart-speaker-client-技术文档索引.md](smart-speaker-client/docs/smart-speaker-client-技术文档索引.md)
