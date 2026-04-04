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
  libssl-dev
```

若无 `default-libmysqlclient-dev`：`sudo apt install -y libmysqlclient-dev`

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

```bash
sudo apt install -y apache2
```

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

链接：`libevent`、`libjsoncpp`、`libmysqlclient`、`crypto`。

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

## 协议要点

TCP：4 字节小端长度 + UTF-8 JSON。支持 `get_music`、`search_music`、`device_report`、各类 `app_*` 等（见 `src/server.cpp`）。`search_music` 的关键词 **`热门`** 表示全库匹配并随机打乱后分页（与 client 本地后端语义一致）。
