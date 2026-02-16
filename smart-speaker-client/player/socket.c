#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/select.h>
#include <string.h>
#include <errno.h>
#include <json-c/json.h>

#include "../debug_log.h"
#include "select.h"
#include "socket.h"
#include "shm.h"
#include "main.h"
#include "device.h"
#include "link.h"
#include "player.h"

#define TAG "SOCKET"

int g_socket_fd = -1;       // socket文件描述符
pthread_t g_report_tid;     // 定时上报数据线程的线程id

// 发送JSON数据给服务器
static int socket_send_data(json_object *data)
{
    char buf[1024] = {0};
    // 把json数据转换成字符串
    const char *json_str = json_object_to_json_string(data);
    if(NULL == json_str)
    {
        LOGE(TAG, "JSON转换失败");
        return -1;
    }
    // 判断长度
    int len = strlen(json_str);
    // 封装成一个包
    memcpy(buf, &len, sizeof(len));
    memcpy(buf + sizeof(len), json_str, len);
    // 发送数据
    if(-1 == send(g_socket_fd, buf, len + sizeof(len), 0))
    {
        LOGE(TAG, "发送失败: %s", strerror(errno));
        return -1;
    }

    // 释放json对象
    json_object_put(data);
    return 0;
}

// 用于定时上报数据给服务器
static void* report_thread(void *arg)
{
    // 发送数据给服务器
    // 当前歌曲、播放模式、播放状态、设备id、音量
    int volume;
    while(1)
    {
        // 读取共享内存数据
        Shm_Data data = {0};
        shm_get(&data);
        // 获取音量
        device_get_volume(&volume);
        // 创建json对象
        json_object *json = json_object_new_object();
        // 添加数据
        json_object_object_add(json, "cmd", json_object_new_string("device_report"));  // 数据类型
        json_object_object_add(json, "cur_singer", json_object_new_string(data.current_singer));    // 当前音乐
        json_object_object_add(json, "cur_music", json_object_new_string(data.current_music));    // 当前音乐
        json_object_object_add(json, "cur_mode", json_object_new_int(data.current_mode));      // 播放模式
        if(g_current_state == PLAY_STATE_STOP){
            json_object_object_add(json, "state", json_object_new_string("stop"));
        }else if(g_current_state == PLAY_STATE_PLAY && g_current_suspend == PLAY_SUSPEND_NO){
            json_object_object_add(json, "state", json_object_new_string("play"));
        }else if(g_current_state == PLAY_STATE_PLAY && g_current_suspend == PLAY_SUSPEND_YES){
            json_object_object_add(json, "state", json_object_new_string("suspend"));
        }
        json_object_object_add(json, "deviceid", json_object_new_string(DEVICE_ID));
        json_object_object_add(json, "cur_volume", json_object_new_int(volume));
        
        // 发送给服务器
        socket_send_data(json);
        // printf("服务器上报状态成功！\n");
        sleep(1);
    }
    return NULL;
}

// 初始化socket连接
int socket_init()
{
    // 创建套接字
    g_socket_fd = socket(AF_INET, SOCK_STREAM, 0);  // ipv4 tcp 
    if(-1 == g_socket_fd)
    {
        LOGE(TAG, "socket create failed: %s", strerror(errno));
        return -1;
    }

    // 设置服务器地址
    struct sockaddr_in server_info = {0};
    server_info.sin_family = AF_INET;
    server_info.sin_port = htons(PORT);
    server_info.sin_addr.s_addr = inet_addr(IP);

    // 连接套接字
    int ret = 0;
    int count = 20;  // 尝试连接20次
    while(count-- > 0)
    {
        ret = connect(g_socket_fd, (struct sockaddr *)&server_info, sizeof(server_info));
        if(-1 == ret)
        {
            LOGE(TAG, "连接服务器失败: %s (剩余尝试次数: %d)", strerror(errno), count);
            sleep(1);
            continue;
        }
        LOGI(TAG, "连接服务器成功");
        
        // 把服务器套接字加入到select中
        FD_SET(g_socket_fd, &READSET);

        g_max_fd = (g_max_fd > g_socket_fd ? g_max_fd : g_socket_fd);
        // 每隔五秒上报一次次数据 （当前歌曲、模式、音量、状态'暂停播放停止'）
        // 创建一个线程用于上报
        if(pthread_create(&g_report_tid, NULL, report_thread, NULL) != 0)
        {
            LOGE(TAG, "创建上报线程失败");
            close(g_socket_fd);
            return -1;
        }
        LOGI(TAG, "创建上报线程成功!");

        // 设置为在线模式
        g_current_online_mode = ONLINE_MODE_YES;    // 在线模式

        return 0;
    }
    LOGE(TAG, "多次尝试后连接服务器失败");
    close(g_socket_fd);
    return -1;
}

int socket_recv_data(char *buf)
{
    if(buf == NULL)  return -1;

    int len = -1;           // 接收数据长度
    size_t recv_len = 0;    // 已接收数据长度

     // 接收长度
    while (recv_len < sizeof(int))
    {
        int rcv = recv(g_socket_fd, buf + recv_len, sizeof(int) - recv_len, 0);
        recv_len += rcv;
        if(rcv == 0)
        {
            FD_CLR(g_socket_fd, &READSET);  // 删除服务器socket
            close(g_socket_fd);             // 关闭服务器socket
            pthread_cancel(g_report_tid);   // 关闭数据上报线程
            update_max_fd();                // 重新计算最大fd
            LOGW(TAG, "服务器断开，进入离线模式");
        }
    }
    len = *(int *)buf;

    // 接收数据
    memset(buf, 0, sizeof(int));
    recv_len = 0;
    // 接收服务器发送的数据
    while(recv_len < len) {
        int rcv = recv(g_socket_fd, buf + recv_len, len - recv_len, 0);
        recv_len += rcv;
        if(rcv == 0)
        {
            FD_CLR(g_socket_fd, &READSET);  // 删除服务器socket
            close(g_socket_fd);             // 关闭服务器socket
            pthread_cancel(g_report_tid);   // 关闭数据上报线程
            update_max_fd();                // 重新计算最大fd
            LOGW(TAG, "服务器断开，进入离线模式");
        }
    }
    return 0;
}

// 获取指定歌手的音乐
int socket_get_music(const char *singer)
{
    if(!singer) return -1;

    // 发送请求
    json_object *json = json_object_new_object();  
    json_object_object_add(json, "cmd", json_object_new_string("get_music"));
    json_object_object_add(json, "singer", json_object_new_string(singer));
    socket_send_data(json);

    // 接收并解析响应
    char msg[1024] = {0};
    if(socket_recv_data(msg) == 0) {
        // 解析并插入链表
        Parse_music_name(msg);
    }

    return 0;
}


// 处理音乐更新信号
void socket_update_music(int sig)
{
    Shm_Data data;
    shm_get(&data);
    // 回收子进程
    int wstatus;
    waitpid(data.child_pid, &wstatus, 0);  
    // 更新父进程播放标志位
    g_current_state = PLAY_STATE_STOP;
    // 清空链表
    link_clear_list();
    // 请求数据
    socket_get_music(data.current_singer);
    // 重新开始播放
    player_start_play();

    // 通知APP歌曲列表更新乐
    socket_upload_music_list();
}


// 处理服务器播放音乐请求
void socket_start_play()
{
    // 开始播放
    player_start_play();
    // 判断结果（mplayer进程是否启动成功）
    char result[256] = {0};
    // 执行命令并获取结果
    FILE *fp = popen("pgrep mplayer", "r"); // 用于查找名称包含"mplayer"的进程的PID（进程ID）
    fgets(result, sizeof(result), fp);  // 读取进程ID（如果有）
    pclose(fp);
    // 发送判断结果给服务器
    // 创建回复JSON对象
    json_object *json = json_object_new_object();  
    json_object_object_add(json, "cmd", json_object_new_string("reply_app_start_play"));
    if(strlen(result) > 0)  // 发送成功回复
    {
        json_object_object_add(json, "result", json_object_new_string("success"));
    }
    else    // 发送失败回复 
    {
        json_object_object_add(json, "result", json_object_new_string("failure"));
    }
    // 发送回复给服务器
    socket_send_data(json);
}

// 处理服务器停止播放请求
void socket_stop_play()
{
    // 停止播放
    player_stop_play();
    // 判断结果（mplayer进程是否停止成功）
    char result[256] = {0};
    // 执行命令并获取结果
    FILE *fp = popen("pgrep mplayer", "r"); // 用于查找名称包含"mplayer"的进程的PID（进程ID）
    fgets(result, sizeof(result), fp);  // 读取进程ID（如果有）
    pclose(fp);
    // 判断结果（mplayer进程是否停止成功）
    // 创建回复JSON对象
    json_object *json = json_object_new_object();  
    json_object_object_add(json, "cmd", json_object_new_string("reply_app_stop_play"));
    if(strlen(result) == 0)  // 发送成功回复
    {
        json_object_object_add(json, "result", json_object_new_string("success"));
    }
    else    // 发送失败回复 
    {
        json_object_object_add(json, "result", json_object_new_string("failure"));
    }
    // 发送回复给服务器
    socket_send_data(json);
}


// 处理服务器继续播放请求
void socket_continue_play()
{
    // 继续播放
    player_continue_play();
    // 回复服务器
    json_object *json = json_object_new_object();  
    json_object_object_add(json, "cmd", json_object_new_string("reply_app_continue_play"));
    json_object_object_add(json, "result", json_object_new_string("success"));
    socket_send_data(json);
}

// 处理服务器暂停播放请求
void socket_suspend_play()
{
    // 暂停播放
    player_suspend_play();
    // 回复服务器
    json_object *json = json_object_new_object();  
    json_object_object_add(json, "cmd", json_object_new_string("reply_app_suspend_play"));
    json_object_object_add(json, "result", json_object_new_string("success"));
    socket_send_data(json);
}

// 处理服务器上一首音乐请求
void socket_prev_song()
{
    Shm_Data current_data;
    Shm_Data next_data;
    // 读取共享内存，获取当前歌曲
    shm_get(&current_data);
    // 上一首音乐
    int ret = player_prev_song();  // 如果ret = 0 则为正常， ret = 1异常
    // 读取共享内存，获取当前歌曲
    shm_get(&next_data);
    // 对比是否一样
    if(strcmp(current_data.current_music, next_data.current_music) == 0 && ret != 0) //如果一样，且不是第一首歌导致的一样
    {
        // 回复服务器
        json_object *json = json_object_new_object();  
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_play_prev_song"));
        json_object_object_add(json, "result", json_object_new_string("failure"));
        socket_send_data(json);
    }
    else
    {
        // 回复服务器
        json_object *json = json_object_new_object();  
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_play_prev_song"));
        json_object_object_add(json, "result", json_object_new_string("success"));
        socket_send_data(json);
    }
}

// 处理服务器下一首音乐请求
void socket_next_song()
{
    Shm_Data current_data;
    Shm_Data next_data;
    // 读取共享内存，获取当前歌曲
    shm_get(&current_data);
    // 下一首音乐
    int ret = player_next_song();  // 如果ret = 0 则为正常， -1异常  【1：最后一首，不用报告了，后面会有音乐列表上传过去】
    if(ret == 1)    return;
    // 读取共享内存，获取当前歌曲
    shm_get(&next_data);
    // 对比是否一样
    if(strcmp(current_data.current_music, next_data.current_music) == 0 && ret != 0)
    {
        // 回复服务器
        json_object *json = json_object_new_object();  
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_play_next_song"));
        json_object_object_add(json, "result", json_object_new_string("failure"));
        socket_send_data(json);
    }
    else
    {
        // 回复服务器
        json_object *json = json_object_new_object();  
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_play_next_song"));
        json_object_object_add(json, "result", json_object_new_string("success"));
        socket_send_data(json);
    }
}


// 处理服务器增加音量请求
void socket_add_volume()
{
    // 增加音量
    if(device_adjust_volume(1) == 0)    //设置音量成功返回0(自动对比新音量是否与旧音量不同)
    {
        // 回复服务器
        json_object *json = json_object_new_object();  
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_add_volume"));
        json_object_object_add(json, "result", json_object_new_string("success"));
        socket_send_data(json);
    }
    else
    {
        // 音量设置失败
        json_object *json = json_object_new_object();  
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_add_volume"));
        json_object_object_add(json, "result", json_object_new_string("failure"));
        socket_send_data(json);
    }
}

// 处理客户端减少音量请求
void socket_sub_volume()
{
    // 减少音量
    if(device_adjust_volume(0) == 0)    //设置音量成功返回0(自动对比新音量是否与旧音量不同)
    {
        // 回复服务器
        json_object *json = json_object_new_object();  
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_sub_volume"));
        json_object_object_add(json, "result", json_object_new_string("success"));
        socket_send_data(json);
    }
    else
    {
        // 音量设置失败
        json_object *json = json_object_new_object();  
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_sub_volume"));
        json_object_object_add(json, "result", json_object_new_string("failure"));
        socket_send_data(json);
    }
}

// 处理服务器设置随机播放模式请求
void socket_set_random_mode()
{
    Shm_Data current_data;
    Shm_Data next_data;
    // 获取共享内存，获取当前播放模式
    shm_get(&current_data);
    // 设置随机播放模式
    player_set_mode(RANDOM_PLAY);
    // 获取当前播放模式
    shm_get(&next_data);
    if(next_data.current_mode == RANDOM_PLAY)
    {
        // 回复服务器
        json_object *json = json_object_new_object();  
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_random_mode"));
        json_object_object_add(json, "result", json_object_new_string("success"));
        socket_send_data(json);
    }
    else
    {
        // 设置失败
        json_object *json = json_object_new_object();  
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_random_mode"));
        json_object_object_add(json, "result", json_object_new_string("failure"));
        socket_send_data(json);
    }
}

// 处理服务器设置顺序播放模式请求
void socket_set_order_mode()
{
    Shm_Data current_data;
    Shm_Data next_data;
    // 获取共享内存，获取当前播放模式
    shm_get(&current_data);
    // 设置顺序播放模式
    player_set_mode(ORDER_PLAY);
    // 获取当前播放模式
    shm_get(&next_data);
    if(next_data.current_mode == ORDER_PLAY)
    {
        // 回复服务器
        json_object *json = json_object_new_object();  
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_order_mode"));
        json_object_object_add(json, "result", json_object_new_string("success"));
        socket_send_data(json);
    }
    else
    {
        // 设置失败
        json_object *json = json_object_new_object();  
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_order_mode"));
        json_object_object_add(json, "result", json_object_new_string("failure"));
        socket_send_data(json);
    }
}

// 处理服务器设置单曲循环播放模式请求
void socket_set_single_mode()
{
    Shm_Data current_data;
    Shm_Data next_data;
    // 获取共享内存，获取当前播放模式
    shm_get(&current_data);
    // 设置单曲循环播放模式
    player_set_mode(SINGLE_PLAY);
    // 获取当前播放模式
    shm_get(&next_data);
    if(next_data.current_mode == SINGLE_PLAY)
    {
        // 回复服务器
        json_object *json = json_object_new_object();  
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_single_mode"));
        json_object_object_add(json, "result", json_object_new_string("success"));
        socket_send_data(json);
    }
    else
    {
        // 设置失败
        json_object *json = json_object_new_object();  
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_single_mode"));
        json_object_object_add(json, "result", json_object_new_string("failure"));
        socket_send_data(json);
    }
}

// 处理服务器获取当前音乐列表请求（主动上传新的歌曲）
void socket_upload_music_list()
{
    // 遍历链表
    char* music_list[GET_MAX_MUSIC];
    link_traverse_list(music_list);
    // 创建json
    json_object *json = json_object_new_object();  
    json_object_object_add(json, "cmd", json_object_new_string("upload_music_list"));
    json_object *music_arr = json_object_new_array();  // 创建音乐数组
    for(int i = 0; i < GET_MAX_MUSIC; i++)  // 将歌名插入数组
    {
        if(music_list[i] == NULL)
            break;
        json_object_array_add(music_arr, json_object_new_string(music_list[i]));
    }
    json_object_object_add(json, "music", music_arr);  // 将数组添加到json
    // 发送给服务器
    if(json != NULL)
        socket_send_data(json);

    // 释放数组、 释放json
    json_object_put(json);  // 释放json
    for(int i = 0; i<GET_MAX_MUSIC; i++)
    {
        free(music_list[i]);
    }
}


// 断开服务器
int socket_disconnect()
{
    // 取消 上报线程
    pthread_cancel(g_report_tid);

    // 把服务端 fd 从监听集合取消 并 释放
    FD_CLR(g_socket_fd, &READSET);
    update_max_fd();
    close(g_socket_fd);

    return 0;
}


// 连接服务器
int socket_connect(void)
{
    // 调用socket.c中已实现的socket初始化逻辑（连接服务器+创建上报线程）
    int ret = socket_init();
    if (ret != 0) {
        LOGE(TAG, "连接服务器失败，无法切换在线模式");
        tts_play_text("连接服务器失败，切换在线模式失败");
        return -1;
    }
    return 0;
}

