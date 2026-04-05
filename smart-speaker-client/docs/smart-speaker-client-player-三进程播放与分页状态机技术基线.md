# smart-speaker-client player 三进程播放与分页状态机技术基线

## 1. 目标与范围

本文档定义 `smart-speaker-client/player` 的当前技术基线，覆盖以下内容：

- 三进程播放模型（父/子/孙）职责边界与状态流转
- 在线曲库：`smart-speaker-server/music-lib` 聚合搜索；`list_music` 可返回 **`play_url`**（client 优先直链播放）
- 歌单数据结构与共享内存一致性模型
- 播放模式语义（顺序/随机/单曲）与翻页策略
- 关键故障面与回退路径

适用代码范围：

- `player/core/player.c`
- `player/net/link.c`, `player/net/link.h`
- `player/core/shm.h`
- `player/bridge/music_lib_bridge.c`, `player/bridge/music_lib_bridge.h`
- `player/core/player_gst.c`
- `player/core/main.c`
- `player/select_loop/select.c`
- `../smart-speaker-server/music-lib/`（Rust，服务端链接）
- `../smart-speaker-server/src/music_remote_list.cpp`

## 2. 核心架构

### 2.1 进程模型

- 父进程：命令/事件入口，维护主控制面（`select_run`、模式切换、翻页触发）
- 子进程：播放编排器，负责孙进程生命周期与“下一首”决策
- 孙进程：实际播放执行器，调用 `run_gst_player()` 进行音频播放

### 2.2 控制与数据通道

- 共享内存 `Shm_Data`：承载全局播放态（当前曲目唯一键、模式、PID）
- FIFO `GST_CMD_FIFO`：父/子向 GStreamer 下发 `quit`、`cycle pause`、`loadfile`
- ASR/TTS/Player 控制 FIFO：语音链路对播放器下发控制命令

## 3. 数据模型与一致性

## 3.1 链表节点（播放列表内存模型）

`Music_Node` 统一使用四元组标识一首歌：

- `song_name`
- `singer`
- `source`
- `song_id`

其中唯一定位键为：`source + song_id`。

## 3.2 共享内存（跨进程权威状态）

`Shm_Data` 当前播放字段：

- `current_music`
- `current_singer`
- `current_source`
- `current_song_id`
- `current_mode`
- `parent_pid`, `child_pid`, `grand_pid`

一致性原则：

- 子进程每轮调度前后以共享内存为准，同步本地 `current_song`
- 父进程执行“切歌/翻页”后先写 `Shm_Data`，再通过 `quit` 驱动孙进程退场重建

## 4. 在线曲库与 URL

## 4.1 列表接口（`list_music`）

- client 短时 TCP 请求 `list_music`，可选 **`keyword`**；泛意图（热门/听歌等）由服务端走本地曲库打乱分页。
- 精准关键词：服务端用 **`music-lib`** 搜索并在 JSON 中填 **`play_url`**（及 `source`/`song_id` 等）。

## 4.2 起播 URL 优先级（`resolve_play_url`）

1. 链表中节点已有 **`play_url`**（非空）
2. `music_lib_get_url_by_source_id` / `music_lib_get_url_for_music`（委托 `music_source_get_url`，无 `play_url` 时）
3. 在线兜底拼接、离线 `MUSIC_PATH` 等（见 `player.c`）

约束：

- 精准搜歌可 **插入当前曲之后**（`link_insert_node_after`），不整表清空；泛「热门」类仍可整表重建。

在线默认态：`g_current_online_mode = ONLINE_MODE_YES`。

## 5. 播放状态机与行为语义

## 5.1 播放状态

- `PLAY_STATE_STOP`
- `PLAY_STATE_PLAY`
- `PLAY_SUSPEND_NO`
- `PLAY_SUSPEND_YES`

关键语义：

- STOP 下执行继续播放：走 `player_start_play()` 直接恢复
- 暂停/继续：通过 `cycle pause` 对 GStreamer 切换

## 5.2 模式语义

- 顺序播放（`ORDER_PLAY`）：到页末返回 `ret=1`，触发翻页
- 随机播放（`RANDOM_PLAY`）：随机选歌，规避“立刻重复当前曲目”
- 单曲循环（`SINGLE_PLAY`）：
  - 自动续播时保持单曲
  - 手动“下一首”时 `force_advance=1`，仅该次强制前进，不改变模式

## 6. 分页状态机（关键词 + 页号）

`player_playlist_ctx_t`：

- `keyword`
- `current_page`
- `total_pages`
- `page_size`

规则：

- 新关键词：重置到 `page=1`
- 同关键词：保持当前页递进
- 到最后一页后再下一页：回卷到 `page=1`（循环页空间）
- 当页面返回空结果时，按可用总页数继续尝试直到成功或耗尽

触发入口：

- 语义命令播放：`player_search_and_play_keyword()`
- 手动下一首到页末：`player_next_song()` 内触发 `player_playlist_load_next_page()`
- 自然播完页末：子进程向父进程发 `SIGUSR1`，父进程 `player_handle_playlist_eof()` 触发翻页续播

## 7. 关键流程（端到端）

## 7.1 “我想听 XXX”

1. `select.c` 解析规则/意图，进入 `try_music_lib_play`
2. 泛意图：`player_search_and_play_keyword("热门")` 等；精准词：`player_search_insert_keyword_and_play()` 插入后播插入段第一首
3. `player_start_play()` 选择起播曲，写共享内存
4. fork 子进程；子进程循环 fork 孙进程
5. 孙进程 `run_gst_player(url)` 执行播放

## 7.2 “下一首”

1. `player_next_song()` 依据 `current_source/current_song_id` 选下一首
2. 若页末：翻页并取新页首曲
3. 写回共享内存
4. 若子进程存活，发 `quit` 促使孙进程退出并重建到新曲目

## 7.3 自然播完

1. 孙进程 EOS 退出
2. 子进程 `waitpid` 后按模式决定下一首
3. 页末时向父进程发 `SIGUSR1`
4. 父进程翻页后 `player_start_play()`

## 8. 稳定性与故障边界

- 服务端 `music-lib` / 网络不可用：`list_music` 无 `play_url` 或回退本地扫描，播放可能失败或仅本地曲
- URL 解析失败：当前曲目播放失败，孙进程退出码非 0
- FIFO 写失败：切歌/暂停/继续无法投递到 GStreamer
- 共享内存与链表短暂不一致：通过子进程轮询同步 `current_song` 缓解

## 8.1 三进程防重入约束（本轮补充）

- 当处于 `PLAY_STATE_PLAY + PLAY_SUSPEND_YES`（已暂停）时，`player_start_play()` 必须走恢复分支，不允许再次 fork 新子进程
- `STOP` 后需要清理共享内存中的 `child_pid/grand_pid`，避免后续命令误判“旧进程仍有效”
- 该约束用于避免“重复创建孙进程导致同曲并发播放/重复日志”的问题

## 9. 对后续 AI Coding 与论文写作的可复用结论

- 该播放器实现了“控制平面（父）/编排平面（子）/执行平面（孙）”三层解耦
- 曲目主键采用 `source + song_id`，避免仅靠歌名导致的歧义和串歌
- 分页策略从“列表内循环”升级为“关键词域内跨页连续播放”
- 手动行为与自动行为语义解耦（单曲模式下手动 next 仅一次强制前进）
- 共享内存作为跨进程权威态，FIFO 作为执行通道，形成弱耦合控制链
