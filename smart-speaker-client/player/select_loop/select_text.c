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
    const char *markers[] = {
        "我想听", "想听一首", "我要听一首", "想听", "我要听", "给我放", "给我来一首", "来一首", "来首",
        "放一首", "点一首", "唱一首", "点播", "播放一下", "放一下", "听一下", "播放", "听", NULL,
    };
    const char *p = NULL;
    int i = 0;
    if (text == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    out[0] = '\0';

    if (strstr(text, "纯音乐") != NULL || strstr(text, "轻音乐") != NULL) {
        if (strstr(text, "纯音乐") != NULL) snprintf(out, out_size, "纯音乐");
        else snprintf(out, out_size, "轻音乐");
        return 1;
    }
    if (strstr(text, "我想听歌") != NULL || strstr(text, "想听歌") != NULL ||
        strstr(text, "听歌") != NULL || strstr(text, "播放音乐") != NULL ||
        strstr(text, "我想听音乐") != NULL || strstr(text, "想听音乐") != NULL) {
        snprintf(out, out_size, "热门");
        return 1;
    }
    if (strcmp(text, "我想听") == 0 || strcmp(text, "想听") == 0 ||
        strcmp(text, "我想听。") == 0 || strcmp(text, "想听。") == 0) {
        snprintf(out, out_size, "热门");
        return 1;
    }

    while (markers[i] != NULL) {
        p = strstr(text, markers[i]);
        if (p != NULL) {
            p += strlen(markers[i]);
            break;
        }
        ++i;
    }

    if (p != NULL && p[0] != '\0') {
        snprintf(out, out_size, "%s", p);
        trim_text(out);
        strip_trailing_particles(out);
        if (strlen(out) >= 1) {
            return 1;
        }
    }

    if (!select_text_has_control_intent(text) && strstr(text, "的") != NULL && !de_music_query_denied(text)) {
        snprintf(out, out_size, "%s", text);
        trim_text(out);
        strip_trailing_particles(out);
        if (strlen(out) >= 2) {
            return 1;
        }
    }
    return 0;
}
