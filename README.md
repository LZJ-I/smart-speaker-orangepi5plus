# smart-speaker-orangepi5plus

基于 OrangePi 5 Plus 的智能音箱项目仓库。主代码与使用说明在 **smart-speaker-client**（嵌入式端）；另有 **smart-speaker-server**（服务端）、**smart-speaker-qtApp**（Qt 客户端），详见各自目录。

- **克隆**：`git clone https://github.com/LZJ-I/smart-speaker-orangepi5plus && cd smart-speaker-orangepi5plus`
- **client**：见 [smart-speaker-client/README.md](smart-speaker-client/README.md)
- **server**：见 [smart-speaker-server/README.md](smart-speaker-server/README.md)
- **当前重构结果**：
  - `client/player` 已切到本地曲库 + server 曲库双后端
  - `smart-speaker-server` 已整理为 `include/src/tests/docs`
- **系统依赖**：生产环境所需 `apt` 包见各目录 README（`smart-speaker-client/README.md`、`smart-speaker-server/README.md`）；安装齐后再恢复标准技术栈实现。
- **技术文档索引**：[smart-speaker-client/docs/smart-speaker-client-技术文档索引.md](smart-speaker-client/docs/smart-speaker-client-技术文档索引.md)
