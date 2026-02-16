#ifndef __LINK_H__
#define __LINK_H__

#define MUSIC_MAX_NAME 256
#define SINGER_MAX_NAME 128

#define GET_MAX_MUSIC  5 // 单次获取歌曲数量


typedef struct Node{
    char music_name[MUSIC_MAX_NAME];   // 音乐名称
    struct Node *next;      // 指向下一个节点
    struct Node *prev;      // 指向前一个节点
}Music_Node;

extern Music_Node* g_music_head;       // 链表头


// 初始化链表
int link_init();
// 解析并插入音乐名到链表
int Parse_music_name(char *buf);
// 遍历链表(单纯打印或返回当前歌单)
void link_traverse_list(char** music_list);
// 根据当前歌曲、当前模式、找到下一首歌
int link_get_next_music(char *cur_music, int mode, char *next_music);
// 清空链表
void link_clear_list(void);
// 根据当前歌曲，找到上一首歌
int link_get_prev_music(char *cur_music, char *prev_music);

// 读取U盘内歌曲到链表
int link_read_udisk_music(void);
#endif
