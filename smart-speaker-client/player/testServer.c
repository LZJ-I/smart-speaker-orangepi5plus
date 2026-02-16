#define LOG_LEVEL 4
#include "../debug_log.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <json-c/json.h>

#define TAG "TEST-SERVER"

// 线程参数
typedef struct {
    int client_sockfd;
    struct sockaddr_in client_addr;  // 可选：保存客户端地址，方便日志
} ThreadArgs;


// 发送数据给客户端
int server_send_data(json_object *json_obj, int client_sockfd)
{
    char buf[1024] = {0};
    const char *json_str = json_object_to_json_string(json_obj);
    if(!json_str) return -1;
    
    int len = strlen(json_str);
    memcpy(buf, &len, sizeof(len));
    memcpy(buf + sizeof(len), json_str, len);
    if(-1 == send(client_sockfd, buf, len + sizeof(len), 0))
    {
        LOGE(TAG, "发送失败: %s", strerror(errno));
        return -1;
    }
    json_object_put(json_obj);
    return 0;
}

// 线程清理函数（关闭socket，资源释放）
void thread_cleanup(void *arg)
{
    close(*(int *)arg);
}

// 处理 客户端 【请求】：获取音乐列表
static int server_handle_get_music_request(char *cmd, struct json_object *json, int client_sockfd)
{ 
    LOGI(TAG, "消息类型:[获取音乐列表]");

    // 判断获取谁的音乐
    const char *singer_name = json_object_get_string(json_object_object_get(json, "singer"));
    if(!singer_name)
    {
        LOGE(TAG, "获取音乐人名称失败");
        return -1;
    }
    LOGI(TAG, "获取音乐人名称：%s", singer_name);

    json_object *json_reply = json_object_new_object();
    json_object_object_add(json_reply, "cmd", json_object_new_string("reply_music"));
    json_object *music_array = json_object_new_array();
    // 添加测试音乐
#if 0
    //json_object_array_add(music_array, json_object_new_string("萧敬腾\\ -\\ 怎么说我不爱你.mp3"));
    json_object_array_add(music_array, json_object_new_string("刘振杰1/1.mp3"));
    json_object_array_add(music_array, json_object_new_string("刘振杰2/2.mp3"));
    json_object_array_add(music_array, json_object_new_string("刘振杰3/3.mp3"));
    json_object_array_add(music_array, json_object_new_string("刘振杰4/4.mp3"));
    json_object_array_add(music_array, json_object_new_string("刘振杰5/5.mp3"));
#else
    json_object_array_add(music_array, json_object_new_string("稻香-周杰伦.mp3"));
    json_object_array_add(music_array, json_object_new_string("周杰伦-兰亭序.mp3"));
    json_object_array_add(music_array, json_object_new_string("周杰伦 - 晴天.mp3"));
    json_object_array_add(music_array, json_object_new_string("周杰伦+-+反方向的钟.mp3"));
    json_object_array_add(music_array, json_object_new_string("周杰伦 - 青花瓷.mp3"));
#endif
    json_object_object_add(json_reply, "music", music_array);
    // 发送回复给客户端
    server_send_data(json_reply, client_sockfd);
}

// 处理 客户端 【回复】：开始播放
static int server_handle_music_start_play_reply(char *cmd, struct json_object *json, int client_sockfd)
{ 
    LOGI(TAG, "消息类型:[嵌入式端回复播放音乐请求]");
    // 解析结果字段
    json_object *json_result = json_object_object_get(json, "result");
    // 打印结果
    LOGI(TAG, "开始播放的结果：result = %s", json_object_get_string(json_result));
}

// 处理 客户端 【回复】：停止播放
static int server_handle_music_stop_play_reply(char *cmd, struct json_object *json, int client_sockfd)
{ 
    LOGI(TAG, "消息类型:[嵌入式端回复停止播放请求]");
    // 解析结果字段
    json_object *json_result = json_object_object_get(json, "result");
    // 打印结果
    LOGI(TAG, "停止播放的结果：result = %s", json_object_get_string(json_result));
}

// 处理 客户端 【回复】：暂停播放
static int server_handle_music_suspend_play_reply(char *cmd, struct json_object *json, int client_sockfd)
{ 
    LOGI(TAG, "消息类型:[嵌入式端回复暂停播放请求]");
    // 解析结果字段
    json_object *json_result = json_object_object_get(json, "result");
    // 打印结果
    LOGI(TAG, "暂停播放的结果：result = %s", json_object_get_string(json_result));
}

// 处理 客户端 【回复】：继续播放
static int server_handle_music_continue_play_reply(char *cmd, struct json_object *json, int client_sockfd)
{ 
    LOGI(TAG, "消息类型:[嵌入式端回复继续播放请求]");
    // 解析结果字段
    json_object *json_result = json_object_object_get(json, "result");
    // 打印结果
    LOGI(TAG, "继续播放的结果：result = %s", json_object_get_string(json_result));
}

// 处理 客户端 【回复】：下一首音乐
static int server_handle_music_next_song_reply(char *cmd, struct json_object *json, int client_sockfd)
{ 
    LOGI(TAG, "消息类型:[嵌入式端回复下一首音乐请求]");
    // 解析结果字段
    json_object *json_result = json_object_object_get(json, "result");
    // 打印结果
    LOGI(TAG, "下一首音乐的结果：result = %s", json_object_get_string(json_result));
}

// 处理 客户端 【回复】：上一首音乐
static int server_handle_music_prev_song_reply(char *cmd, struct json_object *json, int client_sockfd)
{ 
    LOGI(TAG, "消息类型:[嵌入式端回复上一首音乐请求]");
    // 解析结果字段
    json_object *json_result = json_object_object_get(json, "result");
    // 打印结果
    LOGI(TAG, "上一首音乐的结果：result = %s", json_object_get_string(json_result));
}

// 处理 客户端 【回复】：增加音量
static int server_handle_music_add_volume_reply(char *cmd, struct json_object *json, int client_sockfd)
{ 
    LOGI(TAG, "消息类型:[嵌入式端回复增加音量请求]");
    // 解析结果字段
    json_object *json_result = json_object_object_get(json, "result");
    // 打印结果
    LOGI(TAG, "增加音量的结果：result = %s", json_object_get_string(json_result));
}

// 处理 客户端 【回复】：减少音量
static int server_handle_music_sub_volume_reply(char *cmd, struct json_object *json, int client_sockfd)
{ 
    LOGI(TAG, "消息类型:[嵌入式端回复减少音量请求]");
    // 解析结果字段
    json_object *json_result = json_object_object_get(json, "result");
    // 打印结果
    LOGI(TAG, "减少音量的结果：result = %s", json_object_get_string(json_result));
}

// 处理 客户端 【回复】：随机播放模式
static int server_handle_music_random_mode_reply(char *cmd, struct json_object *json, int client_sockfd)
{ 
    LOGI(TAG, "消息类型:[嵌入式端回复随机播放模式请求]");
    // 解析结果字段
    json_object *json_result = json_object_object_get(json, "result");
    // 打印结果
    LOGI(TAG, "随机播放模式的结果：result = %s", json_object_get_string(json_result));
}
// 处理 客户端 【回复】：顺序播放模式
static int server_handle_music_order_mode_reply(char *cmd, struct json_object *json, int client_sockfd)
{ 
    LOGI(TAG, "消息类型:[嵌入式端回复顺序播放模式请求]");
    // 解析结果字段
    json_object *json_result = json_object_object_get(json, "result");
    // 打印结果
    LOGI(TAG, "顺序播放模式的结果：result = %s", json_object_get_string(json_result));
}

// 处理 客户端 【回复】：单曲循环播放模式
static int server_handle_music_single_mode_reply(char *cmd, struct json_object *json, int client_sockfd)
{ 
    LOGI(TAG, "消息类型:[嵌入式端回复单曲循环播放模式请求]");
    // 解析结果字段
    json_object *json_result = json_object_object_get(json, "result");
    // 打印结果
    LOGI(TAG, "单曲循环播放模式的结果：result = %s", json_object_get_string(json_result));
}

// 处理 客户端 【回复】：当前的音乐列表
static int server_handle_upload_music_list(char *cmd, struct json_object *json, int client_sockfd)
{ 
    LOGI(TAG, "消息类型:[嵌入式端回复当前的音乐列表]");
    // 解析数组
    json_object *music_json = json_object_object_get(json, "music");
    // 判断多少个项
    int music_len = json_object_array_length(music_json);
    // 轮询并输出
    for(int i = 0; i < music_len; i++)
    {
        // 获取子项
        json_object *idx_json = json_object_array_get_idx(music_json, i);
        // 解析音乐名
        const char *music_name_json = json_object_get_string(idx_json);
        // 打印音乐名
        LOGI(TAG, "音乐名：%s", music_name_json);
    }
}

// 解析客户端命令
static int server_parse_client_cmd(char* cmd, struct json_object *json, int client_sockfd)
{
    // 处理 客户端 【上报】：状态
    if(strcmp(cmd, "report") == 0)
    {
        LOGI(TAG, "消息类型:[嵌入式端上报状态]");
    }
    // 处理 客户端 【请求】：获取音乐列表
    else if(strcmp(cmd, "get_music") == 0)
    {   
        server_handle_get_music_request(cmd, json, client_sockfd);
    }
    // 处理 客户端 【回复】：开始播放
    else if(strcmp(cmd, "reply_app_start_play") == 0)
    {
        server_handle_music_start_play_reply(cmd, json, client_sockfd);
    }
    // 处理 客户端 【回复】：停止播放
    else if(strcmp(cmd, "reply_app_stop_play") == 0)
    { 
        server_handle_music_stop_play_reply(cmd, json, client_sockfd);
    }
    // 处理 客户端 【回复】：暂停播放
    else if(strcmp(cmd, "reply_app_suspend_play") == 0)
    {
        server_handle_music_suspend_play_reply(cmd, json, client_sockfd);
    }
    // 处理 客户端 【回复】：继续播放
    else if(strcmp(cmd, "reply_app_continue_play") == 0)
    {
        server_handle_music_continue_play_reply(cmd, json, client_sockfd);
    }
    // 处理 客户端 【回复】：下一首音乐
    else if(strcmp(cmd, "reply_app_play_next_song") == 0)
    {
        server_handle_music_next_song_reply(cmd, json, client_sockfd);
    }
    // 处理 客户端 【回复】：上一首音乐
    else if(strcmp(cmd, "reply_app_play_prev_song") == 0)
    {
        server_handle_music_prev_song_reply(cmd, json, client_sockfd);
    }
    // 处理 客户端 【回复】：增加音量
    else if(strcmp(cmd, "reply_app_add_volume") == 0)
    {
        server_handle_music_add_volume_reply(cmd, json, client_sockfd);
    }
    // 处理 客户端 【回复】：减少音量
    else if(strcmp(cmd, "reply_app_sub_volume") == 0)
    {
        server_handle_music_sub_volume_reply(cmd, json, client_sockfd);
    }
    // 处理 客户端 【回复】：随机播放模式
    else if(strcmp(cmd, "reply_app_random_mode") == 0)
    {
        server_handle_music_random_mode_reply(cmd, json, client_sockfd);
    }
    // 处理 客户端 【回复】：顺序播放模式
    else if(strcmp(cmd, "reply_app_order_mode") == 0)
    {
        server_handle_music_order_mode_reply(cmd, json, client_sockfd);
    }
    // 处理 客户端 【回复】：单曲循环播放模式
    else if(strcmp(cmd, "reply_app_single_mode") == 0)
    {
        server_handle_music_single_mode_reply(cmd, json, client_sockfd);
    }
    // 处理 客户端 【回复】：当前的音乐列表
    else if(strcmp(cmd, "upload_music_list") == 0)
    {
        server_handle_upload_music_list(cmd, json, client_sockfd);
    }
    // 处理 客户端 【未知】：命令
    else
    {
        LOGE(TAG, "客户端[%d]未知命令：%s", client_sockfd, cmd);
    }
}



// 客户端处理线程
void *rcev_client_thread(void *arg)
{
    // 获取客户端信息
    ThreadArgs *args = (ThreadArgs *)arg;
    int client_sockfd = args->client_sockfd;
    struct sockaddr_in client_addr = args->client_addr;
    // 释放参数内存
    free(args);

    // 注册线程清理函数（确保退出时关闭socket）
    pthread_cleanup_push(thread_cleanup, &client_sockfd);

    char buf[1024] = {0};
    ssize_t recv_ret;
    size_t recv_len = 0;

    while(1)
    {
        memset(buf, 0, sizeof(buf));
        recv_len = 0;

        // 第一步：接收数据长度（4字节）
        while(recv_len < sizeof(int)) {
            recv_ret = recv(client_sockfd, buf + recv_len, sizeof(int) - recv_len, 0);
            if(recv_ret <= 0) {
                LOGI(TAG, "客户端断开连接");
                pthread_exit(NULL);
            }
            recv_len += recv_ret;
        }

         // 接收数据
         int data_len = *(int *)buf;
         if(data_len <= 0 || data_len > sizeof(buf)) {
             LOGW(TAG, "非法数据长度");
             pthread_exit(NULL);
         }
         memset(buf, 0, sizeof(buf));
         recv_len = 0;
         while(recv_len < data_len) {
             recv_ret = recv(client_sockfd, buf + recv_len, data_len - recv_len, 0);
             if(recv_ret <= 0) {
                 LOGI(TAG, "客户端断开连接");
                 pthread_exit(NULL);
             }
             recv_len += recv_ret;
         }
 
        LOGI(TAG, "recv from client[%d]（%s:%d）: len = %d, data = %s", 
               client_sockfd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), 
               data_len, buf);

        // 第三步：解析JSON
        json_object *json = json_tokener_parse(buf);
        if(!json) {
            LOGE(TAG, "无效JSON");
            continue;
        }

        // 解析cmd和data
        json_object *json_cmd = json_object_object_get(json, "cmd");
        if(!json_cmd) {
            json_object_put(json);
            continue;
        }

        char cmd[64] = {0};
        strncpy(cmd, json_object_get_string(json_cmd), sizeof(cmd)-1);

        // 解析 客户端 命令
        server_parse_client_cmd(cmd, json, client_sockfd);

        json_object_put(json);  // 释放JSON对象
    }


    // 线程清理函数：pthread_cleanup_push和pop必须成对出现
    pthread_cleanup_pop(1);  // 1表示执行cleanup函数
    return NULL;
}


int main(int argc, char const *argv[])
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd == -1)
    {
        perror("socket");
        return -1;
    }

    // 端口复用
    int reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        perror("setsockopt");
        close(sockfd);
        return -1;
    }

    // 绑定套接字
    struct sockaddr_in server_info;
    memset(&server_info, 0, sizeof(server_info));
    server_info.sin_family = AF_INET;
    server_info.sin_port = htons(8888);
    server_info.sin_addr.s_addr = INADDR_ANY;  // 监听所有网卡
    if(bind(sockfd, (struct sockaddr *)&server_info, sizeof(server_info)) == -1)
    {
        perror("bind");
        close(sockfd);
        return -1;
    }
    
    // 监听套接字
    if(listen(sockfd, 5) == -1)
    {
        perror("listen");
        close(sockfd);
        return -1;
    }

    LOGI(TAG, "服务器启动成功，监听端口8888...");

    struct sockaddr_in client_info;
    socklen_t client_len = sizeof(client_info);
    pthread_t tid;
    int ret;

    while (1)
    {
        // 阻塞接受客户端连接
        int client_fd = accept(sockfd, (struct sockaddr *)&client_info, &client_len);
        if(-1 == client_fd)
        {
            if (errno == EINTR)  // 被信号中断，重试
                continue;
            perror("accept");
            close(sockfd);
            return -1;
        }

        // 打印客户端信息
        LOGI(TAG, "客户端已连接：IP=%s, PORT=%d, fd=%d", 
               inet_ntoa(client_info.sin_addr), ntohs(client_info.sin_port), client_fd);

        // 创建线程处理客户端消息
        ThreadArgs *args = (ThreadArgs *)malloc(sizeof(ThreadArgs));
        if (args == NULL)
        {
            LOGE(TAG, "线程内存分配失败: %s", strerror(errno));
            close(client_fd);  // 分配失败，关闭客户端fd
            continue;
        }
        args->client_sockfd = client_fd;
        args->client_addr = client_info;

        ret = pthread_create(&tid, NULL, rcev_client_thread, (void *)args);
        if (ret != 0)
        {
            LOGE(TAG, "创建线程失败: %s", strerror(ret));
            free(args);         // 线程创建失败，释放参数内存
            close(client_fd);   // 关闭客户端fd
            continue;
        }

        // 设置线程为分离态（无需主线程join，退出后自动回收资源）
        ret = pthread_detach(tid);
        if (ret != 0)
        {
            LOGW(TAG, "设置线程分离态失败: %s", strerror(ret));
        }

        // test
        
        json_object *json;

        // sleep(2);
        // printf("[模拟转发APP的开始播放音乐]\n");
        // json = json_object_new_object();
        // json_object_object_add(json, "cmd", json_object_new_string("app_start_play"));
        // server_send_data(json, client_fd);

        // sleep(4);
        // printf("[模拟转发APP的暂停播放音乐]\n");
        // json = json_object_new_object();
        // json_object_object_add(json, "cmd", json_object_new_string("app_suspend_play"));
        // server_send_data(json, client_fd);

        // sleep(4);
        // printf("[模拟转发APP的继续播放音乐]\n");
        // json = json_object_new_object();
        // json_object_object_add(json, "cmd", json_object_new_string("app_continue_play"));
        // server_send_data(json, client_fd);

        // sleep(4);
        // printf("[模拟转发APP的下一首音乐]\n");
        // json = json_object_new_object();
        // json_object_object_add(json, "cmd", json_object_new_string("app_play_next_song"));
        // server_send_data(json, client_fd);

        // sleep(4);
        // printf("[模拟转发APP的上一首音乐]\n");
        // json = json_object_new_object();
        // json_object_object_add(json, "cmd", json_object_new_string("app_play_prev_song"));
        // server_send_data(json, client_fd);
       
        // sleep(4);
        // printf("[模拟转发APP的停止播放音乐]\n");
        // json = json_object_new_object();
        // json_object_object_add(json, "cmd", json_object_new_string("app_stop_play"));
        // server_send_data(json, client_fd);

        // sleep(4);
        // printf("[模拟转发APP的增加音量]\n");
        // json = json_object_new_object();
        // json_object_object_add(json, "cmd", json_object_new_string("app_add_volume"));
        // server_send_data(json, client_fd);

        // sleep(4);
        // printf("[模拟转发APP的减少音量]\n");
        // json = json_object_new_object();
        // json_object_object_add(json, "cmd", json_object_new_string("app_sub_volume"));
        // server_send_data(json, client_fd);
        
        // sleep(5);
        // printf("[模拟转发APP的随机播放模式]\n");
        // json = json_object_new_object();
        // json_object_object_add(json, "cmd", json_object_new_string("app_random_mode"));
        // server_send_data(json, client_fd);

        // sleep(5);
        // printf("[模拟转发APP的顺序播放模式]\n");
        // json = json_object_new_object();
        // json_object_object_add(json, "cmd", json_object_new_string("app_order_mode"));
        // server_send_data(json, client_fd);
        
        // sleep(5);
        // printf("[模拟转发APP的单曲循环播放模式]\n");
        // json = json_object_new_object();
        // json_object_object_add(json, "cmd", json_object_new_string("app_single_mode"));
        // server_send_data(json, client_fd);

        sleep(2);
        LOGI(TAG, "[模拟转发APP的获取歌单列表]");
        json = json_object_new_object();
        json_object_object_add(json, "cmd", json_object_new_string("app_get_music_list"));
        server_send_data(json, client_fd);
    }
    // 以下代码永远执行不到
    close(sockfd);

    return 0;
}




