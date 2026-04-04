# player 离线在线切换与兜底策略

## 离线/在线切换

- 在线切离线：`player_switch_offline_mode()`
  - 停止播放
  - 卸载旧挂载点
  - 挂载 `/mnt/sdcard/`
  - 扫描本地曲库
  - 通知 ASR-KWS 切离线
- 离线切在线：`player_switch_online_mode()`
  - 停止播放
  - 卸载本地挂载点
  - 连接服务器
  - 拉取在线歌单并播放
  - 通知 ASR-KWS 切在线

## 离线未命中兜底

- 固定兜底音频资产：`./assets/tts/fallback_unmatched.wav`
- 入口函数：`tts_play_audio_file(const char *path)`
- 触发点：
  - `RULE_CMD_PLAY_QUERY` 离线未命中
  - `RULE_CMD_NONE` 离线未命中

## TTS 事件联动

- `main_tts` 收到 `IPC_CMD_PLAY_AUDIO_FILE`：
  - 成功播放：上报 `tts:start` 与 `tts:done`
  - body 为空、路径为空、路径不存在、命令执行失败：统一补发 `tts:done`
- `player/select_loop/select.c` 收到 `tts:start/tts:done` 后执行暂停/恢复逻辑，不引入第二播放入口。

## 本轮补强

- `player` 侧新增 TTS FIFO 发送前重试打开，避免启动时序导致 `g_tts_fd=-1` 时离线兜底失效。
