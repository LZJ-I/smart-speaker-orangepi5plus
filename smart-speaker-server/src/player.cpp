#include "player.h"
#include "server.h"

#include <ctime>

namespace {

std::string json_string_or_empty(const Json::Value &obj, const char *key)
{
    if (!obj.isObject() || !obj.isMember(key))
        return "";
    return obj[key].asString();
}

int json_int_or_default(const Json::Value &obj, const char *key, int default_value)
{
    if (!obj.isObject() || !obj.isMember(key))
        return default_value;
    return obj[key].asInt();
}

void sync_cached_snapshots_to_app(PlayerInfo_t *player, Server *s)
{
    if (player == nullptr || s == nullptr || player->m_app_bev == nullptr) {
        return;
    }
    if (player->m_last_device_report.isObject()) {
        s->server_send_data(player->m_app_bev, player->m_last_device_report);
    }
    if (player->m_last_music_list.isObject()) {
        s->server_send_data(player->m_app_bev, player->m_last_music_list);
    }
}

}  // namespace

void PlayerInfo::player_timer_cb(evutil_socket_t fd, short events, void *arg)
{
    PlayerInfo *p = (PlayerInfo *)arg;
    (void)fd;
    (void)events;

    for (auto it = p->m_player_list->begin(); it != p->m_player_list->end();) {
        bool erase_current = false;

        if (time(NULL) - it->m_app_last_time > TIMEOUT && !it->m_appid.empty()) {
            Server::debug("[超时的APP] APPID：%s", it->m_appid.c_str());
            it->m_appid.clear();
            if (it->m_app_bev != nullptr) {
                bufferevent_free(it->m_app_bev);
                it->m_app_bev = nullptr;
            }
        }

        if (time(NULL) - it->m_device_last_time > TIMEOUT) {
            Server::debug("[有音箱超时了] 音箱ID：%s", it->m_deviceid.c_str());
            if (it->m_device_bev != nullptr) {
                bufferevent_free(it->m_device_bev);
                it->m_device_bev = nullptr;
            }
            if (it->m_app_bev != nullptr) {
                bufferevent_free(it->m_app_bev);
                it->m_app_bev = nullptr;
            }
            erase_current = true;
        }

        if (erase_current) {
            it = p->m_player_list->erase(it);
        } else {
            ++it;
        }
    }
}

void PlayerInfo::player_start_timer(Server *s)
{
    struct timeval timeout;
    evutil_timerclear(&timeout);
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    m_timer_event = event_new(s->server_get_eventbase(), -1, EV_PERSIST, player_timer_cb, this);

    if (m_timer_event == NULL) {
        Server::debug("定时器事件创建失败");
        return;
    }

    if (event_add(m_timer_event, &timeout) != 0) {
        Server::debug("定时器添加到事件循环失败");
        event_free(m_timer_event);
        m_timer_event = NULL;
        return;
    }

    Server::debug("定时器已启动，超时时间：%d秒", timeout.tv_sec);
}

PlayerInfo::PlayerInfo()
{
    m_player_list = new std::list<PlayerInfo_t>();
    m_timer_event = NULL;
}

PlayerInfo::~PlayerInfo()
{
    if (m_player_list != NULL) {
        delete m_player_list;
        m_player_list = NULL;
    }
    if (m_timer_event != NULL) {
        event_free(m_timer_event);
        m_timer_event = NULL;
        Server::debug("定时器事件已释放");
    }
}

void PlayerInfo::player_device_update_infolist(struct bufferevent *bev, const Json::Value &report, Server *s)
{
    bool is_exist = false;
    std::string deviceid = json_string_or_empty(report, "deviceid");
    std::string cur_singer = json_string_or_empty(report, "cur_singer");
    std::string cur_music = json_string_or_empty(report, "cur_music");
    std::string state = json_string_or_empty(report, "state");
    int cur_volume = json_int_or_default(report, "cur_volume", 0);
    int cur_mode = json_int_or_default(report, "cur_mode", 0);
    auto it = m_player_list->begin();
    for (; it != m_player_list->end(); it++) {
        if (deviceid == it->m_deviceid) {
            it->m_cur_singer = cur_singer;
            it->m_cur_music = cur_music;
            it->m_state = state;
            it->m_cur_volume = cur_volume;
            it->m_cur_mode = cur_mode;
            it->m_device_last_time = time(NULL);
            it->m_device_bev = bev;
            it->m_last_device_report = report;

            if (it->m_app_bev != nullptr) {
                s->server_send_data(it->m_app_bev, report);
            }
            is_exist = true;
            break;
        }
    }

    if (!is_exist) {
        PlayerInfo_t player_info;
        player_info.m_deviceid = deviceid;
        player_info.m_cur_singer = cur_singer;
        player_info.m_cur_music = cur_music;
        player_info.m_state = state;
        player_info.m_cur_volume = cur_volume;
        player_info.m_cur_mode = cur_mode;
        player_info.m_device_last_time = time(NULL);
        player_info.m_app_last_time = 0;
        player_info.m_last_device_report = report;
        player_info.m_device_bev = bev;
        player_info.m_app_bev = nullptr;
        m_player_list->push_back(player_info);
        Server::debug("嵌入式端首次上报，添加到链表中");
    }
}

void PlayerInfo::player_app_update_infolist(struct bufferevent *bev, const Json::Value &report, Server *s)
{
    std::string deviceid = json_string_or_empty(report, "deviceid");
    std::string appid = json_string_or_empty(report, "appid");
    auto it = m_player_list->begin();
    for (; it != m_player_list->end(); it++) {
        if (deviceid == it->m_deviceid) {
            bool need_sync = (it->m_app_bev != bev);
            it->m_app_last_time = time(NULL);
            it->m_app_bev = bev;
            it->m_appid = appid;
            if (need_sync) {
                sync_cached_snapshots_to_app(&(*it), s);
            }
            break;
        }
    }
}

void PlayerInfo::player_device_update_music_list(struct bufferevent *bev, const Json::Value &report, Server *s)
{
    for (auto it = m_player_list->begin(); it != m_player_list->end(); it++) {
        if (it->m_device_bev == bev) {
            it->m_last_music_list = report;
            if (it->m_app_bev != nullptr) {
                Server::debug("嵌入式端上报音乐列表 转发给应用端");
                s->server_send_data(it->m_app_bev, report);
            } else {
                Server::debug("嵌入式端上报音乐列表 应用端未连接");
            }
        }
    }
}

std::list<PlayerInfo_t> *PlayerInfo::player_get_m_player_list(void)
{
    return m_player_list;
}

void PlayerInfo::player_app_register(struct bufferevent *bev, const Json::Value &json, Server *s)
{
    Json::Value result(Json::objectValue);
    std::string appid = json_string_or_empty(json, "appid");
    std::string password = json_string_or_empty(json, "password");

    result["cmd"] = "reply_app_register";

    do {
        if (appid.size() <= 3) {
            result["result"] = "idshort";
            break;
        }
        if (password.size() <= 3) {
            result["result"] = "passhort";
            break;
        }
        int res = s->server_get_database()->user_register(appid, password);
        if (res == 1) {
            result["result"] = "idexist";
            break;
        } else if (res == -1) {
            result["result"] = "failuse";
            break;
        } else {
            result["result"] = "success";
            break;
        }
    } while (1);

    s->server_send_data(bev, result);
}

void PlayerInfo::player_app_bind(struct bufferevent *bev, const Json::Value &json, Server *s)
{
    Json::Value result(Json::objectValue);
    std::string appid = json_string_or_empty(json, "appid");
    std::string deviceid = json_string_or_empty(json, "deviceid");

    result["cmd"] = "reply_app_bind";

    do {
        if (deviceid.size() <= 3) {
            result["result"] = "devidshort";
            break;
        }
        int res = s->server_get_database()->user_bind(deviceid, appid);
        if (res == 1) {
            result["result"] = "isbind";
        } else if (res == -1) {
            result["result"] = "failuse";
        } else {
            result["result"] = "success";
        }
    } while (0);

    result["deviceid"] = deviceid;
    s->server_send_data(bev, result);
}

void PlayerInfo::player_app_login(struct bufferevent *bev, const Json::Value &json, Server *s)
{
    Json::Value result(Json::objectValue);

    std::string appid = json_string_or_empty(json, "appid");
    std::string password = json_string_or_empty(json, "password");
    std::string deviceid = "";

    result["cmd"] = "reply_app_login";

    do {
        if (appid.size() <= 3) {
            result["result"] = "idshort";
            break;
        }
        if (password.size() <= 3) {
            result["result"] = "passhort";
            break;
        }

        int res = s->server_get_database()->user_login(appid, password, deviceid);
        if (res == 1) {
            result["result"] = "idnotexist";
        } else if (res == 2) {
            result["result"] = "passerr";
        } else if (res == 3) {
            result["result"] = "notbind";
        } else if (res == -1) {
            result["result"] = "failuse";
        } else {
            result["result"] = "success";
            result["deviceid"] = deviceid;
        }
    } while (0);

    s->server_send_data(bev, result);
}
