#define LOG_LEVEL 4
#include "../debug_log.h"
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
#include "player.h"

#define TAG "LINK"

Music_Node* g_music_head = NULL;

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
    safe_copy(dst->song_name, sizeof(dst->song_name), src->song_name);
    safe_copy(dst->singer, sizeof(dst->singer), src->singer);
    safe_copy(dst->source, sizeof(dst->source), src->source);
    safe_copy(dst->song_id, sizeof(dst->song_id), src->song_id);
}

static void format_display_name(const Music_Node *node, char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size == 0) return;
    buf[0] = '\0';
    if (node == NULL) return;
    if (node->singer[0] != '\0') {
        snprintf(buf, buf_size, "%s/%s", node->singer, node->song_name);
    } else {
        snprintf(buf, buf_size, "%s", node->song_name);
    }
    buf[buf_size - 1] = '\0';
}

static Music_Node *find_by_identity(const char *source, const char *song_id)
{
    Music_Node *node = (g_music_head != NULL) ? g_music_head->next : NULL;
    while (node != NULL) {
        if (source != NULL && song_id != NULL &&
            source[0] != '\0' && song_id[0] != '\0' &&
            strcmp(node->source, source) == 0 &&
            strcmp(node->song_id, song_id) == 0) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

int link_init()
{
    g_music_head = (Music_Node*)malloc(sizeof(Music_Node));
    if (g_music_head == NULL) {
        LOGE(TAG, "分配链表头节点内存失败");
        return -1;
    }
    memset(g_music_head, 0, sizeof(Music_Node));
    srand((unsigned int)(time(NULL) ^ getpid()));
    return 0;
}

int link_add_music_lib(const char *source, const char *id, const char *artist, const char *name)
{
    if (g_music_head == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    Music_Node *current = g_music_head;
    while (current->next != NULL) current = current->next;
    Music_Node *node = (Music_Node *)malloc(sizeof(Music_Node));
    if (node == NULL) {
        LOGE(TAG, "分配歌曲节点内存失败");
        return -1;
    }
    memset(node, 0, sizeof(Music_Node));
    safe_copy(node->song_name, sizeof(node->song_name), name);
    safe_copy(node->singer, sizeof(node->singer), artist);
    safe_copy(node->source, sizeof(node->source), source);
    safe_copy(node->song_id, sizeof(node->song_id), id);
    if (node->song_id[0] == '\0') {
        safe_copy(node->song_id, sizeof(node->song_id), node->song_name);
    }
    if (node->source[0] == '\0') {
        safe_copy(node->source, sizeof(node->source), "local");
    }
    node->next = NULL;
    node->prev = current;
    current->next = node;
    return 0;
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
        int name_match = (strcmp(node->song_name, song_name) == 0);
        int singer_match = (singer == NULL || singer[0] == '\0' || strcmp(node->singer, singer) == 0);
        if (name_match && singer_match) {
            safe_copy(source_buf, source_size, node->source);
            safe_copy(id_buf, id_size, node->song_id);
            return (source_buf[0] != '\0' && id_buf[0] != '\0') ? 0 : -1;
        }
        node = node->next;
    }
    return -1;
}

int link_get_music_by_source_id(const char *source, const char *id, Music_Node *out_node)
{
    Music_Node *node = NULL;
    if (source == NULL || id == NULL || out_node == NULL || source[0] == '\0' || id[0] == '\0') {
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
    return link_add_music_lib("local", normalized, "", normalized);
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
        if (music_item == NULL) continue;
        link_add(json_object_get_string(music_item));
    }
    json_object_put(obj);
    return 0;
}

void link_traverse_list(char** music_list)
{
    Music_Node* current = (g_music_head != NULL) ? g_music_head->next : NULL;
    int index = 0;
    while (current != NULL && index < GET_MAX_MUSIC) {
        char display[MUSIC_MAX_NAME + SINGER_MAX_NAME + 2];
        format_display_name(current, display, sizeof(display));
        if (music_list == NULL) {
            LOGI(TAG, "id=%s singer=%s song=%s source=%s",
                 current->song_id, current->singer, current->song_name, current->source);
        } else {
            music_list[index++] = strdup(display);
        }
        current = current->next;
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
    while (current != NULL) {
        Music_Node *next = current->next;
        free(current);
        current = next;
    }
    if (g_music_head != NULL) {
        g_music_head->next = NULL;
    }
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

