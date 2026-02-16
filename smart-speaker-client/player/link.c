#define LOG_LEVEL 4
#include "../debug_log.h"
#include "link.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <json-c/json.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include "player.h"

#define TAG "LINK"


Music_Node* g_music_head = NULL;    // 链表头

int link_init()
{
    g_music_head = (Music_Node*)malloc(sizeof(Music_Node));
    if(g_music_head == NULL)
    {
        perror("[ERROR] 分配链表头节点内存失败");
        return -1;
    }
    g_music_head->next = NULL;
    g_music_head->prev = NULL;

    return 0;
}

static int link_add(const char *music_name)
{ 
    Music_Node *current = g_music_head;
    // 尾插
    while(current->next != NULL)
        current = current->next;
    
    // 创建节点
    Music_Node *node = (Music_Node*)malloc(sizeof(Music_Node));
    if(node == NULL)
    {
        perror("[ERROR] 分配歌曲节点内存失败");
        return -1;
    }
    strcpy(node->music_name, music_name);   // 拷贝name
    // 去除转义反斜杠（将"\ "替换为" "）
    char *p = node->music_name;
    for (int i = 0; p[i] != '\0'; )
    {
        if (p[i] == '\\' && p[i+1] == ' ')
        {
            // 覆盖反斜杠，后续字符前移一位
            memmove(&p[i], &p[i+1], strlen(&p[i+1]) + 1);
        }
        else
        {
            i++; // 没有匹配到转义字符，继续遍历
        }
    }
    // // 如果有空格，那么将歌曲名称添加""
    // if(strchr(node->music_name, ' ') != NULL)
    // {
    //     char temp[MUSIC_MAX_NAME*2];
    //     snprintf(temp, sizeof(temp), "\"%s\"", node->music_name);
    //     strcpy(node->music_name, temp);
    // }



    node->next = NULL;
    node->prev = current;
    current->next = node;

    return 0;
}

int Parse_music_name(char *buf)
{ 
    // 将json字符串解析成对象
    struct json_object *obj = json_tokener_parse(buf);
    if(NULL == obj)
    {
        LOGE(TAG, "JSON解析失败");
        return -1;
    }
    // 判断cmd字段是否正确
    struct json_object *cmd = json_object_object_get(obj, "cmd");
    if(cmd == NULL)
    {
        LOGE(TAG, "JSON对象中未找到'cmd'字段");
        json_object_put(obj);
        return -1;
    }
    if(strcmp(json_object_get_string(cmd), "reply_music") != 0)
    {
        LOGE(TAG, "解析服务器返回的歌曲列表失败：命令不匹配");
        json_object_put(obj);
        return -1;
    }
    // 获取歌曲列表 对象
    struct json_object *music = json_object_object_get(obj, "music");
    if(music == NULL)
    {
        LOGE(TAG, "JSON对象中未找到'music'字段");
        json_object_put(obj);
        return -1;
    }
    // 遍历歌曲列表
    for(int i = 0; i < json_object_array_length(music); i++)
    {
        // 获取歌曲列表项
        struct json_object *music_item = json_object_array_get_idx(music, i);
        if(music_item == NULL)
        {
            LOGE(TAG, "JSON对象中未找到第%d个歌曲列表项", i);
            continue;
        }
        // 把music_item转换成字符串 并 插入链表
        link_add(json_object_get_string(music_item));
    }

    json_object_put(obj);
    return 0;
}





// 遍历链表
void link_traverse_list(char** music_list)
{
    Music_Node* current = g_music_head->next; ;
    int index = 0;
    while(current != NULL && index < GET_MAX_MUSIC) // 边界检查，防止数组越界
    {
        if(music_list == NULL)  //如果为空，则单纯打印。
            LOGI(TAG, "%s", current->music_name);  
        else    // 如果不为空，则将歌曲名拷贝到数组中。
            music_list[index++] = strdup(current->music_name);  // 拷贝并返回歌曲名指针（需要手动释放）
        current = current->next;
    }
}


// 根据当前歌曲、当前模式、找到下一首歌
// 如果顺序播放完毕，会向服务器 申请 再一次歌曲列表
// return 1 需要申请下一批歌曲列表
// return 0 获取成功
// return -1 获取失败
int link_get_next_music(char *cur_music, int mode, char *next_music)
{ 
    if(cur_music == NULL || next_music == NULL){
        LOGE(TAG, "link_get_next_music: param error");
        return -1;
    }

    // LOGD(TAG, "link_get_next_music: cur_music = %s, mode = %d", cur_music, mode);

    if(mode == SINGLE_PLAY)    // 单曲播放
    {
        // 遍历链表找到当前播放歌曲节点
        Music_Node *node = g_music_head->next;
        Music_Node *target_node = NULL;
        while(node != NULL)
        {
            // strstr返回非NULL表示找到包含cur_music的子串
            if(strstr(node->music_name, cur_music) != NULL)    
            {
                target_node = node;
                break;
            }
            node = node->next;
        }
        // 拷贝链表中完整的歌曲名（歌手/歌曲）
        strcpy(next_music, target_node->music_name);
        return 0;
    }
    else if(mode == ORDER_PLAY)          // 顺序播放
    {
        Music_Node *node = g_music_head->next;
        Music_Node *target_node = NULL;

        // 遍历链表找到当前播放歌曲节点
        while(node != NULL)
        {
            // strstr返回非NULL表示找到包含cur_music的子串
            if(strstr(node->music_name, cur_music) != NULL)    
            {
                target_node = node;
                break;
            }
            node = node->next;
        }

        if(target_node == NULL)  // 未找到当前歌曲
        {
            LOGE(TAG, "链表中未找到当前歌曲：%s", cur_music);
            return -1;
        }

        // 如果下一个节点为空（播放到最后一首）     或者  下一个节点为空字符串（不够MAX_music_num首）
        if(target_node->next == NULL || strcmp(target_node->next->music_name, "") == 0)    // 是最后一首歌曲，需要申请下一批
        {
            // 返回申请码
            return 1;  
        }
        else    // 获取下一首歌
        {
            strcpy(next_music, target_node->next->music_name);
            LOGI(TAG, "下一首歌是：%s", next_music);
            return 0;
        }
    }
    else if(mode == RANDOM_PLAY)    // 随机播放
    { 
        // 暂时按顺序播放
        strcpy(next_music, cur_music);
        return 0;
    }

    return -1;
}


// 清空链表
void link_clear_list(void)
{
    Music_Node *current = g_music_head->next;

    while(current != NULL)
    {
        Music_Node *next = current->next;
        free(current);
        current = next;
    }
    g_music_head->next = NULL;
}

// 根据当前歌曲、当前模式、找到上一首歌
// 如果是第一首，会返回当前歌曲
// return 1 当前歌曲为第一首
// return 0 获取成功
// return -1 获取失败
int link_get_prev_music(char *cur_music, char *prev_music)
{ 
    if(cur_music == NULL || prev_music == NULL){
        LOGE(TAG, "link_get_prev_music: param error");
        return -1;
    }
    Music_Node *node = g_music_head->next;
    Music_Node *target_node = NULL;
    // 遍历链表找到当前播放歌曲节点
    while(node != NULL)
    {
        // strstr返回非NULL表示找到包含cur_music的子串
        if(strstr(node->music_name, cur_music) != NULL)    
        {
            target_node = node;
            break;
        }
        node = node->next;
    }

    if(target_node == NULL)  // 未找到当前歌曲
    {
        LOGE(TAG, "链表中未找到当前歌曲：%s", cur_music);
        return -1;
    }

    if(strcmp(target_node->prev->music_name, "") == 0)    // 是第一首
    {
        // 继续播放当前歌曲
        strcpy(prev_music, target_node->music_name);
        LOGI(TAG, "[INFO] 当前歌曲为第一首歌曲，当前节点歌曲为：%s", target_node->music_name);
        return 1;  
    }
    else    // 获取上一首歌曲
    {
        strcpy(prev_music, target_node->prev->music_name);
        LOGI(TAG, "上一首歌是：%s", prev_music);
        return 0;
    }
}


// 读取U盘内歌曲到链表
int link_read_udisk_music(void)
{
    // 清空
    link_clear_list();
    
    // 打开文件夹
    DIR *dir;
    dir = opendir(UDISK_MOUNT_PATH);
    if(NULL == dir)
    {
        LOGE(TAG, "打开U盘挂载目录失败: %s", strerror(errno));
        return -1;
    }
    // 循环读取目录下所有文件
    struct dirent *entry;
    int count = 0;
    while((entry = readdir(dir)) != NULL)
    {
        // 跳过 目录和隐藏文件
        if(entry->d_type == DT_DIR || entry->d_name[0] == '.')
        {
            continue;
        }
        // 打印文件名
        // LOGD(TAG, "找到文件: %s", entry->d_name);
        count++;
        // 加入链表
        link_add(entry->d_name);
    }
    LOGI(TAG, "[添加歌曲] 共找到 %d 个歌曲~", count);
    // 关闭目录流
    if (closedir(dir) != 0)
    {
        LOGW(TAG, "关闭U盘目录失败: %s", strerror(errno));
    }
    return 0;
}

