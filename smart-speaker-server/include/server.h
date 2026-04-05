#ifndef __SERVER_H
#define __SERVER_H

#include <event2/bufferevent.h>
#include <event2/event.h>
#include <json/json.h>

#include "database.h"
#include "player.h"

#define PORT 8888

#define GET_MAX_MUSIC 5
#define DEFAULT_PAGE_SIZE 10
#define MUSIC_PATH "/var/www/html/music/"

class Server
{
private:
    struct event_base *m_eventbase;
    Database *m_database;
    PlayerInfo *m_player_info;
    bool m_ok;

public:
    static void debug(const char *s, ...);
    Server();
    ~Server();
    bool initialized_ok(void) const { return m_ok; }

    event_base *server_get_eventbase(void);
    Database *server_get_database(void);

    void listen(const char *ip, int port);
    static void listener_cb(struct evconnlistener *, evutil_socket_t, struct sockaddr *, int, void *);
    static void read_cb(struct bufferevent *bev, void *ctx);

    /** @return 1 已解析一条；0 数据不足待下次 read；负值 已记录错误并丢弃当前帧/长度头 */
    int server_try_read_one_json(struct bufferevent *bev, Json::Value *root);
    bool server_send_data(struct bufferevent *bev, const Json::Value &root);

    bool server_get_music(struct bufferevent *bev, const Json::Value &root);
    bool server_list_music(struct bufferevent *bev, const Json::Value &root);
    bool server_get_play_url(struct bufferevent *bev, const Json::Value &root);
    bool server_resolve_music(struct bufferevent *bev, const Json::Value &root);
    bool server_search_music(struct bufferevent *bev, const Json::Value &root);
    bool server_app_option(struct bufferevent *bev, Json::Value &root);
    bool server_device_reply_handle(struct bufferevent *bev, const Json::Value &root);

    static void event_cb(struct bufferevent *bev, short what, void *ctx);
};

#endif
