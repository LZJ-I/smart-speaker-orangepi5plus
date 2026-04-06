# smart-speaker-server

![服务端枢纽（霓虹赛博）](../readme-illustrations/neon-server-focus.svg)

本目录是智能音箱的**服务端**：设备与桌面端经此转发与登记；维护账号；扫描本地曲库；在配置就绪时提供在线搜歌与取链。板端客户端见 **`smart-speaker-client`**，桌面控制端见 **`smart-speaker-qtApp`**。

## 应用做什么

- 接收板端/APP 的 TCP 长连与业务 JSON，转发状态与列表。
- 账号数据在 MySQL；本地音乐在磁盘目录（`server.toml` 中的 `music_root`）。
- 在线能力依赖首次运行生成的 **`data/config/*.toml`**（音源脚本或直填 API；Node 子进程由服务拉起或协同）。

## 部署提要

1. 安装下文「依赖」中的包；本机安装 MySQL/MariaDB 并建库建用户。
2. `make` 编译（会顺带构建 Rust `music-lib`）。
3. 首次 `./server_smart_speaker` 前确保 `music_runtime_init` 所需脚本或 API 配置已就绪，否则进程会直接退出（终端有提示）。
4. 曲库音频建议 `歌手名/歌曲文件` 两级目录，便于列表展示；仅平铺在根目录时部分字段可能为空。
5. 与板端联调时，在板端 `client.toml` 填写本机 `bind_ip`/`bind_port`。

## 实现组成（目录）

- 主服务：`include/`、`src/`（TCP 与业务逻辑）。
- `music-lib/`：远程搜索与取链动态库，由主服务加载。
- `music-service/`：Node 小服务，与主服务配置联动。
- `tests/`：联调小程序；`docs/`：接口与链路说明（**`接口定义.txt`**、**`音乐链路协议说明.md`**）。

## 依赖（开发）

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  libevent-dev \
  libjsoncpp-dev \
  default-libmysqlclient-dev \
  libssl-dev \
  pkg-config \
  cargo \
  curl \
  nodejs
```

若无 `default-libmysqlclient-dev`：`sudo apt install -y libmysqlclient-dev`。`music-service/index.js` 当前仅用 Node 内置模块，无强制 `npm install`。

### Rust（music-lib）

`make` 时会 `cargo build --release`。可执行文件通过 `rpath` 加载 `music-lib/target/release/libmusic_downloader.so`；若移动二进制须同步 `.so` 或 `LD_LIBRARY_PATH`。

- **Rust**：建议 `rustup` stable；若 toolchain 不支持 `edition = "2024"`，可将 `music-lib/Cargo.toml` 改为 `2021`。

## 配置文件（`data/`，已 `.gitignore`）

均在 **`smart-speaker-server` 当前工作目录** 下相对路径；首次运行会创建默认文件（见 `src/runtime_config.cpp`、`src/music_runtime_init.cpp`）。

| 文件 | 作用 |
|------|------|
| `data/config/server.toml` | `bind_ip`、`bind_port`、`music_root`（本地曲库扫描根，默认 `data/music-library/`）、`legacy_platform` / `legacy_quality`（传给 Rust 搜歌/取链）、`music_service_host` / `music_service_port` / `music_service_base_path`（Node 子服务） |
| `data/config/music.toml` | 洛雪脚本下载与 API：`lx_script_import_url`、`lx_script_save_path`、`music_api_url`、`music_api_key`、`music_user_agent` 等 |
| `data/config/music-service.toml` | Node 监听与脚本路径；启动时由 C++ 根据 `music.toml` 同步 `resolver_api_*` 与 `music_source_script` |

**启动硬前置**：`music_runtime_init()` 必须成功（存在可读洛雪类 `lx.js` 且能解析出 `API_URL`/`API_KEY`，或 `music.toml` 已填 `music_api_url` / `music_api_key`），否则 `server_smart_speaker` **直接退出**（见 `src/main.cpp`）。终端会打印缺失项与配置文件路径。

## 运行期

### MySQL / MariaDB

创建库与用户（与 `include/database.h` 中宏一致，可按需改宏后重编）：

```sql
CREATE DATABASE IF NOT EXISTS musicplayer_info CHARACTER SET utf8mb4;
CREATE USER IF NOT EXISTS 'musicplayer'@'localhost' IDENTIFIED BY 'musicplayer';
GRANT ALL ON musicplayer_info.* TO 'musicplayer'@'localhost';
FLUSH PRIVILEGES;
```

服务启动时会自动建表 `account`（若不存在）。

本地随机/关键词分页等仍遍历 **`music_root`** 下目录结构（子目录为歌手名，内为音频文件）。默认目录为仓库下 **`data/music-library/`**（在 `server.toml` 修改；相对路径相对 `server_smart_speaker` 工作目录）。

## 编译

```bash
cd smart-speaker-server
make
make tests   # 可选
```

链接：`libevent`、`libjsoncpp`、`libmysqlclient`、`crypto`、`libmusic_downloader`、`pthread`、`dl`。

## 运行

```bash
make stop   # 可按环境变量 SMART_SPEAKER_SERVER_PORT 杀占用端口（默认 8888）
./server_smart_speaker
```

- **监听地址/端口、曲库根**：`data/config/server.toml` 的 `bind_ip`、`bind_port`、`music_root`（**不再**使用文档中已废弃的 `SMART_SPEAKER_SERVER_IP` / `SMART_SPEAKER_MUSIC_PATH` 作为运行配置）。
- **联测小程序**：`tests/test_client.c` / `test_app.cpp` 仍可用 **`SMART_SPEAKER_SERVER_IP`**、**`SMART_SPEAKER_SERVER_PORT`** 指向被测实例。
- **music-lib 独立示例**：`music-lib/examples/` 内程序若需 Key，以该目录 README 为准（与 C++ 主服务的 `music.toml` 配置方式不同）。

初始化失败（`music_runtime_init`、MySQL 等）时退出码为 1。`music_service_restart_local` 失败会打印 **`music-service 未就绪`**，但 TCP 服务仍可能继续启动（本地曲库等不依赖 Node 的路径仍可用）。

日志：标准输出与 **`data/server/app.log`**（`app_log_init`）。

## 与客户端的约定

报文格式与字段说明见 **`docs/接口定义.txt`**；多端队列与状态含义见 **`docs/音乐链路协议说明.md`**。实现以当前 `src/` 为准。

## 在线能力说明与使用约定

- 本服务的在线搜歌、取链等能力依赖 `data/config/*.toml` 中配置的脚本、API 或自定义源。服务端仅负责调用、转发与结果展示，**不保证**返回的数据（包括歌曲名、歌手、封面、音频链接等）的合法性、准确性与可用性。
- 在线音频是否正确、是否可播放，取决于所配置的数据源返回结果；若出现歌曲不匹配、链接失效、无法播放等情况，属于上游源或外部服务问题。
- 本地列表、账号外的同步数据等内容来自使用者本地系统、客户端或其连接的同步服务，本项目不对其合法性、准确性负责。
- 使用过程中可能接触到图片、音频、名称等版权数据，这些数据的权利归原权利人所有。请遵守当地法律法规，尊重版权、支持正版；如因测试或使用产生相关缓存/数据，请在 **24 小时内**自行清理。
- 文档或代码中出现的平台别名仅用于技术区分，不含恶意；若相关资源或表述存在侵权或不妥之处，可联系移除或更正。
- 本项目**免费开源**，仅用于学习、研究与技术交流，**不用于商业用途**，也不对使用或无法使用本项目造成的任何直接或间接损失承担责任。
- **使用本项目即视为你已知悉并接受上述说明。**
