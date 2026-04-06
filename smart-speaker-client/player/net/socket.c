#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/select.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <json-c/json.h>

#include "debug_log.h"
#include "select.h"
#include "socket.h"
#include "shm.h"
#include "device.h"
#include "link.h"
#include "player.h"
#include "runtime_config.h"
#include "socket_report.h"
#include "music_source_server.h"

#define TAG "SOCKET"

int g_socket_fd = -1;       // socket文件描述符
pthread_t g_report_tid;     // 定时上报数据线程的线程id
static int g_socket_report_thread_started;

void socket_close_connection(void)
{
    int fd;

    if (g_socket_fd < 0) {
        return;
    }
    fd = g_socket_fd;
    FD_CLR(fd, &READSET);
    if (g_socket_report_thread_started) {
        socket_report_stop_thread(fd, g_report_tid);
        g_socket_report_thread_started = 0;
    }
    shutdown(fd, SHUT_RDWR);
    close(fd);
    g_socket_fd = -1;
    update_max_fd();
}

static int socket_connect_with_timeout(int fd, const struct sockaddr_in *server_info, int timeout_ms)
{
    int flags;
    int ret;
    int so_error = 0;
    socklen_t so_error_len = sizeof(so_error);
    fd_set wfds;
    struct timeval tv;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) return -1;

    ret = connect(fd, (const struct sockaddr *)server_info, sizeof(*server_info));
    if (ret != 0 && errno != EINPROGRESS) {
        (void)fcntl(fd, F_SETFL, flags);
        return -1;
    }
    if (ret != 0) {
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ret = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (ret <= 0 ||
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) != 0 ||
            so_error != 0) {
            (void)fcntl(fd, F_SETFL, flags);
            errno = (so_error != 0) ? so_error : ETIMEDOUT;
            return -1;
        }
    }
    if (fcntl(fd, F_SETFL, flags) != 0) return -1;
    return 0;
}

static const char *socket_server_ip(void)
{
    return player_runtime_server_ip();
}

static int socket_server_port(void)
{
    return player_runtime_server_port();
}

static void socket_handle_disconnect(void)
{
    if (g_socket_fd < 0) {
        return;
    }
    socket_close_connection();
    if (player_env_forces_offline()) {
        return;
    }
    LOGW(TAG, "服务器断开，进入离线模式");
    player_offline_init_storage_and_library(1);
    tts_play_audio_file(SERVER_DISCONNECT_OFFLINE_WAV);
}

static const char *socket_json_optional_string(struct json_object *obj, const char *key)
{
    struct json_object *value;

    if (obj == NULL || key == NULL) {
        return NULL;
    }
    value = json_object_object_get(obj, key);
    if (value == NULL || !json_object_is_type(value, json_type_string)) {
        return NULL;
    }
    return json_object_get_string(value);
}

static void socket_add_playlist_page_fields(struct json_object *json)
{
    player_playlist_ctx_t ctx;
    if (json == NULL) {
        return;
    }
    player_get_playlist_ctx(&ctx);
    json_object_object_add(json, "playlist_page", json_object_new_int(ctx.current_page));
    json_object_object_add(json, "playlist_total_pages", json_object_new_int(ctx.total_pages));
}

static void socket_add_queue_snapshot_fields(struct json_object *json, const Shm_Data *data)
{
    int current_index;

    if (json == NULL || data == NULL) {
        return;
    }
    current_index = link_get_current_index(data->current_source, data->current_song_id);
    json_object_object_add(json, "deviceid", json_object_new_string(player_runtime_device_id()));
    json_object_object_add(json, "playlist_version", json_object_new_int64((int64_t)link_get_playlist_version()));
    json_object_object_add(json, "current_index", json_object_new_int(current_index));
    json_object_object_add(json, "current_source", json_object_new_string(data->current_source));
    json_object_object_add(json, "current_song_id", json_object_new_string(data->current_song_id));
    socket_add_playlist_page_fields(json);
}

static void socket_append_music_item(struct json_object *music_arr, const Music_Node *node)
{
    struct json_object *item;
    const char *title;
    const char *subtitle;
    const char *song_id;

    if (music_arr == NULL || node == NULL) {
        return;
    }
    title = (node->title[0] != '\0') ? node->title : node->song_name;
    subtitle = (node->subtitle[0] != '\0') ? node->subtitle : node->singer;
    song_id = (node->id[0] != '\0') ? node->id : node->song_id;
    item = json_object_new_object();
    json_object_object_add(item, "title", json_object_new_string(title));
    json_object_object_add(item, "subtitle", json_object_new_string(subtitle));
    json_object_object_add(item, "source", json_object_new_string(node->source));
    json_object_object_add(item, "id", json_object_new_string(song_id));
    json_object_array_add(music_arr, item);
}

// 初始化socket连接
int socket_init()
{
    const char *server_ip = socket_server_ip();
    int server_port = socket_server_port();

    socket_close_connection();

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
    server_info.sin_port = htons(server_port);
    server_info.sin_addr.s_addr = inet_addr(server_ip);

    // 连接套接字
    int ret = 0;
    int count = 2;
    while(count-- > 0)
    {
        ret = socket_connect_with_timeout(g_socket_fd, &server_info, 1000);
        if(-1 == ret)
        {
            LOGE(TAG, "连接服务器失败: %s %s:%d (剩余尝试次数: %d)",
                 strerror(errno), server_ip, server_port, count);
            usleep(200 * 1000);
            continue;
        }
        LOGI(TAG, "连接服务器成功: %s:%d", server_ip, server_port);

        if (!player_env_forces_offline()) {
            link_clear_list();
        }

        // 把服务器套接字加入到select中
        FD_SET(g_socket_fd, &READSET);

        g_max_fd = (g_max_fd > g_socket_fd ? g_max_fd : g_socket_fd);
        // 每隔五秒上报一次次数据 （当前歌曲、模式、音量、状态'暂停播放停止'）
        // 创建一个线程用于上报
        if (socket_report_start_thread(&g_report_tid) != 0) {
            FD_CLR(g_socket_fd, &READSET);
            close(g_socket_fd);
            g_socket_fd = -1;
            update_max_fd();
            return -1;
        }
        g_socket_report_thread_started = 1;
        LOGI(TAG, "创建上报线程成功!");

        if (!player_env_forces_offline()) {
            g_current_online_mode = ONLINE_MODE_YES;
            player_warm_online_playlist();
        }

        return 0;
    }
    LOGE(TAG, "多次尝试后连接服务器失败");
    close(g_socket_fd);
    g_socket_fd = -1;
    return -1;
}

int socket_recv_data(char *buf)
{
    int len;
    size_t recv_len;
    ssize_t rcv;

    if (buf == NULL || g_socket_fd < 0) {
        return -1;
    }

    recv_len = 0;
    while (recv_len < sizeof(int)) {
        rcv = recv(g_socket_fd, buf + recv_len, sizeof(int) - recv_len, 0);
        if (rcv > 0) {
            recv_len += (size_t)rcv;
            continue;
        }
        if (rcv == 0 || (rcv < 0 && errno != EINTR)) {
            socket_handle_disconnect();
            return -1;
        }
    }
    len = *(int *)buf;
    if (len < 0 || len > SOCKET_JSON_BUF_MAX) {
        LOGE(TAG, "非法报文长度: %d", len);
        socket_handle_disconnect();
        return -1;
    }

    memset(buf, 0, sizeof(int));
    recv_len = 0;
    while (recv_len < (size_t)len) {
        rcv = recv(g_socket_fd, buf + recv_len, (size_t)len - recv_len, 0);
        if (rcv > 0) {
            recv_len += (size_t)rcv;
            continue;
        }
        if (rcv == 0 || (rcv < 0 && errno != EINTR)) {
            socket_handle_disconnect();
            return -1;
        }
    }
    buf[len] = '\0';
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

    {
        char *msg = malloc((size_t)SOCKET_JSON_BUF_MAX + 1u);
        if (msg == NULL) {
            return -1;
        }
        memset(msg, 0, (size_t)SOCKET_JSON_BUF_MAX + 1u);
        if (socket_recv_data(msg) == 0) {
            Parse_music_name(msg);
        }
        free(msg);
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
    // 判断结果（mpv进程是否启动成功）
    char result[256] = {0};
    // 执行命令并获取结果
    FILE *fp = popen("pgrep -f 'player/run'", "r"); // 用于查找名称包含"mpv"的进程的PID
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
    // 判断结果（mpv进程是否停止成功）
    char result[256] = {0};
    // 执行命令并获取结果
    FILE *fp = popen("pgrep -f 'player/run'", "r"); // 用于查找名称包含"mpv"的进程的PID
    fgets(result, sizeof(result), fp);  // 读取进程ID（如果有）
    pclose(fp);
    // 判断结果（mpv进程是否停止成功）
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

void socket_play_assign_song(const char *json_buf)
{
    struct json_object *jo;
    const char *music;
    const char *source;
    const char *song_id;
    const char *title;
    const char *subtitle;
    int ok;
    struct json_object *reply;

    if (json_buf == NULL) {
        return;
    }
    jo = json_tokener_parse(json_buf);
    if (jo == NULL) {
        reply = json_object_new_object();
        json_object_object_add(reply, "cmd", json_object_new_string("reply_app_play_assign_song"));
        json_object_object_add(reply, "result", json_object_new_string("failure"));
        socket_send_data(reply);
        return;
    }
    music = socket_json_optional_string(jo, "music");
    source = socket_json_optional_string(jo, "source");
    song_id = socket_json_optional_string(jo, "id");
    title = socket_json_optional_string(jo, "title");
    subtitle = socket_json_optional_string(jo, "subtitle");
    if ((song_id == NULL || song_id[0] == '\0') &&
        (music == NULL || music[0] == '\0') &&
        (title == NULL || title[0] == '\0')) {
        json_object_put(jo);
        reply = json_object_new_object();
        json_object_object_add(reply, "cmd", json_object_new_string("reply_app_play_assign_song"));
        json_object_object_add(reply, "result", json_object_new_string("failure"));
        socket_send_data(reply);
        return;
    }
    if ((song_id != NULL && song_id[0] != '\0') || (title != NULL && title[0] != '\0')) {
        ok = (player_play_assign_or_insert_song(source, song_id, title, subtitle) == 0);
    } else {
        ok = (player_play_assign_display(music) == 0);
    }
    json_object_put(jo);
    reply = json_object_new_object();
    json_object_object_add(reply, "cmd", json_object_new_string("reply_app_play_assign_song"));
    json_object_object_add(reply, "result", json_object_new_string(ok ? "success" : "failure"));
    socket_send_data(reply);
}

void socket_play_playlist(const char *json_buf)
{
    struct json_object *jo;
    const char *source;
    const char *playlist_id;
    MusicSourceResult result;
    struct json_object *reply;
    int i;
    int ok = 0;

    if (json_buf == NULL) {
        return;
    }
    memset(&result, 0, sizeof(result));
    jo = json_tokener_parse(json_buf);
    if (jo == NULL) {
        reply = json_object_new_object();
        json_object_object_add(reply, "cmd", json_object_new_string("reply_app_play_playlist"));
        json_object_object_add(reply, "result", json_object_new_string("failure"));
        socket_send_data(reply);
        return;
    }
    source = socket_json_optional_string(jo, "source");
    playlist_id = socket_json_optional_string(jo, "id");
    if (playlist_id != NULL && playlist_id[0] != '\0' &&
        music_source_server_load_playlist_detail(playlist_id, source, &result) == 0 && result.count > 0) {
        link_clear_list();
        ok = 1;
        for (i = 0; i < result.count; ++i) {
            const MusicSourceItem *it = &result.items[i];
            if (link_add_music_lib(it->source, it->song_id, it->singer, it->song_name,
                                   (it->play_url[0] != '\0') ? it->play_url : NULL) != 0) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            player_stop_play();
            player_start_play();
            socket_upload_music_list();
        }
    }
    music_source_free_result(&result);
    json_object_put(jo);
    reply = json_object_new_object();
    json_object_object_add(reply, "cmd", json_object_new_string("reply_app_play_playlist"));
    json_object_object_add(reply, "result", json_object_new_string(ok ? "success" : "failure"));
    socket_send_data(reply);
}

// 处理服务器设置单曲循环播放模式请求
void socket_set_single_mode()
{
    Shm_Data current_data;
    Shm_Data next_data;
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

void socket_playlist_page_next(void)
{
    json_object *json;
    if (player_playlist_load_next_page() > 0) {
        json = json_object_new_object();
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_playlist_next_page"));
        json_object_object_add(json, "result", json_object_new_string("success"));
        socket_send_data(json);
    } else {
        json = json_object_new_object();
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_playlist_next_page"));
        json_object_object_add(json, "result", json_object_new_string("failure"));
        socket_send_data(json);
    }
}

void socket_playlist_page_prev(void)
{
    json_object *json;
    if (player_playlist_load_prev_page() > 0) {
        json = json_object_new_object();
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_playlist_prev_page"));
        json_object_object_add(json, "result", json_object_new_string("success"));
        socket_send_data(json);
    } else {
        json = json_object_new_object();
        json_object_object_add(json, "cmd", json_object_new_string("reply_app_playlist_prev_page"));
        json_object_object_add(json, "result", json_object_new_string("failure"));
        socket_send_data(json);
    }
}

// 处理服务器获取当前音乐列表请求（主动上传新的歌曲）
void socket_upload_music_list()
{
    Shm_Data data;
    json_object *json;
    json_object *music_arr;
    int i;

    shm_get(&data);
    json = json_object_new_object();
    json_object_object_add(json, "cmd", json_object_new_string("upload_music_list"));
    socket_add_queue_snapshot_fields(json, &data);
    music_arr = json_object_new_array();
    for (i = 0; i < GET_MAX_MUSIC; i++) {
        Music_Node item;
        memset(&item, 0, sizeof(item));
        if (link_get_music_at(i, &item) != 0) {
            break;
        }
        socket_append_music_item(music_arr, &item);
    }
    json_object_object_add(json, "music", music_arr);
    if (json != NULL) {
        socket_send_data(json);
    }
}


// 断开服务器
int socket_disconnect()
{
    socket_close_connection();
    return 0;
}


// 连接服务器
int socket_connect(void)
{
    // 调用socket.c中已实现的socket初始化逻辑（连接服务器+创建上报线程）
    int ret = socket_init();
    if (ret != 0) {
        LOGE(TAG, "连接服务器失败，无法切换在线模式");
        tts_play_audio_file(SERVER_CONNECT_FAILED_WAV);
        return -1;
    }
    return 0;
}

