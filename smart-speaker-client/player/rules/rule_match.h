#ifndef __RULE_MATCH_H__
#define __RULE_MATCH_H__

typedef enum {
    RULE_CMD_NONE = 0,           // 无操作
    RULE_CMD_STOP,               // 停止播放
    RULE_CMD_PAUSE,              // 暂停播放
    RULE_CMD_RESUME,             // 继续播放
    RULE_CMD_NEXT,               // 下一首
    RULE_CMD_PREV,               // 上一首
    RULE_CMD_VOL_DOWN,           // 音量减小
    RULE_CMD_VOL_UP,             // 音量增大
    RULE_CMD_MODE_SINGLE,        // 单曲循环
    RULE_CMD_MODE_ORDER,        // 顺序播放
    RULE_CMD_PLAY_START,         // 开始播放
    RULE_CMD_PLAY_QUERY,         // 按文本搜歌播放
    RULE_CMD_SWITCH_OFFLINE,     // 切换到离线模式
    RULE_CMD_SWITCH_ONLINE,      // 切换到在线模式
    RULE_CMD_NOOP                // 无指令闲聊（如没事），播随机告别 wav
} rule_cmd_t;

typedef struct {
    int matched;
    rule_cmd_t cmd;
    const char *action_desc;
} rule_match_result_t;

int rule_match_text(const char *text, rule_match_result_t *result);
const char *rule_cmd_to_string(rule_cmd_t cmd);

#endif
