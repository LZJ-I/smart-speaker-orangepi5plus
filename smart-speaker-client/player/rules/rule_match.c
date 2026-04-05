#include "rule_match.h"
#include <string.h>

typedef int (*rule_match_fn)(const char *text);

typedef struct {
    rule_cmd_t cmd;
    const char *action_desc;
    rule_match_fn match_fn;
} rule_def_t;

static int has_any(const char *text, const char *const *keywords)
{
    int i = 0;
    while (keywords[i] != NULL) {
        if (strstr(text, keywords[i]) != NULL) {
            return 1;
        }
        ++i;
    }
    return 0;
}

static int has_all(const char *text, const char *const *keywords)
{
    int i = 0;
    while (keywords[i] != NULL) {
        if (strstr(text, keywords[i]) == NULL) {
            return 0;
        }
        ++i;
    }
    return 1;
}

static int is_trimmed_equal(const char *text, const char *target)
{
    const char *start = text;
    const char *end = text + strlen(text);
    size_t len = 0;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        ++start;
    }
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
        --end;
    }
    len = (size_t)(end - start);
    return strlen(target) == len && strncmp(start, target, len) == 0;
}

static void trim_ascii_blank(char *s)
{
    char *start = s;
    char *end = s + strlen(s);
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        ++start;
    }
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
        --end;
    }
    if (start != s) {
        memmove(s, start, (size_t)(end - start));
    }
    s[end - start] = '\0';
}

static void strip_tail_particles(char *s)
{
    const char *suffixes[] = {
        "。", "！", "？", "，", ".", "!", "?", ",", ";", "；",
        "吧", "呀", "啊", "呢",
        NULL
    };
    int changed = 1;
    while (changed) {
        int i = 0;
        changed = 0;
        while (suffixes[i] != NULL) {
            size_t len = strlen(s);
            size_t slen = strlen(suffixes[i]);
            if (len >= slen && strcmp(s + len - slen, suffixes[i]) == 0) {
                s[len - slen] = '\0';
                trim_ascii_blank(s);
                changed = 1;
                break;
            }
            ++i;
        }
    }
}

static int match_stop(const char *text)
{
    static const char *const k[] = {"停止播放", "结束", "退出", "关闭", "关掉", "停止", "我不想听了", "别放了", "不要放了", NULL};
    return has_any(text, k) || is_trimmed_equal(text, "停");
}

static int match_pause(const char *text)
{
    static const char *const k[] = {"暂停", "停一下", "等一下", "先停下", NULL};
    return has_any(text, k);
}

static int match_resume(const char *text)
{
    static const char *const k[] = {"继续播放", "继续", "接着播放", NULL};
    return has_any(text, k);
}

static int match_next(const char *text)
{
    static const char *const k[] = {"下一首", "换一首", "切歌", "切换歌曲", "下一曲", "下一首歌", NULL};
    return has_any(text, k);
}

static int match_prev(const char *text)
{
    static const char *const k[] = {"上一首", "上一曲", "上一首歌", "前一首", NULL};
    return has_any(text, k);
}

static int match_vol_down(const char *text)
{
    static const char *const k1[] = {"减", "降低", NULL};
    static const char *const k2[] = {"音量", "声音", NULL};
    static const char *const k3[] = {"太大", "有点大", "小点", "小一点", "调小", NULL};
    return (has_any(text, k1) && has_any(text, k2)) || (has_any(text, k2) && has_any(text, k3));
}

static int match_vol_up(const char *text)
{
    static const char *const k1[] = {"增大", "增加", "提高", NULL};
    static const char *const k2[] = {"音量", "声音", NULL};
    static const char *const k3[] = {"太小", "有点小", "大点", "大一点", "调大", NULL};
    return (has_any(text, k1) && has_any(text, k2)) || (has_any(text, k2) && has_any(text, k3));
}

static int match_mode_single(const char *text)
{
    static const char *const k[] = {"单曲循环", "循环播放", "单曲循环播放", NULL};
    return has_any(text, k);
}

static int match_mode_order(const char *text)
{
    static const char *const k[] = {"顺序播放", "列表顺序", "按顺序播放", NULL};
    return has_any(text, k);
}

static int match_switch_offline(const char *text)
{
    static const char *const k[] = {"离线模式", NULL};
    return has_any(text, k);
}

static int match_switch_online(const char *text)
{
    static const char *const k[] = {"在线模式", NULL};
    return has_any(text, k);
}

static int match_noop_dismiss(const char *text)
{
    char t[256];
    size_t len;
    static const char *const phrases[] = {
        "没事", "没事儿", "没有事", "没事了", "没别的事", "没啥事", "没什么事",
        "不要紧", "没关系", "不用了", "没事儿啦", "没事啦", "算了没事", NULL,
    };
    int i = 0;
    if (text == NULL) {
        return 0;
    }
    len = strlen(text);
    if (len == 0 || len >= sizeof(t)) {
        return 0;
    }
    memcpy(t, text, len + 1);
    trim_ascii_blank(t);
    strip_tail_particles(t);
    if (t[0] == '\0') {
        return 0;
    }
    while (phrases[i] != NULL) {
        if (strcmp(t, phrases[i]) == 0) {
            return 1;
        }
        ++i;
    }
    return 0;
}

static int match_play_start(const char *text)
{
    static const char *const k[] = {"开始播放", "播放音乐", "播放", "开始", "放首歌", "唱首歌", NULL};
    static const char *const must_all[] = {"首", "听听", NULL};
    static const char *const deny[] = {"结束", "停止", "暂停", NULL};
    if (has_any(text, deny)) {
        return 0;
    }
    return has_any(text, k) || has_all(text, must_all);
}

static int has_control_words(const char *text)
{
    static const char *const k[] = {
        "停止", "暂停", "继续", "下一首", "上一首", "音量", "声音",
        "单曲循环", "顺序播放", "在线模式", "离线模式", NULL
    };
    return has_any(text, k);
}

static int is_generic_query(const char *q)
{
    static const char *const generic[] = {"音乐", "歌", "歌曲", "一首歌", NULL};
    int i = 0;
    while (generic[i] != NULL) {
        if (strcmp(q, generic[i]) == 0) {
            return 1;
        }
        ++i;
    }
    return 0;
}

static int match_play_query(const char *text)
{
    static const char *const markers[] = {
        "我想听", "想听一首", "我要听一首", "想听", "我要听", "来一首", "来首", "给我来一首", "给我放",
        "播放一下", "放一下", "听一下", "播放", "放一首", "点一首", "唱一首", "点播", "听听", NULL,
    };
    int i = 0;
    if (has_control_words(text)) {
        return 0;
    }
    while (markers[i] != NULL) {
        const char *p = strstr(text, markers[i]);
        if (p != NULL) {
            char q[256] = {0};
            size_t n = 0;
            p += strlen(markers[i]);
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
                ++p;
            }
            n = strlen(p);
            while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\n' || p[n - 1] == '\r')) {
                --n;
            }
            if (n == 0 || n >= sizeof(q)) {
                return 0;
            }
            memcpy(q, p, n);
            q[n] = '\0';
            trim_ascii_blank(q);
            strip_tail_particles(q);
            if (q[0] == '\0') {
                return 0;
            }
            if (is_generic_query(q)) {
                return (strcmp(markers[i], "播放") == 0) ? 0 : 1;
            }
            return 1;
        }
        ++i;
    }
    return 0;
}

static const rule_def_t k_rules[] = {
    {RULE_CMD_NOOP, "无指令告别（随机短 wav）", match_noop_dismiss},
    {RULE_CMD_SWITCH_ONLINE, "切换到在线模式", match_switch_online},
    {RULE_CMD_SWITCH_OFFLINE, "切换到离线模式", match_switch_offline},
    {RULE_CMD_STOP, "停止播放", match_stop},
    {RULE_CMD_PAUSE, "暂停播放", match_pause},
    {RULE_CMD_RESUME, "继续播放", match_resume},
    {RULE_CMD_NEXT, "下一首", match_next},
    {RULE_CMD_PREV, "上一首", match_prev},
    {RULE_CMD_VOL_DOWN, "调低音量", match_vol_down},
    {RULE_CMD_VOL_UP, "调高音量", match_vol_up},
    {RULE_CMD_MODE_SINGLE, "单曲循环", match_mode_single},
    {RULE_CMD_MODE_ORDER, "顺序播放", match_mode_order},
    {RULE_CMD_PLAY_QUERY, "按文本搜歌", match_play_query},
    {RULE_CMD_PLAY_START, "开始播放", match_play_start},
};

int rule_match_text(const char *text, rule_match_result_t *result)
{
    size_t i = 0;
    if (result == NULL) {
        return -1;
    }
    result->matched = 0;
    result->cmd = RULE_CMD_NONE;
    result->action_desc = "未命中";
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    for (i = 0; i < sizeof(k_rules) / sizeof(k_rules[0]); ++i) {
        if (k_rules[i].match_fn(text)) {
            result->matched = 1;
            result->cmd = k_rules[i].cmd;
            result->action_desc = k_rules[i].action_desc;
            return 0;
        }
    }
    return 0;
}

const char *rule_cmd_to_string(rule_cmd_t cmd)
{
    switch (cmd) {
    case RULE_CMD_STOP: return "RULE_CMD_STOP";
    case RULE_CMD_PAUSE: return "RULE_CMD_PAUSE";
    case RULE_CMD_RESUME: return "RULE_CMD_RESUME";
    case RULE_CMD_NEXT: return "RULE_CMD_NEXT";
    case RULE_CMD_PREV: return "RULE_CMD_PREV";
    case RULE_CMD_VOL_DOWN: return "RULE_CMD_VOL_DOWN";
    case RULE_CMD_VOL_UP: return "RULE_CMD_VOL_UP";
    case RULE_CMD_MODE_SINGLE: return "RULE_CMD_MODE_SINGLE";
    case RULE_CMD_MODE_ORDER: return "RULE_CMD_MODE_ORDER";
    case RULE_CMD_PLAY_QUERY: return "RULE_CMD_PLAY_QUERY";
    case RULE_CMD_PLAY_START: return "RULE_CMD_PLAY_START";
    case RULE_CMD_SWITCH_OFFLINE: return "RULE_CMD_SWITCH_OFFLINE";
    case RULE_CMD_SWITCH_ONLINE: return "RULE_CMD_SWITCH_ONLINE";
    case RULE_CMD_NOOP: return "RULE_CMD_NOOP";
    default: return "RULE_CMD_NONE";
    }
}
