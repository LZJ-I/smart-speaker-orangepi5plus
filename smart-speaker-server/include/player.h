#ifndef __PLAYER_H
#define __PLAYER_H

#include <event2/bufferevent.h>
#include <event2/event.h>
#include <json/json.h>
#include <iostream>
#include <list>

#define TIMEOUT 3

class Server;

enum { ORDER_PLAY, RANDOM_PLAY, SINGLE_PLAY };

typedef struct PlayerInfo_t
{
    std::string m_appid;
    std::string m_deviceid;
    std::string m_cur_singer;
    std::string m_cur_music;
    std::string m_state;
    int m_cur_volume;
    int m_cur_mode;
    time_t m_device_last_time;
    time_t m_app_last_time;
    struct bufferevent *m_device_bev;
    struct bufferevent *m_app_bev;
} PlayerInfo_t;

class PlayerInfo
{
private:
    std::list<PlayerInfo_t> *m_player_list;
    struct event *m_timer_event;

public:
    PlayerInfo();
    ~PlayerInfo();
    std::list<PlayerInfo_t> *player_get_m_player_list(void);

    void player_start_timer(Server *s);
    static void player_timer_cb(evutil_socket_t fd, short events, void *arg);

    void player_device_update_infolist(struct bufferevent *bev, const Json::Value &report, Server *s);
    void player_app_update_infolist(struct bufferevent *bev, const Json::Value &report, Server *s);
    void player_device_update_music_list(struct bufferevent *bev, const Json::Value &report, Server *s);

    void player_app_register(struct bufferevent *bev, const Json::Value &json, Server *s);
    void player_app_bind(struct bufferevent *bev, const Json::Value &json, Server *s);
    void player_app_login(struct bufferevent *bev, const Json::Value &json, Server *s);
};

#endif
