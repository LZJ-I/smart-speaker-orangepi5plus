# smart-speaker-server

![服务端枢纽（霓虹赛博）](../readme-illustrations/neon-server-focus.svg)

TCP 服务端：设备/APP 转发、账号（MySQL）、本地曲库扫描（与 Apache 静态目录共用磁盘）。

## 目录

- `include/`：头文件
- `src/`：源码
- `tests/`：联调小程序
- `docs/`：协议说明

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
  cargo
```

若无 `default-libmysqlclient-dev`：`sudo apt install -y libmysqlclient-dev`

### Rust（music-lib）

聚合搜歌与取流 URL 使用仓库内 **`music-lib/`**（Rust crate `music_downloader`，产出 `libmusic_downloader.so`）。`make` 时会自动执行 `cargo build --release`。

- **Rust**：建议 `rustup` 安装 stable（需支持 `edition = "2024"` 的 toolchain，或把 `music-lib/Cargo.toml` 中 `edition` 改为 `2021`）。
- **OpenSSL**：`music-lib` 依赖 `openssl-sys`（`libssl-dev` 已覆盖）。
- **环境变量**（可选）：
  - `SMART_SPEAKER_MUSIC_PLATFORM`：搜索平台，`auto`（默认）/`tx`/`wy`
  - `SMART_SPEAKER_MUSIC_QUALITY`：取链音质，默认 `128k`

**运行 `server_smart_speaker` 时**需能加载同目录相对路径下的 `music-lib/target/release/libmusic_downloader.so`（Makefile 已设置 `rpath`）；若移动可执行文件，请同步拷贝 `.so` 或设置 `LD_LIBRARY_PATH`。

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

### Apache（HTTP 下发 mp3）

服务端 **不** 内置 HTTP；设备在线播放 URL 一般为 `http://<主机>/music/...`，需本机安装 Apache（或 nginx）并把站点根指到放歌的目录。

**安装与启动（简要）**

```bash
sudo apt update
sudo apt install -y apache2
sudo systemctl enable apache2   # 开机自启（可选）
sudo systemctl start apache2
sudo systemctl status apache2   # 应显示 active (running)
```

本机可测：`curl -I http://127.0.0.1/` 应返回 `HTTP/1.1 200`。若改配置后需重载：`sudo systemctl reload apache2`；停止：`sudo systemctl stop apache2`。

默认 `DocumentRoot` 为 `/var/www/html`。把歌曲放到：

```text
/var/www/html/music/
├── 歌手目录/
│   └── 歌曲.mp3
```

与代码默认 `MUSIC_PATH` 一致。若曲库不在该路径，启动前设置：

```bash
export SMART_SPEAKER_MUSIC_PATH=/你的/音乐根目录/
```

（须以 `/` 结尾或程序会自动补；目录结构仍为「子目录=歌手，内为 .mp3」。）

## 编译

```bash
cd smart-speaker-server
make
make tests   # 可选
```

链接：`libevent`、`libjsoncpp`、`libmysqlclient`、`crypto`、`libmusic_downloader`（Rust 构建）、`pthread`、`dl`。

## 运行

端口占用时：

```bash
make stop
./server_smart_speaker
```

环境变量：

- `SMART_SPEAKER_SERVER_IP`：监听 IP，默认 `0.0.0.0`（见 `src/main.cpp`）
- `SMART_SPEAKER_SERVER_PORT`：端口，默认 `8888`
- `SMART_SPEAKER_MUSIC_PATH`：曲库扫描根目录，默认 `/var/www/html/music/`

初始化失败（连不上 MySQL 等）时进程退出码为 1。

在 **`smart-speaker-server` 当前工作目录**下启动时，标准输出与 **`data/server/app.log`** 会同步写入同一套运行日志（`data/` 已加入 `.gitignore`）。

## 协议要点

TCP：4 字节小端长度 + UTF-8 JSON。支持 `get_music`、`search_music`、`list_music`、`get_play_url`、`device_report`、各类 `app_*` 等（见 `src/server.cpp`）。

- **`list_music`**：可选字段 **`keyword`**。未传、`keyword` 为空或与泛意图同义（如 **`热门`**、`听歌`、`音乐` 等）时，返回**本地曲库**随机打乱后的分页；否则由 **`music-lib`** 走 QQ/网易云聚合搜索；每条结果含 `source`/`song_id`/`singer`/`song`，**`play_url` 在取链成功时填充**（失败时仍返回条目，由嵌入式端再通过 **`get_play_url`** 按需取链）。远程失败或结果为空时回退为本地路径扫描关键词分页（与 `search_music` 类似）。
- **`get_play_url`**：字段 **`source`**、**`song_id`**，响应 **`reply_get_play_url`**，`result` 为 `ok` 时含 **`play_url`**（供 `tx`/`wy` 等与 Apache 路径无关的曲目）。
- **`search_music`**：仍表示仅扫本地磁盘路径的关键词分页。
