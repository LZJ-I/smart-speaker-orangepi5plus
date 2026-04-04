#ifndef __LINK_H__
#define __LINK_H__

#include <stddef.h>

#define MUSIC_MAX_NAME 256
#define SINGER_MAX_NAME 128
#define MUSIC_SOURCE_MAX 16
#define MUSIC_ID_MAX 256

#define GET_MAX_MUSIC  5 // 单次获取歌曲数量


typedef struct Node{
    char song_name[MUSIC_MAX_NAME];
    char singer[SINGER_MAX_NAME];
    char source[MUSIC_SOURCE_MAX];
    char song_id[MUSIC_ID_MAX];
    struct Node *next;
    struct Node *prev;
}Music_Node;

extern Music_Node* g_music_head;       // 链表头


// 初始化链表
int link_init();
int link_add_music_lib(const char *source, const char *id, const char *artist, const char *name);
int link_get_source_id(const char *song_name, const char *singer, char *source_buf, size_t source_size, char *id_buf, size_t id_size);
int link_get_music_by_source_id(const char *source, const char *id, Music_Node *out_node);
int link_get_first_music(Music_Node *out_node);
// 解析并插入音乐名到链表
int Parse_music_name(char *buf);
// 遍历链表(单纯打印或返回当前歌单)
void link_traverse_list(char** music_list);
// 根据当前歌曲、当前模式、找到下一首歌
int link_get_next_music(const char *cur_source, const char *cur_song_id, int mode, int force_advance, Music_Node *next_music);
// 清空链表
void link_clear_list(void);
// 根据当前歌曲，找到上一首歌
int link_get_prev_music(const char *cur_source, const char *cur_song_id, int wrap_at_head, Music_Node *prev_music);

// 读取U盘内歌曲到链表
int link_read_udisk_music(void);
#endif
