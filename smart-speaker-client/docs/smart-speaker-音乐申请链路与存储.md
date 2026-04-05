# 音乐：申请链路与存储说明

本文描述 **smart-speaker-client（player）** 与 **smart-speaker-server（C++ + music-service Node + music-lib Rust）** 之间，针对**歌曲 / 歌单 / 歌手**（及泛化「列表 + 取播放 URL」）的请求路径与数据落点。实现以当前代码为准。

---

## 1. 传输与协议约定

| 层级 | 说明 |
|------|------|
| **Client ↔ C++ Server** | TCP 二进制帧：先 4 字节小端长度，再 UTF-8 JSON。见 `server.cpp` `server_send_data` 与 `music_source_server.c` `server_send_request` / `server_recv_response`。 |
| **C++ Server ↔ Node music-service** | HTTP `POST`，JSON body。`music_service_client.cpp` 中 `music_service_post_json`。地址由 `data/config/server.toml`（`music_service_host` / `music_service_port` / `music_service_base_path`）解析到 `ServerRuntimeConfig`。 |
| **C++ Server ↔ Rust music-lib** | 进程内链接，C ABI：`music_get_url`、`music_resolve_keyword`、`music_search_page` 等（`music-lib`）。依赖环境变量 `SMART_SPEAKER_MUSIC_API_KEY` 等。 |

---

## 2. 客户端（player）申请链路

### 2.1 路由入口

- **`music_source_manager.c`**：在线时优先 **`music_source_server` 后端** 的 `search` / `get_url`；失败可回退 **`music_source_local`**（扫描本地目录）。
- **填链表 / 插入当前曲后**：**`music_lib_bridge.c`** 调用 `music_source_search` 或 `music_source_server_*`，再写入 **`link.c` 双向链表**。

### 2.2 按场景

| 场景 | 主要调用链 | TCP `cmd`（至 C++ Server） | 服务端后续 |
|------|------------|---------------------------|------------|
| **分页搜歌（填充播放列表）** | `music_lib_search_fill_list_page` → `music_source_search` → `music_source_server_list_music_page` | `music.search.song` | `proxy_music_service_list` → Node `POST /music/search/song` |
| **在当前曲后插入一页搜索结果** | `music_lib_insert_search_after_current` | 同上 | 同上 |
| **语音/异步「点歌」单条解析** | `music_source_server_resolve_keyword`：先 `music.search.song` 取第一页第一条，若无 `play_url` 再 `music.url.resolve` | `music.search.song`，必要时 `music.url.resolve` | 搜歌走 Node；取链 `proxy_music_service_resolve` → `POST /music/url/resolve` |
| **歌单（在线且 ASR 含「歌单」）** | `music_source_server_resolve_playlist_keyword`：`music.search.playlist` 取首个歌单 → `music.playlist.detail` 拉曲目列表 | `music.search.playlist`、`music.playlist.detail` | `proxy_music_service_list` / `proxy_music_service_detail` → Node 对应路径 |
| **仅本地** | `music_source_local` `search` / `get_url` | 无 TCP（不经过 server） | 扫描 `SMART_SPEAKER_LOCAL_MUSIC_DIR` 等配置的根目录 |

**说明**：player 侧 **未** 直接调用 TCP `list_music` / `search_music` / `resolve_music` 填链；当前在线搜歌主路径是 **`music.search.song`**。C++ 仍保留 **`list_music`** 等命令，供其它客户端或兼容路径使用（见下节）。

### 2.3 取播放 URL（`play_url`）

- 列表项里可能已带 **`play_url`**（Node 或 Rust 列表逻辑填入）。
- 若节点只有 **`source` + `song_id`/`id`**：`player.c` **`resolve_play_url`** → **`music_source_get_url`** → 在线非 local 时 **`music_source_server_get_url`** → TCP **`music.url.resolve`**（字段 `source`、`id` 或与 `song_id` 同源）→ Node **`/music/url/resolve`**。
- 旧式 **`get_play_url`** TCP 命令仍存在于 server，走 **Rust `music_get_url`**（与 Node 并行存在，用途见服务端表）。

---

## 3. 服务端（C++ `server_smart_speaker`）命令与转发

### 3.1 新音乐子服务（经 Node）

| TCP `cmd` | Node HTTP 路径 | 典型 `reply` 字段 |
|-----------|----------------|-------------------|
| `music.search.song` | `/music/search/song` | `result`、`items[]`、`page`、`total`、`total_pages`；项含 `kind`、`source`、`id`、`title`、`subtitle`、`play_url` 等（`normalize_music_service_items` 补全） |
| `music.search.playlist` | `/music/search/playlist` | 同上，`kind` 多为 `playlist` |
| `music.search.artist` | `/music/search/artist` | 同上（**当前 player 未调用**，服务端已代理） |
| `music.playlist.detail` | `/music/playlist/detail` | 请求带 `id`（歌单 ID），`items` 为歌曲列表 |
| `music.url.resolve` | `/music/url/resolve` | `play_url`、`source`、`id`、`title`、`subtitle` 等 |
| `music.artist.hot` | `POST /music/artist/hot`（`proxy_music_service_detail`） | **当前 player 未调用** |

### 3.2 旧 / 兼容路径（Rust `music-lib` + 本地磁盘）

| TCP `cmd` | 实现要点 |
|-----------|----------|
| `list_music` | 无关键词或泛词：`fill_list_music_from_local_cache`（内存里扫盘缓存 + 打乱分页）。有关键词且配置了 API：`music_remote_list_music_page`（Rust 搜索 + 首条 `music_get_url`）；失败则 `fill_list_music_from_local_keyword` 本地子串匹配。 |
| `search_music` | 本地曲库路径匹配分页。 |
| `resolve_music` | `music_resolve_keyword`（搜索 + 取链，一次返回元数据 + `play_url`）。 |
| `get_play_url` | `music_get_url(source, song_id, legacy_quality)`，**不经 Node**。 |

配置项 **`legacy_platform` / `legacy_quality`** 见 `runtime_config.cpp` 与 `data/config/server.toml`，影响 Rust 取链音质等。

---

## 4. Node `music-service`（概念层）

- 监听配置：`data/config/music-service.toml`（如 `host`/`port`、`resolve_quality`、酷我脚本路径等）。
- 对 C++ 提供 **HTTP JSON**：搜索歌曲/歌单/艺人、歌单详情、解析 **`play_url`**（具体字段与酷我/LX 脚本实现见 `music-service/index.js`）。
- **不持久化**用户歌单或 CDN URL；每次请求现拉现转。

---

## 5. 存储方式汇总

### 5.1 客户端（player）

| 数据 | 存储位置 | 说明 |
|------|----------|------|
| 当前播放列表 | **进程内双向链表** `g_music_head`，节点 **`Music_Node`**（`link.h`：`source`、`song_id`、`song_name`、`singer`、`play_url` 等） | 退出进程即失；翻页/搜歌会 `link_clear_list` 或插入片段 |
| 当前曲目标识 / 模式 / PID | **共享内存** `Shm_Data`（`shm.h`：`current_music`、`current_singer`、`current_song_id`、`current_mode`、`parent_pid`/`child_pid`/`grand_pid`） | 父子进程同步；**不含** `source`，在线取链依赖链表节点 |
| 分页搜歌上下文 | **`player_playlist_ctx_t`**（`player_types.h`，如 `keyword`、`current_page`、`total_pages`） | 内存，用于「热门」等续页 |
| 本地音频文件 | **文件系统**（如 SD 挂载目录、`SMART_SPEAKER_LOCAL_MUSIC_DIR`） | 仅路径与扫描结果进链表；不拷贝音频进 SHM |

### 5.2 服务端（C++）

| 数据 | 存储位置 | 说明 |
|------|----------|------|
| 无关键词 `list_music` 用曲库列表 | **内存向量** `g_list_music_cache`（`server.cpp`），首次扫盘后 shuffle，进程存活期间复用 | 非数据库 |
| 本地曲库文件元数据 | **磁盘**：`music_root`（如 `/var/www/html/music/`）目录遍历 | 与 Apache 静态 `music/` 可一致，供 HTTP 直链 |
| 在线 URL、搜索结果 | **不持久化** | Rust / Node 每次请求第三方 API |
| 业务数据库（用户等） | MySQL 等（与「取播放链」无强绑定） | 略 |

### 5.3 Rust `music-lib`

- 无本地歌单库；**API Key**、**API Base** 来自环境变量。
- 返回的 `play_url` 仅存在于 **当次 JSON 响应** 或 **调用方内存**。

---

## 6. 相关源码索引（便于跳转）

| 模块 | 路径 |
|------|------|
| Client TCP 与 JSON | `player/music_source/music_source_server.c` |
| Client 后端选择 | `player/music_source/music_source_manager.c` |
| Client 链表 | `player/net/link.c`、`link.h` |
| Client 共享内存 | `player/core/shm.h`、`shm.c` |
| Client 桥接填链 | `player/bridge/music_lib_bridge.c` |
| Client 歌单判断 / 异步 | `player/select_loop/select_text.c`、`music_server_async.c` |
| Server 命令分发与代理 | `smart-speaker-server/src/server.cpp` |
| Server → Node HTTP | `smart-speaker-server/src/music_service_client.cpp` |
| Server 远程列表（Rust） | `smart-speaker-server/src/music_remote_list.cpp` |
| Rust 取链 | `smart-speaker-server/music-lib/src/api.rs` 等 |
| Node 服务 | `smart-speaker-server/music-service/index.js` |

---

## 7. 与《三进程播放与分页》的关系

播放调度、EOF 翻页、`SIGUSR1` 等与 **链表 + `Shm_Data`** 的配合，见 **`docs/smart-speaker-client-player-三进程播放与分页状态机技术基线.md`**。本文侧重 **数据从哪来、经哪条 RPC、落在哪块内存/磁盘**。
