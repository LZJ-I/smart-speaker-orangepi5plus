#include "select_text.h"
#include <stdio.h>
#include <string.h>

void select_text_trim(char *s)
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

static void trim_text(char *s) { select_text_trim(s); }

typedef struct {
    const char *needle;
    const char *source;
} music_source_hint_t;

static const music_source_hint_t k_music_source_hints[] = {
    {"网易云音乐的", "wy"},
    {"网抑云音乐的", "wy"},
    {"QQ音乐的", "tx"},
    {"qq音乐的", "tx"},
    {"腾讯音乐的", "tx"},
    {"酷狗音乐的", "kg"},
    {"酷我音乐的", "kw"},
    {"咪咕音乐的", "mg"},
    {"网易云的", "wy"},
    {"网抑云的", "wy"},
    {"酷狗的", "kg"},
    {"酷我的", "kw"},
    {"咪咕的", "mg"},
    {"腾讯的", "tx"},
    {"网易云音乐", "wy"},
    {"网抑云音乐", "wy"},
    {"小枸音乐", "kg"},
    {"小蜗音乐", "kw"},
    {"小蜜音乐", "mg"},
    {"小秋音乐", "tx"},
    {"小芸音乐", "wy"},
    {"QQ音乐", "tx"},
    {"qq音乐", "tx"},
    {"腾讯音乐", "tx"},
    {"酷狗音乐", "kg"},
    {"酷我音乐", "kw"},
    {"咪咕音乐", "mg"},
    {"网易云", "wy"},
    {"网抑云", "wy"},
    {"酷狗", "kg"},
    {"酷我", "kw"},
    {"咪咕", "mg"},
    {"腾讯", "tx"},
    {"小枸", "kg"},
    {"小蜗", "kw"},
    {"小蜜", "mg"},
    {"小秋", "tx"},
    {"小芸", "wy"},
    {"QQ", "tx"},
    {"qq", "tx"},
    {NULL, NULL}
};

static const char *k_source_left_connectors[] = {
    "的", "在", "从", "用", "到", "上", "里", "中", "和", "与", "跟", NULL
};

static const char *k_source_right_connectors[] = {
    "的", "版", "里", "上", "中", NULL
};

static int is_ascii_blank_char(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int source_flag_bit(const char *source)
{
    if (source == NULL || source[0] == '\0') return 0;
    if (strcmp(source, "tx") == 0) return 1 << 0;
    if (strcmp(source, "wy") == 0) return 1 << 1;
    if (strcmp(source, "kw") == 0) return 1 << 2;
    if (strcmp(source, "kg") == 0) return 1 << 3;
    if (strcmp(source, "mg") == 0) return 1 << 4;
    return 0;
}

static void select_text_set_source_from_mask(int mask, char *source, size_t source_size)
{
    if (source == NULL || source_size == 0) {
        return;
    }
    source[0] = '\0';
    if (mask == 0) {
        return;
    }
    if ((mask & (mask - 1)) != 0) {
        snprintf(source, source_size, "all");
        return;
    }
    if (mask & (1 << 0)) snprintf(source, source_size, "tx");
    else if (mask & (1 << 1)) snprintf(source, source_size, "wy");
    else if (mask & (1 << 2)) snprintf(source, source_size, "kw");
    else if (mask & (1 << 3)) snprintf(source, source_size, "kg");
    else if (mask & (1 << 4)) snprintf(source, source_size, "mg");
}

static void collapse_ascii_spaces(char *s)
{
    size_t src = 0;
    size_t dst = 0;
    int prev_blank = 1;
    if (s == NULL) {
        return;
    }
    while (s[src] != '\0') {
        if (is_ascii_blank_char(s[src])) {
            if (!prev_blank) {
                s[dst++] = ' ';
                prev_blank = 1;
            }
        } else {
            s[dst++] = s[src];
            prev_blank = 0;
        }
        ++src;
    }
    if (dst > 0 && s[dst - 1] == ' ') {
        --dst;
    }
    s[dst] = '\0';
    trim_text(s);
}

static void try_strip_left_connector(char *s, size_t *start)
{
    int i = 0;
    while (*start > 0 && is_ascii_blank_char(s[*start - 1])) {
        --(*start);
    }
    while (k_source_left_connectors[i] != NULL) {
        size_t len = strlen(k_source_left_connectors[i]);
        if (*start >= len &&
            strncmp(s + *start - len, k_source_left_connectors[i], len) == 0) {
            *start -= len;
            while (*start > 0 && is_ascii_blank_char(s[*start - 1])) {
                --(*start);
            }
            break;
        }
        ++i;
    }
}

static void try_strip_right_connector(char *s, size_t *end)
{
    int i = 0;
    while (s[*end] != '\0' && is_ascii_blank_char(s[*end])) {
        ++(*end);
    }
    while (k_source_right_connectors[i] != NULL) {
        size_t len = strlen(k_source_right_connectors[i]);
        if (strncmp(s + *end, k_source_right_connectors[i], len) == 0) {
            *end += len;
            while (s[*end] != '\0' && is_ascii_blank_char(s[*end])) {
                ++(*end);
            }
            break;
        }
        ++i;
    }
}

static int strip_music_source_hints_inplace(char *s, char *source, size_t source_size)
{
    int mask = 0;
    if (s == NULL) {
        if (source != NULL && source_size > 0) {
            source[0] = '\0';
        }
        return 0;
    }
    for (;;) {
        size_t best_pos = (size_t)-1;
        size_t best_len = 0;
        const char *best_source = NULL;
        int i = 0;
        while (k_music_source_hints[i].needle != NULL) {
            const char *p = strstr(s, k_music_source_hints[i].needle);
            if (p != NULL) {
                size_t pos = (size_t)(p - s);
                size_t len = strlen(k_music_source_hints[i].needle);
                if (best_source == NULL || len > best_len || (len == best_len && pos < best_pos)) {
                    best_pos = pos;
                    best_len = len;
                    best_source = k_music_source_hints[i].source;
                }
            }
            ++i;
        }
        if (best_source == NULL) {
            break;
        }
        {
            size_t start = best_pos;
            size_t end = best_pos + best_len;
            size_t tail_len;
            mask |= source_flag_bit(best_source);
            try_strip_left_connector(s, &start);
            try_strip_right_connector(s, &end);
            tail_len = strlen(s + end);
            memmove(s + start, s + end, tail_len + 1);
        }
        collapse_ascii_spaces(s);
    }
    select_text_set_source_from_mask(mask, source, source_size);
    return mask != 0;
}

static void strip_trailing_particles(char *s)
{
    const char *suffixes[] = {
        "的歌", "歌曲", "这首歌", "这首", "歌", "吧", "呀", "啊", "呢",
        "。", "！", "？", "，", ".", "!", "?", ",", ";", "；", NULL
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
                trim_text(s);
                changed = 1;
                break;
            }
            ++i;
        }
    }
}

static void strip_leading_particles(char *s)
{
    const char *prefixes[] = {
        "的", "吧", "呀", "啊", "呢", "请", NULL
    };
    int changed = 1;
    while (changed) {
        int i = 0;
        changed = 0;
        while (prefixes[i] != NULL) {
            size_t len = strlen(prefixes[i]);
            if (strncmp(s, prefixes[i], len) == 0) {
                memmove(s, s + len, strlen(s + len) + 1);
                trim_text(s);
                changed = 1;
                break;
            }
            ++i;
        }
    }
}

int select_text_has_control_intent(const char *text)
{
    static const char *keywords[] = {
        "停止", "暂停", "继续", "下一首", "上一首", "音量", "声音",
        "单曲循环", "顺序播放", "在线模式", "离线模式", NULL
    };
    int i = 0;
    while (keywords[i] != NULL) {
        if (strstr(text, keywords[i]) != NULL) {
            return 1;
        }
        ++i;
    }
    return 0;
}

int select_text_is_hot_generic_query(const char *query)
{
    static const char *generic[] = {
        "音乐", "歌", "歌曲", "一首歌", "来首歌", "听歌", "热门歌曲", "热门", NULL
    };
    int i = 0;
    if (query == NULL || query[0] == '\0') {
        return 0;
    }
    if (strcmp(query, "纯音乐") == 0 || strcmp(query, "轻音乐") == 0) {
        return 0;
    }
    while (generic[i] != NULL) {
        if (strcmp(query, generic[i]) == 0) {
            return 1;
        }
        ++i;
    }
    return 0;
}

int select_text_is_playlist_query(const char *query)
{
    if (query == NULL || query[0] == '\0') {
        return 0;
    }
    return strstr(query, "歌单") != NULL;
}

void select_text_normalize_playlist_keyword(const char *query, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (query == NULL || query[0] == '\0') {
        return;
    }
    snprintf(out, out_size, "%s", query);
    select_text_trim(out);
    if (strcmp(out, "什么歌单") == 0 || strcmp(out, "歌单") == 0 ||
        strcmp(out, "热门歌单") == 0 || strcmp(out, "推荐歌单") == 0) {
        out[0] = '\0';
        return;
    }
    if (strlen(out) >= strlen("的歌单") &&
        strcmp(out + strlen(out) - strlen("的歌单"), "的歌单") == 0) {
        out[strlen(out) - strlen("的歌单")] = '\0';
    } else if (strlen(out) >= strlen("歌单") &&
               strcmp(out + strlen(out) - strlen("歌单"), "歌单") == 0) {
        out[strlen(out) - strlen("歌单")] = '\0';
    }
    select_text_trim(out);
}

static int de_music_query_denied(const char *text)
{
    static const char *deny_prefix[] = {
        "今天", "现在", "什么", "怎么", "为什么", "请问", "谁是", "哪一", "哪个", NULL,
    };
    int i = 0;
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    while (deny_prefix[i] != NULL) {
        size_t n = strlen(deny_prefix[i]);
        if (strncmp(text, deny_prefix[i], n) == 0) {
            return 1;
        }
        ++i;
    }
    return 0;
}

int select_text_extract_music_query(const char *text, char *out, size_t out_size)
{
    return select_text_extract_music_query_source(text, out, out_size, NULL, 0);
}

int select_text_is_incomplete_music_query(const char *text)
{
    const char *markers[] = {
        "我想听", "想听一首", "我要听一首", "想听", "我要听", "给我放", "给我来一首", "来一首", "来首",
        "放一首", "点一首", "唱一首", "点播", "播放一下", "放一下", "听一下", "播放", "听", NULL,
    };
    const char *p = NULL;
    int i = 0;
    char normalized[512];
    char remain[256];

    if (text == NULL || text[0] == '\0') {
        return 0;
    }

    snprintf(normalized, sizeof(normalized), "%s", text);
    trim_text(normalized);
    strip_music_source_hints_inplace(normalized, NULL, 0);
    trim_text(normalized);

    if (select_text_has_control_intent(normalized)) {
        return 0;
    }
    if (strstr(normalized, "我想听歌") != NULL || strstr(normalized, "想听歌") != NULL ||
        strstr(normalized, "听歌") != NULL || strstr(normalized, "播放音乐") != NULL ||
        strstr(normalized, "我想听音乐") != NULL || strstr(normalized, "想听音乐") != NULL) {
        return 0;
    }

    while (markers[i] != NULL) {
        p = strstr(normalized, markers[i]);
        if (p != NULL) {
            p += strlen(markers[i]);
            snprintf(remain, sizeof(remain), "%s", p);
            trim_text(remain);
            strip_leading_particles(remain);
            strip_trailing_particles(remain);
            return remain[0] == '\0';
        }
        ++i;
    }

    return 0;
}

int select_text_extract_music_query_source(const char *text, char *out, size_t out_size,
                                          char *source, size_t source_size)
{
    const char *markers[] = {
        "我想听", "想听一首", "我要听一首", "想听", "我要听", "给我放", "给我来一首", "来一首", "来首",
        "放一首", "点一首", "唱一首", "点播", "播放一下", "放一下", "听一下", "播放", "听", NULL,
    };
    const char *p = NULL;
    int i = 0;
    char normalized[512];
    if (text == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    if (source != NULL && source_size > 0) {
        source[0] = '\0';
    }
    snprintf(normalized, sizeof(normalized), "%s", text);
    trim_text(normalized);
    strip_music_source_hints_inplace(normalized, source, source_size);
    trim_text(normalized);

    if (strstr(normalized, "纯音乐") != NULL || strstr(normalized, "轻音乐") != NULL) {
        if (strstr(normalized, "纯音乐") != NULL) snprintf(out, out_size, "纯音乐");
        else snprintf(out, out_size, "轻音乐");
        return 1;
    }
    if (strstr(normalized, "我想听歌") != NULL || strstr(normalized, "想听歌") != NULL ||
        strstr(normalized, "听歌") != NULL || strstr(normalized, "播放音乐") != NULL ||
        strstr(normalized, "我想听音乐") != NULL || strstr(normalized, "想听音乐") != NULL) {
        snprintf(out, out_size, "热门");
        return 1;
    }
    if (strcmp(normalized, "我想听") == 0 || strcmp(normalized, "想听") == 0 ||
        strcmp(normalized, "我想听。") == 0 || strcmp(normalized, "想听。") == 0) {
        snprintf(out, out_size, "热门");
        return 1;
    }

    while (markers[i] != NULL) {
        p = strstr(normalized, markers[i]);
        if (p != NULL) {
            p += strlen(markers[i]);
            break;
        }
        ++i;
    }

    if (p != NULL && p[0] != '\0') {
        snprintf(out, out_size, "%s", p);
        trim_text(out);
        strip_leading_particles(out);
        strip_trailing_particles(out);
        if (strlen(out) >= 1) {
            return 1;
        }
    }

    if (!select_text_has_control_intent(normalized) &&
        strstr(normalized, "的") != NULL &&
        !de_music_query_denied(normalized)) {
        snprintf(out, out_size, "%s", normalized);
        trim_text(out);
        strip_leading_particles(out);
        strip_trailing_particles(out);
        if (strlen(out) >= 2) {
            return 1;
        }
    }
    return 0;
}
