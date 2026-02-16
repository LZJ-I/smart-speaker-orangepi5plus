#ifndef __SOCKET_H__
#define __SOCKET_H__ 
#include <pthread.h>

#define PORT 8888
//#define IP "127.0.0.1"
#define IP "10.102.178.47"

extern int g_socket_fd; // 服务器socket 文件描述符
extern pthread_t g_report_tid;        // 定时上报数据线程的线程id


// 初始化网络
int socket_init();

// 接收服务器发送的数据
int socket_recv_data(char *buf);

// 从服务器获取歌手singer的音乐
int socket_get_music(const char *singer);

// 更新音乐
void socket_update_music(int sig);

// 读取服务器信息
void select_read_socket(void);

// 处理APP请求的 开始播放
void socket_start_play();
// 处理APP请求的 停止播放
void socket_stop_play();
// 处理APP请求的 继续播放
void socket_continue_play();
// 处理APP请求的 暂停播放
void socket_suspend_play();
// 处理APP请求的 下一首音乐
void socket_next_song();
// 处理APP请求的 上一首音乐
void socket_prev_song();
// 处理APP请求的 增加音量
void socket_add_volume();
// 处理APP请求的 减少音量
void socket_sub_volume();
// 处理APP请求的 设置顺序播放模式
void socket_set_order_mode();
// 处理APP请求的 设置单曲循环播放模式
void socket_set_single_mode();
// 处理APP请求的 设置随机播放模式
void socket_set_random_mode();

// 处理服务器获取当前音乐列表请求（主动上传新的歌曲）
void socket_upload_music_list();

// 断开服务器
int socket_disconnect();
// 连接服务器
int socket_connect();
#endif
