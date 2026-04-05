# smart-speaker-client player 运行时调试与框架知识总结

## 1. 本次确认的系统框架

- 语音链路：`KWS -> ASR -> Player(select.c) -> 规则匹配 -> (命中执行 | 未命中LLM) -> TTS`
- 播放链路：`父进程(控制)` -> `子进程(编排)` -> `孙进程(GStreamer执行)`
- 歌单链路：`server list_music`（`music-lib` 在服务端）-> `链表含 play_url 或 source/song_id` -> `起播优先 play_url，否则按需取 URL`

## 2. 关键工程约束

- 规则命中与 LLM 互斥：命中规则后必须短路，不进入 LLM
- 泛听歌短语映射热门：`我想听歌/我想听音乐/想听` 等映射到 `热门`
- 精确搜歌保真：`纯音乐/轻音乐` 等保持原词搜索
- 服务端可在 `list_music` 时填好 `play_url`；client 无 `play_url` 时仍可按 `source/song_id` 解析（若链了库）

## 3. 本轮核心问题与根因

### 3.1 “疑似两个孙进程同时播放”

根因：暂停态下再次收到“开始播放”时，`player_start_play()` 重新 fork 了新子进程，而不是恢复已有暂停播放链路。

修复约束：

- `PLAY_STATE_PLAY + PLAY_SUSPEND_YES` 下，`player_start_play()` 改为调用 `player_continue_play()`，不再新建进程
- `player_stop_play()` 后清理共享内存中的 `child_pid/grand_pid`

### 3.2 “获取下载链接错误”高频出现

现象：个别 `source/song_id` 在上游返回 200 但无可用 URL，导致库侧报错。

当前策略：

- 仍保持“按 id 优先，按需取 URL”的架构
- 保留多级兜底（`source_id -> singer/song -> song -> legacy`）
- 通过运行时日志区分“URL解析失败”与“进程重复拉起导致重复解析”

## 4. 对后续开发/论文可复用的方法论

- 采用“状态权威在共享内存、动作执行经 FIFO”的控制平面/执行平面分离
- 使用 `source + song_id` 作为跨页、跨进程唯一主键
- 对语音系统优先做“意图分类（控制/搜歌/问答）”，再做执行分派
- 排障时优先以运行时证据验证：输入文本、规则命中、进程 PID、URL 解析路径
