#include "server.h"

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <event2/listener.h>
#include <iostream>
#include <netinet/in.h>
#include <random>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <vector>

namespace {

struct MusicFileInfo {
    std::string singer;
    std::string song;
    std::string path;
};

std::string music_root_path(void)
{
    const char *e = getenv("SMART_SPEAKER_MUSIC_PATH");
    if (e != NULL && e[0] != '\0') {
        std::string p(e);
        if (!p.empty() && p.back() != '/')
            p += '/';
        return p;
    }
    return std::string(MUSIC_PATH);
}

bool json_has_string(const Json::Value &obj, const char *key)
{
    return obj.isObject() && obj.isMember(key) && obj[key].isString();
}

bool json_has_int(const Json::Value &obj, const char *key)
{
    return obj.isObject() && obj.isMember(key) && obj[key].isIntegral();
}

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

bool has_stream_audio_suffix(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (ext == NULL)
        return false;
    return strcasecmp(ext, ".mp3") == 0 || strcasecmp(ext, ".flac") == 0;
}

bool path_is_regular_file(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    return S_ISREG(st.st_mode);
}

bool dirent_is_reg_audio(const std::string &dir_with_slash, struct dirent *e)
{
    if (e->d_name[0] == '.')
        return false;
    if (e->d_type == DT_REG)
        return has_stream_audio_suffix(e->d_name);
    if (e->d_type == DT_UNKNOWN) {
        std::string path = dir_with_slash + e->d_name;
        return path_is_regular_file(path) && has_stream_audio_suffix(e->d_name);
    }
    return false;
}

bool contains_keyword(const std::string &text, const std::string &keyword)
{
    return keyword.empty() || text.find(keyword) != std::string::npos;
}

void collect_music_by_singer(const std::string &singer, std::vector<MusicFileInfo> &out)
{
    std::string root = music_root_path();
    std::string music_path = root + singer;
    if (music_path.empty() || music_path.back() != '/')
        music_path += '/';
    DIR *dir = opendir(music_path.c_str());
    if (dir == NULL) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!dirent_is_reg_audio(music_path, entry)) {
            continue;
        }
        MusicFileInfo info;
        info.singer = singer;
        info.song = entry->d_name;
        info.path = singer + "/" + entry->d_name;
        out.push_back(info);
    }
    closedir(dir);
}

void collect_all_music(std::vector<MusicFileInfo> &out)
{
    std::string root = music_root_path();
    DIR *r = opendir(root.c_str());
    if (r == NULL) {
        return;
    }

    struct dirent *singer_entry;
    while ((singer_entry = readdir(r)) != NULL) {
        if (singer_entry->d_name[0] == '.') {
            continue;
        }
        if (singer_entry->d_type != DT_DIR) {
            if (singer_entry->d_type == DT_REG || singer_entry->d_type == DT_UNKNOWN) {
                if (!dirent_is_reg_audio(root, singer_entry)) {
                    continue;
                }
                MusicFileInfo info;
                info.singer.clear();
                info.song = singer_entry->d_name;
                info.path = info.song;
                out.push_back(info);
            }
            continue;
        }

        std::string singer = singer_entry->d_name;
        std::string singer_dir = root + singer;
        if (singer_dir.empty() || singer_dir.back() != '/')
            singer_dir += '/';
        DIR *dir = opendir(singer_dir.c_str());
        if (dir == NULL) {
            continue;
        }

        struct dirent *song_entry;
        while ((song_entry = readdir(dir)) != NULL) {
            if (!dirent_is_reg_audio(singer_dir, song_entry)) {
                continue;
            }
            MusicFileInfo info;
            info.singer = singer;
            info.song = song_entry->d_name;
            info.path = singer + "/" + info.song;
            out.push_back(info);
        }
        closedir(dir);
    }
    closedir(r);
}

void collect_music_by_keyword(const std::string &keyword, std::vector<MusicFileInfo> &out)
{
    std::string root = music_root_path();
    DIR *r = opendir(root.c_str());
    if (r == NULL) {
        return;
    }

    struct dirent *singer_entry;
    while ((singer_entry = readdir(r)) != NULL) {
        if (singer_entry->d_name[0] == '.') {
            continue;
        }
        if (singer_entry->d_type != DT_DIR) {
            if (singer_entry->d_type == DT_REG || singer_entry->d_type == DT_UNKNOWN) {
                if (!dirent_is_reg_audio(root, singer_entry)) {
                    continue;
                }
                std::string song = singer_entry->d_name;
                if (!contains_keyword(song, keyword)) {
                    continue;
                }
                MusicFileInfo info;
                info.singer.clear();
                info.song = song;
                info.path = song;
                out.push_back(info);
            }
            continue;
        }

        std::string singer = singer_entry->d_name;
        std::string singer_dir = root + singer;
        if (singer_dir.empty() || singer_dir.back() != '/')
            singer_dir += '/';
        DIR *dir = opendir(singer_dir.c_str());
        if (dir == NULL) {
            continue;
        }

        struct dirent *song_entry;
        while ((song_entry = readdir(dir)) != NULL) {
            if (!dirent_is_reg_audio(singer_dir, song_entry)) {
                continue;
            }
            std::string song = song_entry->d_name;
            if (!contains_keyword(singer, keyword) && !contains_keyword(song, keyword)) {
                continue;
            }

            MusicFileInfo info;
            info.singer = singer;
            info.song = song;
            info.path = singer + "/" + song;
            out.push_back(info);
        }
        closedir(dir);
    }
    closedir(r);
}

void reply_music_paths(Server *server, struct bufferevent *bev, const std::vector<MusicFileInfo> &items)
{
    Json::Value reply(Json::objectValue);
    Json::Value music_list(Json::arrayValue);
    for (size_t i = 0; i < items.size() && i < (size_t)GET_MAX_MUSIC; ++i) {
        music_list.append(items[i].path);
    }
    while (music_list.size() < GET_MAX_MUSIC) {
        music_list.append("");
    }
    reply["cmd"] = "reply_music";
    reply["music"] = music_list;
    server->server_send_data(bev, reply);
}

std::vector<MusicFileInfo> g_list_music_cache;
bool g_list_music_cache_ready = false;

void ensure_list_music_cache(void)
{
    if (g_list_music_cache_ready) {
        return;
    }
    g_list_music_cache.clear();
    collect_all_music(g_list_music_cache);
    if (g_list_music_cache.size() > 1) {
        unsigned random_seed = std::chrono::system_clock::now().time_since_epoch().count();
        std::shuffle(g_list_music_cache.begin(), g_list_music_cache.end(), std::default_random_engine(random_seed));
    }
    g_list_music_cache_ready = true;
}

}  // namespace

Server::Server()
    : m_eventbase(event_base_new()), m_database(new Database()), m_player_info(NULL), m_ok(false)
{
    if (m_eventbase == NULL || m_database == NULL) {
        return;
    }
    if (m_database->database_connect() == false) {
        return;
    }
    debug("数据库连接成功！字符集已设为 utf8mb4");

    if (m_database->database_init_table() == false) {
        Server::debug("数据库初始化表失败");
        return;
    }
    debug("数据库初始化表成功！");

    m_player_info = new PlayerInfo();
    m_player_info->player_start_timer(this);
    m_ok = true;
}

Server::~Server()
{
    if (m_player_info != NULL) {
        delete m_player_info;
        m_player_info = NULL;
        debug("PlayerInfo 已释放");
    }
    if (m_database != NULL) {
        delete m_database;
        m_database = NULL;
        std::cout << "数据库已断开连接" << std::endl;
    }
    if (m_eventbase != NULL) {
        event_base_free(m_eventbase);
        m_eventbase = NULL;
        std::cout << "事件基础对象已释放" << std::endl;
    }
}

event_base *Server::server_get_eventbase(void)
{
    return m_eventbase;
}

Database *Server::server_get_database(void)
{
    return m_database;
}

void Server::listen(const char *ip, int port)
{
    struct sockaddr_in server_info;
    memset(&server_info, 0, sizeof(server_info));
    server_info.sin_family = AF_INET;
    server_info.sin_addr.s_addr = inet_addr(ip);
    server_info.sin_port = htons(port);
    int socketlen = sizeof(server_info);

    struct evconnlistener *listener = evconnlistener_new_bind(
        m_eventbase, listener_cb, this, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, 5,
        (struct sockaddr *)&server_info, socketlen);
    if (listener == NULL) {
        perror("evconnlistener_new_bind");
        return;
    }

    event_base_dispatch(m_eventbase);
    evconnlistener_free(listener);
}

void Server::listener_cb(struct evconnlistener *l, evutil_socket_t fd, struct sockaddr *c, int socklen, void *arg)
{
    Server *s = (Server *)arg;
    struct sockaddr_in *client_info = (struct sockaddr_in *)c;
    (void)l;
    (void)socklen;

    s->debug("[新的客户端连接]: %s:%d", inet_ntoa(client_info->sin_addr), ntohs(client_info->sin_port));

    struct bufferevent *bev = bufferevent_socket_new(s->m_eventbase, fd, BEV_OPT_CLOSE_ON_FREE);
    if (bev == NULL) {
        perror("bufferevent_socket_new");
        return;
    }
    bufferevent_setcb(bev, read_cb, NULL, event_cb, s);
    bufferevent_enable(bev, EV_READ);
}

void Server::read_cb(struct bufferevent *bev, void *ctx)
{
    Server *s = (Server *)ctx;
    Json::Value root;
    std::string cmd;
    if (s->server_read_data(bev, &root) == false) {
        Server::debug("数据读取或解析失败，跳过该消息");
        return;
    }
    if (!json_has_string(root, "cmd")) {
        Server::debug("JSON格式错误：缺少cmd字段或cmd类型非法");
        return;
    }

    cmd = json_string_or_empty(root, "cmd");
    if (cmd == "get_music") {
        s->debug("[消息类型] 嵌入式端获取音乐列表");
        s->server_get_music(bev, root);
    } else if (cmd == "list_music") {
        s->debug("[消息类型] 音乐列表(伪随机分页)");
        s->server_list_music(bev, root);
    } else if (cmd == "search_music") {
        s->debug("[消息类型] 搜索音乐");
        s->server_search_music(bev, root);
    } else if (cmd == "device_report") {
        s->m_player_info->player_device_update_infolist(bev, root, s);
    } else if (cmd == "upload_music_list") {
        s->debug("[消息类型] 嵌入式端上传音乐列表");
        s->m_player_info->player_device_update_music_list(bev, root, s);
    } else if (cmd == "app_report") {
        s->m_player_info->player_app_update_infolist(bev, root, s);
    } else if (cmd == "app_register") {
        s->m_player_info->player_app_register(bev, root, s);
    } else if (cmd == "app_bind") {
        s->m_player_info->player_app_bind(bev, root, s);
    } else if (cmd == "app_login") {
        s->m_player_info->player_app_login(bev, root, s);
    } else if (cmd == "app_start_play" || cmd == "app_stop_play" || cmd == "app_suspend_play" ||
               cmd == "app_continue_play" || cmd == "app_play_next_song" || cmd == "app_play_prev_song" ||
               cmd == "app_add_volume" || cmd == "app_sub_volume" || cmd == "app_order_mode" ||
               cmd == "app_single_mode" || cmd == "app_random_mode" || cmd == "app_get_music_list") {
        s->server_app_option(bev, root);
    } else if (cmd == "reply_app_start_play" || cmd == "reply_app_stop_play" || cmd == "reply_app_suspend_play" ||
               cmd == "reply_app_continue_play" || cmd == "reply_app_play_next_song" ||
               cmd == "reply_app_play_prev_song" || cmd == "reply_app_add_volume" || cmd == "reply_app_sub_volume" ||
               cmd == "reply_app_order_mode" || cmd == "reply_app_single_mode" || cmd == "reply_app_random_mode") {
        s->server_device_reply_handle(bev, root);
    } else {
        s->debug("未知命令：%s", cmd.c_str());
    }
}

bool Server::server_device_reply_handle(struct bufferevent *bev, const Json::Value &root)
{
    std::string cmd;
    if (!json_has_string(root, "cmd")) {
        Server::debug("JSON格式错误：缺少cmd字段或cmd类型非法");
        return false;
    }
    cmd = json_string_or_empty(root, "cmd");

    for (auto it = m_player_info->player_get_m_player_list()->begin();
         it != m_player_info->player_get_m_player_list()->end(); it++) {
        if (it->m_device_bev == bev) {
            bufferevent *app_bev = it->m_app_bev;
            if (server_send_data(app_bev, root) == false) {
                Server::debug("发送回复消息失败");
                return false;
            }
            Server::debug("[回复应用端]: %s", cmd.c_str());
            return true;
        }
    }
    return true;
}

bool Server::server_app_option(struct bufferevent *bev, Json::Value &root)
{
    std::string cmd;
    if (!json_has_string(root, "cmd")) {
        Server::debug("JSON格式错误：缺少cmd字段或cmd类型非法");
        return false;
    }
    cmd = json_string_or_empty(root, "cmd");

    bool is_online = false;
    auto it = m_player_info->player_get_m_player_list()->begin();
    for (it = m_player_info->player_get_m_player_list()->begin(); it != m_player_info->player_get_m_player_list()->end();
         it++) {
        if (it->m_app_bev == bev) {
            is_online = true;
            break;
        }
    }
    if (!is_online) {
        root["cmd"] = "reply_" + cmd;
        root["result"] = "offline";
        if (server_send_data(bev, root) == false) {
            Server::debug("发送回复消息失败");
            return false;
        }
        Server::debug("[回复应用端]: 嵌入式端不在线");
        return true;
    }
    if (server_send_data(it->m_device_bev, root) == false) {
        Server::debug("发送回复消息失败");
        return false;
    }
    Server::debug("[转发应用端命令]: %s", cmd.c_str());
    return true;
}

bool Server::server_get_music(struct bufferevent *bev, const Json::Value &root)
{
    std::string singer;
    if (!json_has_string(root, "singer")) {
        Server::debug("JSON格式错误：缺少singer字段或singer类型非法");
        return false;
    }

    singer = json_string_or_empty(root, "singer");
    std::vector<MusicFileInfo> all_valid_musics;
    collect_music_by_singer(singer, all_valid_musics);
    if (all_valid_musics.empty()) {
        reply_music_paths(this, bev, all_valid_musics);
        return false;
    }

    unsigned random_seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::shuffle(all_valid_musics.begin(), all_valid_musics.end(), std::default_random_engine(random_seed));
    reply_music_paths(this, bev, all_valid_musics);
    return true;
}

bool Server::server_search_music(struct bufferevent *bev, const Json::Value &root)
{
    Json::Value reply(Json::objectValue);
    Json::Value music(Json::arrayValue);
    std::string keyword;
    int page = 1;
    int page_size = DEFAULT_PAGE_SIZE;
    int total;
    int total_pages;
    int start;
    int end;
    std::vector<MusicFileInfo> matches;

    if (!json_has_string(root, "keyword")) {
        Server::debug("JSON格式错误：缺少keyword字段或keyword类型非法");
        return false;
    }
    keyword = json_string_or_empty(root, "keyword");
    if (json_has_int(root, "page")) {
        page = json_int_or_default(root, "page", page);
    }
    if (json_has_int(root, "page_size")) {
        page_size = json_int_or_default(root, "page_size", page_size);
    }
    if (page <= 0)
        page = 1;
    if (page_size <= 0)
        page_size = DEFAULT_PAGE_SIZE;

    collect_music_by_keyword(keyword, matches);
    total = static_cast<int>(matches.size());
    total_pages = (total == 0) ? 0 : ((total + page_size - 1) / page_size);
    start = (page - 1) * page_size;
    end = std::min(start + page_size, total);

    if (start < total) {
        for (int i = start; i < end; ++i) {
            Json::Value item(Json::objectValue);
            item["singer"] = matches[i].singer;
            item["song"] = matches[i].song;
            item["path"] = matches[i].path;
            music.append(item);
        }
    }

    reply["cmd"] = "reply_search_music";
    reply["result"] = "ok";
    reply["music"] = music;
    reply["page"] = page;
    reply["total_pages"] = total_pages;
    reply["total"] = total;
    bool ok = server_send_data(bev, reply);
    return ok;
}

bool Server::server_list_music(struct bufferevent *bev, const Json::Value &root)
{
    Json::Value reply(Json::objectValue);
    Json::Value music(Json::arrayValue);
    int page = 1;
    int page_size = DEFAULT_PAGE_SIZE;
    int total;
    int total_pages;
    int start;
    int end;

    if (json_has_int(root, "page")) {
        page = json_int_or_default(root, "page", page);
    }
    if (json_has_int(root, "page_size")) {
        page_size = json_int_or_default(root, "page_size", page_size);
    }
    if (page <= 0)
        page = 1;
    if (page_size <= 0)
        page_size = DEFAULT_PAGE_SIZE;

    ensure_list_music_cache();
    const std::vector<MusicFileInfo> &matches = g_list_music_cache;
    total = static_cast<int>(matches.size());
    total_pages = (total == 0) ? 0 : ((total + page_size - 1) / page_size);
    start = (page - 1) * page_size;
    end = std::min(start + page_size, total);

    if (start < total) {
        for (int i = start; i < end; ++i) {
            Json::Value item(Json::objectValue);
            item["singer"] = matches[i].singer;
            item["song"] = matches[i].song;
            item["path"] = matches[i].path;
            music.append(item);
        }
    }

    reply["cmd"] = "reply_list_music";
    reply["result"] = "ok";
    reply["music"] = music;
    reply["page"] = page;
    reply["total_pages"] = total_pages;
    reply["total"] = total;
    return server_send_data(bev, reply);
}

bool Server::server_send_data(struct bufferevent *bev, const Json::Value &root)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string json_text = Json::writeString(wb, root);
    unsigned int msg_len = static_cast<unsigned int>(json_text.size());
    std::vector<char> msg_buf(sizeof(int) + msg_len);
    memcpy(&msg_buf[0], &msg_len, sizeof(int));
    memcpy(&msg_buf[sizeof(int)], json_text.data(), msg_len);
    if (bufferevent_write(bev, &msg_buf[0], msg_buf.size()) != 0) {
        Server::debug("发送消息体失败");
        return false;
    }
    return true;
}

bool Server::server_read_data(struct bufferevent *bev, Json::Value *root)
{
    char len_buf[sizeof(int)] = {0};
    int read_len = 0;
    while (read_len < (int)sizeof(int)) {
        int n = bufferevent_read(bev, len_buf + read_len, sizeof(int) - read_len);
        if (n <= 0) {
            Server::debug("读取消息长度失败（连接关闭或错误）");
            return false;
        }
        read_len += n;
    }
    int msg_len = *(int *)len_buf;

    const int MAX_MSG_LEN = 1024 * 1024;
    if (msg_len <= 0 || msg_len > MAX_MSG_LEN) {
        Server::debug("无效消息长度：%d（允许范围：1-%d）", msg_len, MAX_MSG_LEN);
        return false;
    }

    std::string msg;
    msg.resize((size_t)msg_len);
    read_len = 0;
    while (read_len < msg_len) {
        int n = bufferevent_read(bev, &msg[read_len], msg_len - read_len);
        if (n <= 0) {
            Server::debug("读取消息体失败");
            return false;
        }
        read_len += n;
    }

    Json::Reader reader;
    if (!reader.parse(msg, *root)) {
        Server::debug("JSON解析失败：%s", reader.getFormattedErrorMessages().c_str());
        return false;
    }
    if (!root->isObject()) {
        Server::debug("JSON类型错误：必须是对象类型");
        return false;
    }
    return true;
}

void Server::event_cb(struct bufferevent *bev, short what, void *ctx)
{
    Server *s = (Server *)ctx;
    if (what & BEV_EVENT_EOF) {
        auto plist = s->m_player_info->player_get_m_player_list();
        for (auto it = plist->begin(); it != plist->end(); it++) {
            if (it->m_app_bev == bev) {
                Server::debug("[有APP下线了] APPID：%s", it->m_appid.c_str());
                it->m_appid.clear();
                if (it->m_app_bev != nullptr) {
                    bufferevent_free(it->m_app_bev);
                    it->m_app_bev = nullptr;
                }
                break;
            } else if (it->m_device_bev == bev) {
                Server::debug("[有音箱下线了] 音箱ID：%s", it->m_deviceid.c_str());
                if (it->m_device_bev != nullptr) {
                    bufferevent_free(it->m_device_bev);
                    it->m_device_bev = nullptr;
                }
                if (it->m_app_bev != nullptr) {
                    Json::Value json(Json::objectValue);
                    json["cmd"] = "device_offline";
                    s->server_send_data(it->m_app_bev, json);
                    bufferevent_free(it->m_app_bev);
                    it->m_app_bev = nullptr;
                }
                plist->erase(it);
                break;
            }
        }
    }
}

void Server::debug(const char *s, ...)
{
    va_list args;
    va_start(args, s);
    char buf[1024] = {0};
    vsnprintf(buf, sizeof(buf), s, args);
    va_end(args);

    time_t t = time(NULL);
    char time_buf[64] = {0};
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
    std::cout << "[" << time_buf << "] " << buf << std::endl;
}
