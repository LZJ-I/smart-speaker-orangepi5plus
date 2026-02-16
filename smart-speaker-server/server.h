#ifndef __SERVER_H
#define __SERVER_H
#include <event.h>
#include <jsoncpp/json/json.h>


#include "database.h"
#include "player.h"

//#define IP      "172.17.187.231"    // 服务器私网IP
#define IP      "10.102.178.47"    // 虚拟机私网IP
#define PORT    8888                // 监听端口

#define GET_MAX_MUSIC   5           // 单次获取最大音乐数量
#define MUSIC_PATH  "/var/www/html/music/" // 音乐文件路径

class Server
{
private:
    struct event_base* m_eventbase;         // 事件的集合
    Database* m_database;                   // 数据库
    PlayerInfo* m_player_info;              // 音箱数据列表
public:
    static void debug(const char* s, ...);              // 调试函数
    Server();                               // 服务器构造函数
    ~Server();                              // 服务器析构函数
    event_base* server_get_eventbase(void);         // 获取 事件的集合
    Database* server_get_database(void);            // 获取 数据库句柄

    void listen(const char* ip, int port);      // 服务器监听函数
    static void listener_cb(struct evconnlistener *, evutil_socket_t, struct sockaddr *, int, void *);  // 监听回调函数
    static void read_cb(struct bufferevent *bev, void *ctx);                // 读取回调函数（有数据可读时调用）

    bool server_read_data(struct bufferevent* bev, Json::Value& root);      // 从读取回调函数中提取内容, 返回解析到的json 根节点
    bool server_send_data(struct bufferevent* bev, Json::Value& root);      // 服务器发送数据函数
    
    bool server_get_music(struct bufferevent* bev, Json::Value& root);      // 嵌入式端 获取音乐列表 【命令】 执行流程
    bool server_app_option(struct bufferevent* bev, Json::Value& root);      // 应用端 不同选项 【命令】 执行流程
    bool server_device_reply_handle(struct bufferevent* bev, Json::Value& root);      // 嵌入式端 回复APP的命令 【命令】 执行流程

    static void event_cb(struct bufferevent *bev, short what, void *ctx);   // bufferevent事件回调函数（有事件发生时调用）
};


#endif
