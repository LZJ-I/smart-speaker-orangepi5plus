#ifndef __PLAYER_H
#define __PLAYER_H

#include <iostream>
#include <event.h> // 缓冲区事件库
#include <jsoncpp/json/json.h>
#include <list> // 链表

#define TIMEOUT 1 * 3 // 超时时间 3秒

class Server;   // 声明 服务器类

// 播放模式
enum {
    ORDER_PLAY,     // 顺序播放     //0
    RANDOM_PLAY,    // 随机播放     //1
    SINGLE_PLAY     // 单曲循环     //2
};

typedef struct PlayerInfo_t
{
    std::string m_appid;                // 当前 APPID

    std::string m_deviceid;             // 当前 客户端ID
    std::string m_cur_singer;           // 当前客户端 歌手
    std::string m_cur_music;            // 当前客户端 歌曲
    std::string m_state;                // 当前客户端 状态
    int m_cur_volume;                   // 当前客户端 音量
    int m_cur_mode;                     // 当前客户端 模式

    // 服务器每隔2秒检查一次, 如果没有上报则认为 客户端\应用端 离线
    time_t m_device_last_time;          // 客户端 上次上报时间
    time_t m_app_last_time;             // APP 上次上报时间

    struct bufferevent* m_device_bev;   // 客户端 bufferevent
    struct bufferevent* m_app_bev;      // APP的  bufferevent
} PlayerInfo_t;

class PlayerInfo
{
private:
    std::list<PlayerInfo_t> *m_player_list;     // 音箱数据列表
    struct event* m_timer_event;                // 定时器事件对象指针
public:
    PlayerInfo();
    ~PlayerInfo();
    std::list<PlayerInfo_t>* player_get_m_player_list(void); // 获取 音箱数据列表私有成员

    void player_start_timer(Server* s);          // 启动 定时器
    static void player_timer_cb(evutil_socket_t fd, short events, void* arg);   // 定时器回调函数（用于检测是否掉线）

    void player_device_update_infolist(struct bufferevent* bev, Json::Value& report, Server* s);    // 嵌入式端 上报数据时调用 （创建设备节点、转发服务器）
    void player_app_update_infolist(struct bufferevent* bev, Json::Value& report, Server* s);       // 应用端   上报数据时调用 （完善设备节点）
    void player_device_update_music_list(struct bufferevent* bev, Json::Value& report, Server* s);  // 在嵌入式端上报音乐列表时调用 （通知app）

    void player_app_register(struct bufferevent* bev, Json::Value& report, Server* s);  // 处理APP注册
    void player_app_bind(struct bufferevent* bev, Json::Value& report, Server* s);  // 处理APP绑定设备
    void player_app_login(struct bufferevent* bev, Json::Value& json, Server* s);       // 处理APP登录
};

#endif
