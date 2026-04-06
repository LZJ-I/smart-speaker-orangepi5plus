#ifndef __LINK_H__
#define __LINK_H__

#include <stddef.h>

#define MUSIC_MAX_NAME 256
#define SINGER_MAX_NAME 128
#define MUSIC_SOURCE_MAX 16
#define MUSIC_ID_MAX 256
#define MUSIC_PLAY_URL_MAX 2048

#define GET_MAX_MUSIC  1024


typedef struct Node{
    char title[MUSIC_MAX_NAME];
    char subtitle[SINGER_MAX_NAME];
    char id[MUSIC_ID_MAX];
    char song_name[MUSIC_MAX_NAME];
    char singer[SINGER_MAX_NAME];
    char source[MUSIC_SOURCE_MAX];
    char song_id[MUSIC_ID_MAX];
    char play_url[MUSIC_PLAY_URL_MAX];
    struct Node *next;
    struct Node *prev;
}Music_Node;

extern Music_Node* g_music_head;       // 链表头


// 初始化链表
int link_init();
int link_add_music_meta(const char *source, const char *id, const char *title, const char *subtitle,
                        const char *play_url);
int link_add_music_lib(const char *source, const char *id, const char *artist, const char *name,
                       const char *play_url);
Music_Node *link_anchor_for_insert(void);
int link_insert_node_after_meta(Music_Node *anchor, const char *source, const char *id,
                                const char *title, const char *subtitle, const char *play_url);
int link_insert_node_after(Music_Node *anchor, const char *source, const char *id, const char *artist,
                           const char *name, const char *play_url);
int link_get_source_id(const char *song_name, const char *singer, char *source_buf, size_t source_size, char *id_buf, size_t id_size);
int link_get_music_by_source_id(const char *source, const char *id, Music_Node *out_node);
int link_get_first_music(Music_Node *out_node);
// 解析并插入音乐名到链表
int Parse_music_name(char *buf);
// 遍历链表(单纯打印或返回当前歌单)
void link_traverse_list(char** music_list);
int link_get_music_at(int index, Music_Node *out);
int link_get_current_index(const char *source, const char *song_id);
unsigned int link_get_playlist_version(void);
int link_find_by_display_name(const char *display, Music_Node *out);
// 根据当前歌曲、当前模式、找到下一首歌
int link_get_next_music(const char *cur_source, const char *cur_song_id, int mode, int force_advance, Music_Node *next_music);
// 清空链表
void link_clear_list(void);
// 调试：将当前链表写入文件（client.toml 中 music_link_debug = true）
void link_debug_dump_list(void);
// 根据当前歌曲，找到上一首歌
int link_get_prev_music(const char *cur_source, const char *cur_song_id, int wrap_at_head, Music_Node *prev_music);

// 读取U盘内歌曲到链表
int link_read_udisk_music(void);
#endif
