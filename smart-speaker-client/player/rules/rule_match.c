#include "rule_match.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "select_text.h"

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
    static const char *const k1[] = {"减", "降低", "调低", "放低", NULL};
    static const char *const k2[] = {"音量", "声音", NULL};
    static const char *const k3[] = {
        "太大", "有点大", "小点", "小一点", "轻一点", "再小点", "再轻点", "调小", "轻点", NULL
    };
    return (has_any(text, k1) && has_any(text, k2)) || (has_any(text, k2) && has_any(text, k3));
}

static int match_vol_up(const char *text)
{
    static const char *const k1[] = {"增大", "增加", "提高", "调高", "放大", NULL};
    static const char *const k2[] = {"音量", "声音", NULL};
    static const char *const k3[] = {
        "太小", "有点小", "大点", "大一点", "响一点", "再大点", "再响点", "调大", "响点", NULL
    };
    return (has_any(text, k1) && has_any(text, k2)) || (has_any(text, k2) && has_any(text, k3));
}

static void skip_ws_vol_sep(const char **pp)
{
    for (;;) {
        const unsigned char *s = (const unsigned char *)*pp;
        if (*s == '\0') {
            return;
        }
        if (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
            ++(*pp);
            continue;
        }
        if (*s == ',' || *s == '.' || *s == ':' || *s == ';') {
            ++(*pp);
            continue;
        }
        if (s[0] == 0xef && s[1] == 0xbc && (s[2] == 0x9a || s[2] == 0x8c)) {
            *pp += 3;
            continue;
        }
        if (s[0] == 0xe3 && s[1] == 0x80 && s[2] == 0x82) {
            *pp += 3;
            continue;
        }
        break;
    }
}

static int parse_volume_pct_number(const char *p, const char **end_out)
{
    static const char baifen[] = "\xe7\x99\xbe\xe5\x88\x86\xe4\xb9\x8b";
    long v = 0;
    int n = 0;

    skip_ws_vol_sep(&p);
    if (strncmp(p, baifen, sizeof(baifen) - 1) == 0) {
        p += sizeof(baifen) - 1;
        skip_ws_vol_sep(&p);
    }
    while (1) {
        unsigned char c = (unsigned char)*p;
        if (c >= '0' && c <= '9') {
            v = v * 10 + (long)(c - '0');
            ++n;
            ++p;
            if (v > 1000) {
                break;
            }
            continue;
        }
        if ((unsigned char)p[0] == 0xef && (unsigned char)p[1] == 0xbc &&
            (unsigned char)p[2] >= 0x90 && (unsigned char)p[2] <= 0x99) {
            v = v * 10 + (long)(((unsigned char)p[2]) - 0x90);
            n++;
            p += 3;
            continue;
        }
        break;
    }
    if (n == 0) {
        return -1;
    }
    if (v < 0) {
        v = 0;
    }
    if (v > 100) {
        v = 100;
    }
    skip_ws_vol_sep(&p);
    if (*p == '%') {
        ++p;
    } else if ((unsigned char)p[0] == 0xef && (unsigned char)p[1] == 0xbc && (unsigned char)p[2] == 0x85) {
        p += 3;
    }
    if (end_out != NULL) {
        *end_out = p;
    }
    return (int)v;
}

static const char *skip_ascii_ws_only(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        ++p;
    }
    return p;
}

/* 到 / 成 / 为（「音量设置为」） */
static const char *after_dao_cheng_wei(const char *p)
{
    static const char dao[] = "\xe5\x88\xb0";
    static const char cheng[] = "\xe6\x88\x90";
    static const char wei[] = "\xe4\xb8\xba";
    p = skip_ascii_ws_only(p);
    if (strncmp(p, dao, 3) == 0) {
        return p + 3;
    }
    if (strncmp(p, cheng, 3) == 0) {
        return p + 3;
    }
    if (strncmp(p, wei, 3) == 0) {
        return p + 3;
    }
    return NULL;
}

static void vol_set_try_suffix(const char *suff, const char **best_suff)
{
    if (suff == NULL) {
        return;
    }
    if (parse_volume_pct_number(suff, NULL) < 0) {
        return;
    }
    if (*best_suff == NULL || suff < *best_suff) {
        *best_suff = suff;
    }
}

static void copy_strip_ascii_spaces(const char *text, char *out, size_t out_sz)
{
    size_t o = 0;
    const char *s = text;
    if (text == NULL || out_sz == 0) {
        return;
    }
    while (*s != '\0' && o + 1 < out_sz) {
        if (*s == ' ') {
            ++s;
            continue;
        }
        out[o++] = *s++;
    }
    out[o] = '\0';
}

static const char *vol_set_value_start_in(const char *text)
{
    static const char *const pfx[] = {
        "帮我把音量设置到",
        "帮我把音量调到",
        "帮我把音量设置成",
        "帮我把音量调整成",
        "帮我把音量调整到",
        "把音量给我设置到",
        "把音量给我调到",
        "把音量给我设置成",
        "把音量给我调整成",
        "把音量给我调整到",
        "帮我把声音设置到",
        "帮我把声音调到",
        "帮我把声音设置成",
        "帮我把声音调整成",
        "帮我把声音调整到",
        "把音量设置到",
        "把声音设置到",
        "把音量设置成",
        "把声音设置成",
        "把音量调整到",
        "把声音调整到",
        "把音量调整成",
        "把声音调整成",
        "把音量调到",
        "把声音调到",
        "把音量开到",
        "把声音开到",
        "将音量设置到",
        "将音量调到",
        "将音量设置成",
        "将音量调整到",
        "将音量调整成",
        "将声音设置到",
        "将声音调到",
        "将声音设置成",
        "将声音调整到",
        "将声音调整成",
        "帮我把音量调至",
        "帮我把声音调至",
        "把音量调至",
        "把声音调至",
        "将音量调至",
        "将声音调至",
        "音量调至",
        "声音调至",
        "帮我把音量改为",
        "帮我把声音改为",
        "把音量改为",
        "把声音改为",
        "将音量改为",
        "将声音改为",
        "音量改为",
        "声音改为",
        "设置音量到",
        "设置声音到",
        "设置音量成",
        "设置声音成",
        "调节音量到",
        "调节声音到",
        "调整音量到",
        "调整声音到",
        "调整音量成",
        "调整声音成",
        "调音量到",
        "调声音到",
        "音量给我调到",
        "音量给我设置到",
        "音量给我设置成",
        "音量给我调整到",
        "声音给我调到",
        "声音给我设置到",
        "声音给我设置成",
        "声音给我调整到",
        "声音给我调整成",
        "音量设置到",
        "声音设置到",
        "音量设置成",
        "声音设置成",
        "音量调整到",
        "声音调整到",
        "音量调整成",
        "声音调整成",
        "音量调到",
        "声音调到",
        "音量开到",
        "声音开到",
        "音量调节到",
        "声音调节到",
        "音量设为",
        "声音设为",
        "音量设成",
        "声音设成",
        "音量调成",
        "声音调成",
        NULL
    };
    static const char *const flex_kw[] = {
        "音量设置",
        "声音设置",
        "音量调整",
        "声音调整",
        NULL
    };
    const char *best_suff = NULL;
    int i;
    const char *q;

    for (i = 0; pfx[i] != NULL; ++i) {
        q = text;
        while ((q = strstr(q, pfx[i])) != NULL) {
            vol_set_try_suffix(q + strlen(pfx[i]), &best_suff);
            ++q;
        }
    }
    for (i = 0; flex_kw[i] != NULL; ++i) {
        size_t L = strlen(flex_kw[i]);
        q = text;
        while ((q = strstr(q, flex_kw[i])) != NULL) {
            const char *s = after_dao_cheng_wei(q + L);
            if (s != NULL) {
                vol_set_try_suffix(s, &best_suff);
            }
            ++q;
        }
    }
    return best_suff;
}

static int match_vol_set_percent(const char *text, int *out_pct)
{
    char compact[384];
    const char *p;
    int v;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    p = vol_set_value_start_in(text);
    if (p == NULL) {
        copy_strip_ascii_spaces(text, compact, sizeof(compact));
        if (compact[0] != '\0') {
            p = vol_set_value_start_in(compact);
        }
    }
    if (p == NULL) {
        return 0;
    }
    v = parse_volume_pct_number(p, NULL);
    if (v < 0) {
        return 0;
    }
    *out_pct = v;
    return 1;
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

static int is_start_only_request(const char *text)
{
    char t[256];
    size_t len;
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
    return strcmp(t, "播放音乐") == 0 ||
           strcmp(t, "播放") == 0 ||
           strcmp(t, "开始播放") == 0 ||
           strcmp(t, "开始") == 0 ||
           strcmp(t, "放首歌") == 0 ||
           strcmp(t, "唱首歌") == 0;
}

static int match_play_query(const char *text)
{
    char q[256];
    if (has_control_words(text)) {
        return 0;
    }
    if (!select_text_extract_music_query_source(text, q, sizeof(q), NULL, 0)) {
        return 0;
    }
    if (q[0] == '\0' || select_text_is_playlist_query(q)) {
        return 0;
    }
    if (is_generic_query(q) && is_start_only_request(text)) {
        return 0;
    }
    return 1;
}

static int match_play_playlist(const char *text)
{
    char q[256];
    if (has_control_words(text)) {
        return 0;
    }
    if (!select_text_extract_music_query_source(text, q, sizeof(q), NULL, 0)) {
        return 0;
    }
    if (q[0] == '\0' || !select_text_is_playlist_query(q)) {
        return 0;
    }
    if (is_generic_query(q) && is_start_only_request(text)) {
        return 0;
    }
    return 1;
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
    {RULE_CMD_PLAY_PLAYLIST, "歌单检索", match_play_playlist},
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
    result->vol_set_target = -1;
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    {
        int pct = 0;
        if (match_vol_set_percent(text, &pct)) {
            result->matched = 1;
            result->cmd = RULE_CMD_VOL_SET;
            result->action_desc = "设置音量";
            result->vol_set_target = pct;
            return 0;
        }
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
    case RULE_CMD_VOL_SET: return "RULE_CMD_VOL_SET";
    case RULE_CMD_MODE_SINGLE: return "RULE_CMD_MODE_SINGLE";
    case RULE_CMD_MODE_ORDER: return "RULE_CMD_MODE_ORDER";
    case RULE_CMD_PLAY_PLAYLIST: return "RULE_CMD_PLAY_PLAYLIST";
    case RULE_CMD_PLAY_QUERY: return "RULE_CMD_PLAY_QUERY";
    case RULE_CMD_PLAY_START: return "RULE_CMD_PLAY_START";
    case RULE_CMD_SWITCH_OFFLINE: return "RULE_CMD_SWITCH_OFFLINE";
    case RULE_CMD_SWITCH_ONLINE: return "RULE_CMD_SWITCH_ONLINE";
    case RULE_CMD_NOOP: return "RULE_CMD_NOOP";
    default: return "RULE_CMD_NONE";
    }
}
