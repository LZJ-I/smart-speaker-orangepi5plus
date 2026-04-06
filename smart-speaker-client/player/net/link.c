#define LOG_LEVEL 4
#include "debug_log.h"
#include "link.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <json-c/json.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include "music_source.h"
#include "music_source_server.h"
#include "player.h"
#include "runtime_config.h"
#include "shm.h"

#define TAG "LINK"

Music_Node* g_music_head = NULL;
static unsigned int g_playlist_version = 1;

/* main 可能 chdir 到 smart-speaker-client，勿用 ../data（会写到仓库外） */
#define LINK_DEBUG_DEFAULT_PATH "data/player/music_link_debug.txt"

static int link_debug_trace_enabled(void)
{
    return player_runtime_music_link_debug() != 0;
}

static const char *link_debug_output_path(void)
{
    const char *p = player_runtime_music_link_debug_path();
    if (p != NULL && p[0] != '\0') {
        return p;
    }
    return LINK_DEBUG_DEFAULT_PATH;
}

static void link_debug_mkdir_p(char *path)
{
    size_t i;

    if (path == NULL || path[0] == '\0') {
        return;
    }
    for (i = 1; path[i] != '\0'; i++) {
        if (path[i] != '/') {
            continue;
        }
        path[i] = '\0';
        if (path[0] != '\0') {
            mkdir(path, 0755);
        }
        path[i] = '/';
    }
}

static void link_debug_ensure_parent_dir(const char *path)
{
    char buf[512];
    const char *slash;
    size_t len;

    if (path == NULL || path[0] == '\0') {
        return;
    }
    snprintf(buf, sizeof(buf), "%s", path);
    slash = strrchr(buf, '/');
    if (slash == NULL || slash == buf) {
        return;
    }
    len = (size_t)(slash - buf);
    if (len >= sizeof(buf)) {
        return;
    }
    buf[len] = '\0';
    if (buf[0] != '\0') {
        link_debug_mkdir_p(buf);
        mkdir(buf, 0755);
    }
}

static void link_mark_playlist_changed(void)
{
    if (g_playlist_version == UINT_MAX) {
        g_playlist_version = 1;
        return;
    }
    g_playlist_version++;
}

static void link_debug_log_abs_once(const char *path)
{
    static int logged;
    char absbuf[PATH_MAX];

    if (logged || path == NULL) {
        return;
    }
    if (realpath(path, absbuf) != NULL) {
        LOGI(TAG, "music_link_debug 绝对路径: %s", absbuf);
        logged = 1;
    }
}

void link_debug_dump_list(void)
{
    const char *path;
    FILE *fp;
    Music_Node *n;
    struct timespec ts;
    int idx;
    const char *sid;
    const char *src;
    const char *sn;
    const char *sg;
    size_t url_len;

    if (!link_debug_trace_enabled()) {
        return;
    }
    path = link_debug_output_path();
    link_debug_ensure_parent_dir(path);
    fp = fopen(path, "w");
    if (fp == NULL) {
        LOGW(TAG, "music_link_debug 无法写入 %s: %s", path, strerror(errno));
        return;
    }
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        fprintf(fp, "# ts=%lld.%03ld pid=%d\n", (long long)ts.tv_sec, ts.tv_nsec / 1000000L, (int)getpid());
    } else {
        fprintf(fp, "# pid=%d\n", (int)getpid());
    }
    idx = 0;
    for (n = (g_music_head != NULL) ? g_music_head->next : NULL; n != NULL; n = n->next) {
        sid = (n->song_id[0] != '\0') ? n->song_id : n->id;
        src = (n->source[0] != '\0') ? n->source : "-";
        sn = (n->song_name[0] != '\0') ? n->song_name : n->title;
        sg = (n->singer[0] != '\0') ? n->singer : n->subtitle;
        if (sid == NULL) {
            sid = "";
        }
        if (sn == NULL) {
            sn = "";
        }
        if (sg == NULL) {
            sg = "";
        }
        fprintf(fp, "%d\t%s\t%s\t%s\t%s", idx++, src, sid, sg, sn);
        url_len = strlen(n->play_url);
        if (url_len == 0) {
            fprintf(fp, "\t0\t\n");
        } else {
            fprintf(fp, "\t1\t%.*s\n", (int)(url_len > 120 ? 120 : (int)url_len), n->play_url);
        }
    }
    fclose(fp);
    link_debug_log_abs_once(path);
}

static void trim_trailing_crlf(char *s, size_t cap)
{
    if (s == NULL || cap == 0) return;
    s[cap - 1] = '\0';
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
}

static void safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) return;
    dst[0] = '\0';
    if (src == NULL) return;
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
    trim_trailing_crlf(dst, dst_size);
}

static void copy_node_value(Music_Node *dst, const Music_Node *src)
{
    if (dst == NULL || src == NULL) return;
    memset(dst, 0, sizeof(*dst));
    safe_copy(dst->title, sizeof(dst->title), src->title);
    safe_copy(dst->subtitle, sizeof(dst->subtitle), src->subtitle);
    safe_copy(dst->id, sizeof(dst->id), src->id);
    safe_copy(dst->song_name, sizeof(dst->song_name), src->song_name);
    safe_copy(dst->singer, sizeof(dst->singer), src->singer);
    safe_copy(dst->source, sizeof(dst->source), src->source);
    safe_copy(dst->song_id, sizeof(dst->song_id), src->song_id);
    safe_copy(dst->play_url, sizeof(dst->play_url), src->play_url);
}

static int node_matches_identity(const Music_Node *node, const char *source, const char *song_id)
{
    const char *node_id;

    if (node == NULL || song_id == NULL || song_id[0] == '\0') {
        return 0;
    }
    node_id = node->id[0] != '\0' ? node->id : node->song_id;
    if (strcmp(node_id, song_id) != 0) {
        return 0;
    }
    if (source != NULL && source[0] != '\0' &&
        node->source[0] != '\0' && strcmp(node->source, source) != 0) {
        return 0;
    }
    return 1;
}

static void format_display_name(const Music_Node *node, char *buf, size_t buf_size)
{
    const char *title;
    const char *subtitle;
    if (buf == NULL || buf_size == 0) return;
    buf[0] = '\0';
    if (node == NULL) return;
    title = (node->title[0] != '\0') ? node->title : node->song_name;
    subtitle = (node->subtitle[0] != '\0') ? node->subtitle : node->singer;
    if (subtitle[0] != '\0') {
        snprintf(buf, buf_size, "%s/%s", subtitle, title);
    } else {
        snprintf(buf, buf_size, "%s", title);
    }
    buf[buf_size - 1] = '\0';
}

static Music_Node *find_by_identity(const char *source, const char *song_id)
{
    Shm_Data s;
    const char *src_eff;
    const char *prefer_singer;
    const char *nid;
    Music_Node *node;
    Music_Node *fallback;
    int need_source_match;

    if (song_id == NULL || song_id[0] == '\0') {
        return NULL;
    }
    shm_get(&s);
    src_eff = source;
    if (src_eff == NULL || src_eff[0] == '\0') {
        if (s.current_source[0] != '\0') {
            src_eff = s.current_source;
        }
    }
    prefer_singer = (s.current_singer[0] != '\0') ? s.current_singer : NULL;
    need_source_match = (src_eff != NULL && src_eff[0] != '\0');
    node = (g_music_head != NULL) ? g_music_head->next : NULL;
    fallback = NULL;
    while (node != NULL) {
        nid = node->id[0] != '\0' ? node->id : node->song_id;
        if (strcmp(nid, song_id) != 0) {
            node = node->next;
            continue;
        }
        if (need_source_match && strcmp(node->source, src_eff) != 0) {
            node = node->next;
            continue;
        }
        if (prefer_singer != NULL && prefer_singer[0] != '\0') {
            if (strcmp(node->singer, prefer_singer) == 0) {
                return node;
            }
            if (fallback == NULL) {
                fallback = node;
            }
        } else {
            return node;
        }
        node = node->next;
    }
    return fallback;
}

int link_init()
{
    g_music_head = (Music_Node*)malloc(sizeof(Music_Node));
    if (g_music_head == NULL) {
        LOGE(TAG, "分配链表头节点内存失败");
        return -1;
    }
    memset(g_music_head, 0, sizeof(Music_Node));
    g_playlist_version = 1;
    srand((unsigned int)(time(NULL) ^ getpid()));
    if (link_debug_trace_enabled()) {
        link_debug_dump_list();
    }
    return 0;
}

Music_Node *link_anchor_for_insert(void)
{
    Shm_Data s;
    Music_Node *n;
    if (g_music_head == NULL) {
        return NULL;
    }
    shm_get(&s);
    if (s.current_song_id[0] == '\0') {
        return g_music_head;
    }
    n = find_by_identity(NULL, s.current_song_id);
    return (n != NULL) ? n : g_music_head;
}

int link_insert_node_after_meta(Music_Node *anchor, const char *source, const char *id,
                                const char *title, const char *subtitle, const char *play_url)
{
    Music_Node *node;
    const char *safe_title;
    const char *safe_subtitle;
    const char *safe_id;
    if (g_music_head == NULL || anchor == NULL || title == NULL || title[0] == '\0') {
        return -1;
    }
    safe_title = title;
    safe_subtitle = (subtitle != NULL) ? subtitle : "";
    safe_id = (id != NULL && id[0] != '\0') ? id : safe_title;
    node = (Music_Node *)malloc(sizeof(Music_Node));
    if (node == NULL) {
        LOGE(TAG, "分配歌曲节点内存失败");
        return -1;
    }
    memset(node, 0, sizeof(Music_Node));
    safe_copy(node->title, sizeof(node->title), safe_title);
    safe_copy(node->subtitle, sizeof(node->subtitle), safe_subtitle);
    safe_copy(node->id, sizeof(node->id), safe_id);
    safe_copy(node->song_name, sizeof(node->song_name), safe_title);
    safe_copy(node->singer, sizeof(node->singer), safe_subtitle);
    safe_copy(node->source, sizeof(node->source), source != NULL ? source : "");
    safe_copy(node->song_id, sizeof(node->song_id), safe_id);
    if (node->source[0] == '\0') {
        safe_copy(node->source, sizeof(node->source), "local");
    }
    if (play_url != NULL && play_url[0] != '\0') {
        safe_copy(node->play_url, sizeof(node->play_url), play_url);
    }
    node->next = anchor->next;
    node->prev = anchor;
    if (anchor->next != NULL) {
        anchor->next->prev = node;
    }
    anchor->next = node;
    link_mark_playlist_changed();
    link_debug_dump_list();
    return 0;
}

int link_insert_node_after(Music_Node *anchor, const char *source, const char *id, const char *artist,
                           const char *name, const char *play_url)
{
    return link_insert_node_after_meta(anchor, source, id, name, artist, play_url);
}

int link_add_music_meta(const char *source, const char *id, const char *title, const char *subtitle,
                        const char *play_url)
{
    Music_Node *tail;
    const char *pu = (play_url != NULL && play_url[0] != '\0') ? play_url : NULL;
    if (g_music_head == NULL || title == NULL || title[0] == '\0') {
        return -1;
    }
    tail = g_music_head;
    while (tail->next != NULL) {
        tail = tail->next;
    }
    return link_insert_node_after_meta(tail, source, id, title, subtitle, pu);
}

int link_add_music_lib(const char *source, const char *id, const char *artist, const char *name,
                       const char *play_url)
{
    return link_add_music_meta(source, id, name, artist, play_url);
}

int link_get_source_id(const char *song_name, const char *singer, char *source_buf, size_t source_size, char *id_buf, size_t id_size)
{
    Music_Node *node = NULL;
    if (song_name == NULL || source_buf == NULL || id_buf == NULL || source_size == 0 || id_size == 0) {
        return -1;
    }
    source_buf[0] = '\0';
    id_buf[0] = '\0';
    node = (g_music_head != NULL) ? g_music_head->next : NULL;
    while (node != NULL) {
        const char *title = (node->title[0] != '\0') ? node->title : node->song_name;
        const char *subtitle = (node->subtitle[0] != '\0') ? node->subtitle : node->singer;
        int name_match = (strcmp(title, song_name) == 0 || strcmp(node->song_name, song_name) == 0);
        int singer_match = (singer == NULL || singer[0] == '\0' ||
                            strcmp(subtitle, singer) == 0 ||
                            strcmp(node->singer, singer) == 0);
        if (name_match && singer_match) {
            safe_copy(source_buf, source_size, node->source);
            safe_copy(id_buf, id_size, node->id[0] != '\0' ? node->id : node->song_id);
            return (source_buf[0] != '\0' && id_buf[0] != '\0') ? 0 : -1;
        }
        node = node->next;
    }
    return -1;
}

int link_get_music_by_source_id(const char *source, const char *id, Music_Node *out_node)
{
    Music_Node *node = NULL;
    if (id == NULL || out_node == NULL || id[0] == '\0') {
        return -1;
    }
    node = find_by_identity(source, id);
    if (node == NULL) return -1;
    copy_node_value(out_node, node);
    return 0;
}

int link_get_first_music(Music_Node *out_node)
{
    if (out_node == NULL || g_music_head == NULL || g_music_head->next == NULL) {
        return -1;
    }
    copy_node_value(out_node, g_music_head->next);
    return 0;
}

static int link_add(const char *music_name)
{
    char normalized[MUSIC_MAX_NAME];
    if (music_name == NULL || music_name[0] == '\0') return -1;
    safe_copy(normalized, sizeof(normalized), music_name);
    for (int i = 0; normalized[i] != '\0'; ) {
        if (normalized[i] == '\\' && normalized[i + 1] == ' ') {
            memmove(&normalized[i], &normalized[i + 1], strlen(&normalized[i + 1]) + 1);
        } else {
            i++;
        }
    }
    return link_add_music_meta("local", normalized, normalized, "", NULL);
}

int Parse_music_name(char *buf)
{
    struct json_object *obj = json_tokener_parse(buf);
    if (obj == NULL) {
        LOGE(TAG, "JSON解析失败");
        return -1;
    }
    struct json_object *cmd = json_object_object_get(obj, "cmd");
    if (cmd == NULL || strcmp(json_object_get_string(cmd), "reply_music") != 0) {
        LOGE(TAG, "解析服务器返回的歌曲列表失败：命令不匹配");
        json_object_put(obj);
        return -1;
    }
    struct json_object *music = json_object_object_get(obj, "music");
    if (music == NULL) {
        LOGE(TAG, "JSON对象中未找到'music'字段");
        json_object_put(obj);
        return -1;
    }
    for (int i = 0; i < json_object_array_length(music); i++) {
        struct json_object *music_item = json_object_array_get_idx(music, i);
        MusicSourceItem it;
        if (music_item == NULL) {
            continue;
        }
        if (json_object_is_type(music_item, json_type_string)) {
            link_add(json_object_get_string(music_item));
            continue;
        }
        if (!json_object_is_type(music_item, json_type_object)) {
            continue;
        }
        memset(&it, 0, sizeof(it));
        if (music_source_server_parse_music_item(music_item, &it) != 0) {
            continue;
        }
        if (link_add_music_meta(it.source,
                                it.id[0] != '\0' ? it.id : it.song_id,
                                it.title[0] != '\0' ? it.title : it.song_name,
                                it.subtitle[0] != '\0' ? it.subtitle : it.singer,
                                (it.play_url[0] != '\0') ? it.play_url : NULL) != 0) {
            json_object_put(obj);
            return -1;
        }
    }
    json_object_put(obj);
    return 0;
}

int link_find_by_display_name(const char *display, Music_Node *out)
{
    Music_Node *current;
    char disp[MUSIC_MAX_NAME + SINGER_MAX_NAME + 2];

    if (display == NULL || display[0] == '\0' || out == NULL || g_music_head == NULL) {
        return -1;
    }
    for (current = g_music_head->next; current != NULL; current = current->next) {
        format_display_name(current, disp, sizeof(disp));
        if (strcmp(disp, display) == 0) {
            copy_node_value(out, current);
            return 0;
        }
    }
    return -1;
}

int link_get_music_at(int index, Music_Node *out)
{
    Music_Node *current;
    int current_index;

    if (index < 0 || out == NULL || g_music_head == NULL) {
        return -1;
    }
    current = g_music_head->next;
    current_index = 0;
    while (current != NULL) {
        if (current_index == index) {
            copy_node_value(out, current);
            return 0;
        }
        current = current->next;
        current_index++;
    }
    return -1;
}

int link_get_current_index(const char *source, const char *song_id)
{
    Music_Node *current;
    int current_index;

    if (g_music_head == NULL || song_id == NULL || song_id[0] == '\0') {
        return -1;
    }
    current = g_music_head->next;
    current_index = 0;
    while (current != NULL) {
        if (node_matches_identity(current, source, song_id)) {
            return current_index;
        }
        current = current->next;
        current_index++;
    }
    return -1;
}

unsigned int link_get_playlist_version(void)
{
    return g_playlist_version;
}

void link_traverse_list(char** music_list)
{
    Music_Node* current = (g_music_head != NULL) ? g_music_head->next : NULL;
    int index = 0;
    while (current != NULL && index < GET_MAX_MUSIC) {
        char display[MUSIC_MAX_NAME + SINGER_MAX_NAME + 2];
        format_display_name(current, display, sizeof(display));
        if (music_list == NULL) {
            LOGI(TAG, "id=%s subtitle=%s title=%s",
                 current->id[0] != '\0' ? current->id : current->song_id,
                 current->subtitle[0] != '\0' ? current->subtitle : current->singer,
                 current->title[0] != '\0' ? current->title : current->song_name);
        } else {
            music_list[index++] = strdup(display);
        }
        current = current->next;
    }
    if (music_list != NULL) {
        for (int j = index; j < GET_MAX_MUSIC; j++) {
            music_list[j] = NULL;
        }
    }
}

int link_get_next_music(const char *cur_source, const char *cur_song_id, int mode, int force_advance, Music_Node *next_music)
{
    Music_Node *target_node = NULL;
    if (next_music == NULL || g_music_head == NULL || g_music_head->next == NULL) {
        return -1;
    }
    target_node = find_by_identity(cur_source, cur_song_id);
    if (target_node == NULL) {
        target_node = g_music_head->next;
    }

    if (mode == SINGLE_PLAY && !force_advance) {
        copy_node_value(next_music, target_node);
        return 0;
    }

    if (target_node->next == NULL) {
        return 1;
    }
    copy_node_value(next_music, target_node->next);
    return 0;
}

void link_clear_list(void)
{
    Music_Node *current = (g_music_head != NULL) ? g_music_head->next : NULL;
    int changed = (current != NULL);
    while (current != NULL) {
        Music_Node *next = current->next;
        free(current);
        current = next;
    }
    if (g_music_head != NULL) {
        g_music_head->next = NULL;
    }
    if (changed) {
        link_mark_playlist_changed();
    }
    link_debug_dump_list();
}

int link_get_prev_music(const char *cur_source, const char *cur_song_id, int wrap_at_head, Music_Node *prev_music)
{
    Music_Node *target_node = NULL;
    Music_Node *tail;
    if (prev_music == NULL || g_music_head == NULL || g_music_head->next == NULL) {
        return -1;
    }
    target_node = find_by_identity(cur_source, cur_song_id);
    if (target_node == NULL) {
        return -1;
    }
    if (target_node->prev == NULL || target_node->prev == g_music_head) {
        if (wrap_at_head) {
            tail = g_music_head;
            while (tail->next != NULL) {
                tail = tail->next;
            }
            copy_node_value(prev_music, tail);
            return 0;
        }
        copy_node_value(prev_music, target_node);
        return 1;
    }
    copy_node_value(prev_music, target_node->prev);
    return 0;
}

int link_read_udisk_music(void)
{
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    link_clear_list();
    dir = opendir(UDISK_MOUNT_PATH);
    if (dir == NULL) {
        LOGE(TAG, "打开U盘挂载目录失败: %s", strerror(errno));
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR || entry->d_name[0] == '.') {
            continue;
        }
        if (link_add(entry->d_name) == 0) {
            count++;
        }
    }
    LOGI(TAG, "[添加歌曲] 共找到 %d 个歌曲", count);
    if (closedir(dir) != 0) {
        LOGW(TAG, "关闭U盘目录失败: %s", strerror(errno));
    }
    return 0;
}

