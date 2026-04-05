#include "server.h"
#include "app_log.h"
#include "music_service_client.h"
#include "music_downloader.h"
#include "music_remote_list.h"
#include "runtime_config.h"

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstdarg>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <iostream>
#include <netinet/in.h>
#include <random>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

struct MusicFileInfo {
    std::string singer;
    std::string song;
    std::string path;
};

std::string music_root_path(void)
{
    return server_runtime_config().music_root;
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

static void trim_keyword(std::string &kw)
{
    while (!kw.empty() && (kw.front() == ' ' || kw.front() == '\t' || kw.front() == '\n' || kw.front() == '\r')) {
        kw.erase(kw.begin());
    }
    while (!kw.empty() && (kw.back() == ' ' || kw.back() == '\t' || kw.back() == '\n' || kw.back() == '\r')) {
        kw.pop_back();
    }
}

std::string log_text_preview(const std::string &text, size_t max_len)
{
    if (text.size() <= max_len) {
        return text;
    }
    return text.substr(0, max_len) + "...";
}

void debug_music_items_preview(const char *label, const std::string &keyword, const Json::Value &items,
                               const Json::Value &result)
{
    Json::ArrayIndex i;
    std::string preview;
    const Json::ArrayIndex max_preview = 3;
    if (!items.isArray()) {
        Server::debug("[%s] keyword=%s result=%s items.size=0",
                      label, keyword.c_str(), result.asCString());
        return;
    }
    for (i = 0; i < items.size() && i < max_preview; ++i) {
        const Json::Value &item = items[i];
        std::string source = json_string_or_empty(item, "source");
        std::string id = json_string_or_empty(item, "id");
        std::string title = json_string_or_empty(item, "title");
        std::string subtitle = json_string_or_empty(item, "subtitle");
        if (!preview.empty()) {
            preview += " | ";
        }
        preview += source + "/" + id + "/" + log_text_preview(title, 32) + "/" + log_text_preview(subtitle, 32);
    }
    Server::debug("[%s] keyword=%s result=%s items.size=%u preview=%s",
                  label, keyword.c_str(), result.asCString(),
                  static_cast<unsigned>(items.size()), preview.c_str());
}

void debug_music_resolve_preview(const char *label, const Json::Value &request, const Json::Value &reply)
{
    std::string source = json_string_or_empty(reply, "source");
    if (source.empty()) {
        source = json_string_or_empty(request, "source");
    }
    std::string id = json_string_or_empty(reply, "id");
    if (id.empty()) {
        id = json_string_or_empty(request, "id");
    }
    if (id.empty()) {
        id = json_string_or_empty(request, "song_id");
    }
    Server::debug("[%s] source=%s id=%s result=%s title=%s subtitle=%s play_url=%s",
                  label,
                  source.c_str(),
                  id.c_str(),
                  json_string_or_empty(reply, "result").c_str(),
                  log_text_preview(json_string_or_empty(reply, "title"), 32).c_str(),
                  log_text_preview(json_string_or_empty(reply, "subtitle"), 32).c_str(),
                  json_string_or_empty(reply, "play_url").c_str());
}

static void fill_list_music_from_local_cache(Json::Value &music, int page, int page_size, int &total,
                                             int &total_pages)
{
    int start;
    int end;
    ensure_list_music_cache();
    const std::vector<MusicFileInfo> &matches = g_list_music_cache;
    total = static_cast<int>(matches.size());
    total_pages = (total == 0) ? 0 : ((total + page_size - 1) / page_size);
    start = (page - 1) * page_size;
    end = std::min(start + page_size, total);
    music = Json::Value(Json::arrayValue);
    if (start < total) {
        for (int i = start; i < end; ++i) {
            Json::Value item(Json::objectValue);
            item["singer"] = matches[i].singer;
            item["song"] = matches[i].song;
            item["path"] = matches[i].path;
            music.append(item);
        }
    }
}

static void fill_list_music_from_local_keyword(const std::string &keyword, Json::Value &music, int page,
                                               int page_size, int &total, int &total_pages)
{
    int start;
    int end;
    std::vector<MusicFileInfo> matches;
    collect_music_by_keyword(keyword, matches);
    total = static_cast<int>(matches.size());
    total_pages = (total == 0) ? 0 : ((total + page_size - 1) / page_size);
    start = (page - 1) * page_size;
    end = std::min(start + page_size, total);
    music = Json::Value(Json::arrayValue);
    if (start < total) {
        for (int i = start; i < end; ++i) {
            Json::Value item(Json::objectValue);
            item["singer"] = matches[i].singer;
            item["song"] = matches[i].song;
            item["path"] = matches[i].path;
            music.append(item);
        }
    }
}

std::string reply_cmd_for(const std::string &cmd)
{
    return cmd + ".reply";
}

void fill_music_service_reply_cmd(Json::Value &reply, const std::string &cmd)
{
    reply["cmd"] = reply_cmd_for(cmd);
}

bool normalize_music_service_items(Json::Value &items, const char *fallback_kind)
{
    if (!items.isArray()) {
        return false;
    }
    for (Json::ArrayIndex i = 0; i < items.size(); ++i) {
        Json::Value &item = items[i];
        if (!item.isObject()) {
            return false;
        }
        if (!item.isMember("kind") || !item["kind"].isString()) {
            item["kind"] = fallback_kind;
        }
        if (!item.isMember("source")) {
            item["source"] = "";
        }
        if (!item.isMember("id")) {
            item["id"] = "";
        }
        if (!item.isMember("title")) {
            item["title"] = "";
        }
        if (!item.isMember("subtitle")) {
            item["subtitle"] = "";
        }
        if (!item.isMember("cover")) {
            item["cover"] = "";
        }
    }
    return true;
}

bool reply_music_search_song(Server *server, struct bufferevent *bev, const Json::Value &root,
                             const std::string &cmd)
{
    Json::Value reply(Json::objectValue);
    Json::Value items(Json::arrayValue);
    music_search_result_t res;
    std::string keyword = json_string_or_empty(root, "keyword");
    std::string platform = json_string_or_empty(root, "source");
    int page = json_has_int(root, "page") ? json_int_or_default(root, "page", 1) : 1;
    int page_size = json_has_int(root, "page_size") ? json_int_or_default(root, "page_size", DEFAULT_PAGE_SIZE)
                                                    : DEFAULT_PAGE_SIZE;
    const ServerRuntimeConfig &cfg = server_runtime_config();
    fill_music_service_reply_cmd(reply, cmd);

    if (page <= 0) {
        page = 1;
    }
    if (page_size <= 0) {
        page_size = DEFAULT_PAGE_SIZE;
    }

    trim_keyword(keyword);
    trim_keyword(platform);
    if (platform.empty()) {
        platform = cfg.legacy_platform;
    }
    music_remote_apply_source_hints(keyword, platform, platform);
    if (keyword.empty() || music_remote_keyword_is_vague(keyword)) {
        reply["result"] = "fail";
        reply["kind"] = "song";
        reply["items"] = items;
        reply["page"] = page;
        reply["total"] = 0;
        reply["total_pages"] = 0;
        reply["online_search_enabled"] = music_api_configured();
        return server->server_send_data(bev, reply);
    }
    if (!music_api_configured()) {
        reply["result"] = "fail";
        reply["kind"] = "song";
        reply["items"] = items;
        reply["page"] = page;
        reply["total"] = 0;
        reply["total_pages"] = 0;
        reply["online_search_enabled"] = false;
        return server->server_send_data(bev, reply);
    }

    memset(&res, 0, sizeof(res));
    if (music_search_page(keyword.c_str(), platform.c_str(), (uint32_t)page, (uint32_t)page_size, &res) != Ok) {
        reply["result"] = "fail";
        reply["kind"] = "song";
        reply["items"] = items;
        reply["page"] = page;
        reply["total"] = 0;
        reply["total_pages"] = 0;
        reply["online_search_enabled"] = true;
        Server::debug("[music.search.song] source=%s keyword=%s result=fail", platform.c_str(), keyword.c_str());
        return server->server_send_data(bev, reply);
    }

    for (size_t i = 0; i < res.count; ++i) {
        const music_info_t *info = &res.results[i];
        Json::Value item(Json::objectValue);
        item["kind"] = "song";
        item["source"] = std::string(info->source);
        item["id"] = std::string(info->id);
        item["title"] = std::string(info->name);
        item["subtitle"] = std::string(info->artist);
        item["cover"] = "";
        items.append(item);
    }

    reply["result"] = items.size() > 0 ? "ok" : "empty";
    reply["kind"] = "song";
    reply["items"] = items;
    reply["page"] = static_cast<int>(res.page);
    reply["total"] = static_cast<int>(res.total);
    reply["total_pages"] = static_cast<int>(res.total_pages);
    reply["online_search_enabled"] = true;
    debug_music_items_preview(cmd.c_str(), keyword, items, reply["result"]);
    Server::debug("[music.search.song] source=%s keyword=%s total=%u page=%u page_size=%u",
                  platform.c_str(), keyword.c_str(), res.total, res.page, res.page_size);
    music_free_search_result(&res);
    return server->server_send_data(bev, reply);
}

bool proxy_music_service_list(Server *server, struct bufferevent *bev, const Json::Value &root,
                              const std::string &cmd, const char *path, const char *kind)
{
    Json::Value request(Json::objectValue);
    Json::Value response(Json::objectValue);
    Json::Value reply(Json::objectValue);
    std::string error_message;

    request["keyword"] = json_string_or_empty(root, "keyword");
    request["source"] = json_string_or_empty(root, "source");
    request["page"] = json_has_int(root, "page") ? json_int_or_default(root, "page", 1) : 1;
    request["page_size"] = json_has_int(root, "page_size") ? json_int_or_default(root, "page_size", DEFAULT_PAGE_SIZE)
                                                           : DEFAULT_PAGE_SIZE;
    if (request["page"].asInt() <= 0) {
        request["page"] = 1;
    }
    if (request["page_size"].asInt() <= 0) {
        request["page_size"] = DEFAULT_PAGE_SIZE;
    }

    fill_music_service_reply_cmd(reply, cmd);
    if (!music_service_post_json(path, request, &response, &error_message)) {
        reply["result"] = "fail";
        reply["message"] = error_message;
        return server->server_send_data(bev, reply);
    }

    reply["result"] = json_string_or_empty(response, "result");
    if (reply["result"].asString().empty()) {
        reply["result"] = "fail";
    }
    reply["kind"] = json_string_or_empty(response, "kind");
    if (reply["kind"].asString().empty()) {
        reply["kind"] = kind;
    }
    reply["items"] = response.isMember("items") ? response["items"] : Json::Value(Json::arrayValue);
    if (!normalize_music_service_items(reply["items"], kind)) {
        reply["result"] = "fail";
        reply["items"] = Json::Value(Json::arrayValue);
    }
    reply["page"] = json_int_or_default(response, "page", request["page"].asInt());
    reply["total"] = json_int_or_default(response, "total", 0);
    reply["total_pages"] = json_int_or_default(response, "total_pages", 0);
    debug_music_items_preview(cmd.c_str(), json_string_or_empty(root, "keyword"), reply["items"], reply["result"]);
    return server->server_send_data(bev, reply);
}

bool proxy_music_service_detail(Server *server, struct bufferevent *bev, const Json::Value &root,
                                const std::string &cmd, const char *path, const char *kind)
{
    Json::Value request(Json::objectValue);
    Json::Value response(Json::objectValue);
    Json::Value reply(Json::objectValue);
    std::string error_message;

    request["id"] = json_string_or_empty(root, "id");
    request["source"] = json_string_or_empty(root, "source");
    fill_music_service_reply_cmd(reply, cmd);
    if (request["id"].asString().empty()) {
        reply["result"] = "fail";
        return server->server_send_data(bev, reply);
    }
    if (!music_service_post_json(path, request, &response, &error_message)) {
        reply["result"] = "fail";
        reply["message"] = error_message;
        return server->server_send_data(bev, reply);
    }
    reply["result"] = json_string_or_empty(response, "result");
    if (reply["result"].asString().empty()) {
        reply["result"] = "fail";
    }
    reply["kind"] = kind;
    reply["items"] = response.isMember("items") ? response["items"] : Json::Value(Json::arrayValue);
    if (!normalize_music_service_items(reply["items"], kind)) {
        reply["result"] = "fail";
        reply["items"] = Json::Value(Json::arrayValue);
    }
    reply["page"] = json_int_or_default(response, "page", 1);
    reply["total"] = json_int_or_default(response, "total", 0);
    reply["total_pages"] = json_int_or_default(response, "total_pages", 0);
    debug_music_items_preview(cmd.c_str(), json_string_or_empty(root, "id"), reply["items"], reply["result"]);
    return server->server_send_data(bev, reply);
}

bool proxy_music_service_resolve(Server *server, struct bufferevent *bev, const Json::Value &root,
                                 const std::string &cmd)
{
    Json::Value request(Json::objectValue);
    Json::Value response(Json::objectValue);
    Json::Value reply(Json::objectValue);
    std::string error_message;

    request["source"] = json_string_or_empty(root, "source");
    request["id"] = json_string_or_empty(root, "id");
    if (request["id"].asString().empty()) {
        request["id"] = json_string_or_empty(root, "song_id");
    }

    fill_music_service_reply_cmd(reply, cmd);
    if (request["id"].asString().empty()) {
        reply["result"] = "fail";
        return server->server_send_data(bev, reply);
    }
    if (!music_service_post_json("/music/url/resolve", request, &response, &error_message)) {
        reply["result"] = "fail";
        reply["message"] = error_message;
        return server->server_send_data(bev, reply);
    }

    reply["result"] = json_string_or_empty(response, "result");
    if (reply["result"].asString().empty()) {
        reply["result"] = "fail";
    }
    reply["kind"] = json_string_or_empty(response, "kind");
    if (reply["kind"].asString().empty()) {
        reply["kind"] = "song";
    }
    reply["source"] = json_string_or_empty(response, "source");
    reply["id"] = json_string_or_empty(response, "id");
    reply["title"] = json_string_or_empty(response, "title");
    reply["subtitle"] = json_string_or_empty(response, "subtitle");
    reply["cover"] = json_string_or_empty(response, "cover");
    reply["play_url"] = json_string_or_empty(response, "play_url");
    debug_music_resolve_preview(cmd.c_str(), request, reply);
    return server->server_send_data(bev, reply);
}

bool reply_music_transport_report(Server *server, struct bufferevent *bev, const Json::Value &root,
                                  const std::string &cmd)
{
    Json::Value reply(Json::objectValue);
    fill_music_service_reply_cmd(reply, cmd);
    reply["result"] = "ok";
    reply["state"] = json_string_or_empty(root, "state");
    reply["current_id"] = json_string_or_empty(root, "current_id");
    return server->server_send_data(bev, reply);
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

    for (int attempt = 0; attempt < 2; ++attempt) {
        struct evconnlistener *listener = evconnlistener_new_bind(
            m_eventbase, listener_cb, this, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, 5,
            (struct sockaddr *)&server_info, socketlen);
        if (listener != NULL) {
            event_base_dispatch(m_eventbase);
            evconnlistener_free(listener);
            return;
        }
        if (errno != EADDRINUSE || attempt > 0) {
            perror("evconnlistener_new_bind");
            return;
        }
        char cmd[96];
        snprintf(cmd, sizeof(cmd), "fuser -k %d/tcp >/dev/null 2>&1", port);
        (void)system(cmd);
        usleep(200000);
    }
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
    for (;;) {
        Json::Value root;
        std::string cmd;
        int rr = s->server_try_read_one_json(bev, &root);
        if (rr == 0) {
            return;
        }
        if (rr < 0) {
            continue;
        }
        if (!json_has_string(root, "cmd")) {
            Server::debug("JSON格式错误：缺少cmd字段或cmd类型非法");
            continue;
        }

        cmd = json_string_or_empty(root, "cmd");
    if (cmd == "get_music") {
        s->debug("[消息类型] 嵌入式端获取音乐列表");
        s->server_get_music(bev, root);
    } else if (cmd == "list_music") {
        {
            std::string kw = json_string_or_empty(root, "keyword");
            trim_keyword(kw);
            if (kw.empty() || music_remote_keyword_is_vague(kw)) {
                s->debug("[消息类型] list_music 本地伪随机分页");
            } else {
                s->debug("[消息类型] list_music 关键词[%s]", kw.c_str());
            }
        }
        s->server_list_music(bev, root);
    } else if (cmd == "get_play_url") {
        s->debug("[消息类型] get_play_url");
        s->server_get_play_url(bev, root);
    } else if (cmd == "resolve_music") {
        s->debug("[消息类型] resolve_music");
        s->server_resolve_music(bev, root);
    } else if (cmd == "search_music") {
        s->debug("[消息类型] 搜索音乐");
        s->server_search_music(bev, root);
    } else if (cmd == "music.search.song") {
        s->debug("[消息类型] music.search.song");
        if (music_api_configured()) {
            reply_music_search_song(s, bev, root, cmd);
        } else {
            proxy_music_service_list(s, bev, root, cmd, "/music/search/song", "song");
        }
    } else if (cmd == "music.search.playlist") {
        s->debug("[消息类型] music.search.playlist");
        proxy_music_service_list(s, bev, root, cmd, "/music/search/playlist", "playlist");
    } else if (cmd == "music.search.artist") {
        s->debug("[消息类型] music.search.artist");
        proxy_music_service_list(s, bev, root, cmd, "/music/search/artist", "artist");
    } else if (cmd == "music.playlist.detail") {
        s->debug("[消息类型] music.playlist.detail");
        proxy_music_service_detail(s, bev, root, cmd, "/music/playlist/detail", "song");
    } else if (cmd == "music.artist.hot") {
        s->debug("[消息类型] music.artist.hot");
        proxy_music_service_detail(s, bev, root, cmd, "/music/artist/hot", "song");
    } else if (cmd == "music.url.resolve") {
        s->debug("[消息类型] music.url.resolve");
        proxy_music_service_resolve(s, bev, root, cmd);
    } else if (cmd == "music.transport.report") {
        s->debug("[消息类型] music.transport.report");
        reply_music_transport_report(s, bev, root, cmd);
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

bool Server::server_get_play_url(struct bufferevent *bev, const Json::Value &root)
{
    Json::Value reply(Json::objectValue);
    reply["cmd"] = "reply_get_play_url";
    if (!music_api_configured()) {
        reply["result"] = "disabled";
        return server_send_data(bev, reply);
    }
    if (!json_has_string(root, "source") || !json_has_string(root, "song_id")) {
        reply["result"] = "fail";
        return server_send_data(bev, reply);
    }
    std::string src = json_string_or_empty(root, "source");
    std::string sid = json_string_or_empty(root, "song_id");
    if (src.empty() || sid.empty()) {
        reply["result"] = "fail";
        return server_send_data(bev, reply);
    }
    const std::string &qual = server_runtime_config().legacy_quality;
    char *url = music_get_url(src.c_str(), sid.c_str(), qual.c_str());
    if (url != NULL && url[0] != '\0') {
        reply["result"] = "ok";
        reply["play_url"] = std::string(url);
        Server::debug("[get_play_url] source=%s song_id=%s play_url=%s", src.c_str(), sid.c_str(), url);
        music_free_string(url);
    } else {
        reply["result"] = "fail";
        if (url != NULL) {
            music_free_string(url);
        }
    }
    return server_send_data(bev, reply);
}

bool Server::server_resolve_music(struct bufferevent *bev, const Json::Value &root)
{
    Json::Value reply(Json::objectValue);
    reply["cmd"] = "reply_resolve_music";
    std::string kw = json_string_or_empty(root, "keyword");
    trim_keyword(kw);
    if (kw.empty() || music_remote_keyword_is_vague(kw)) {
        reply["result"] = "fail";
        return server_send_data(bev, reply);
    }
    if (!music_api_configured()) {
        reply["result"] = "disabled";
        reply["online_search_enabled"] = false;
        return server_send_data(bev, reply);
    }
    music_resolve_result_t r;
    memset(&r, 0, sizeof(r));
    const ServerRuntimeConfig &cfg = server_runtime_config();
    std::string plat = cfg.legacy_platform;
    music_remote_apply_source_hints(kw, plat, cfg.legacy_platform);
    if (kw.empty() || music_remote_keyword_is_vague(kw)) {
        reply["result"] = "fail";
        return server_send_data(bev, reply);
    }
    if (music_resolve_keyword(kw.c_str(), plat.c_str(), cfg.legacy_quality.c_str(), &r) != Ok) {
        reply["result"] = "fail";
        return server_send_data(bev, reply);
    }
    reply["result"] = "ok";
    reply["online_search_enabled"] = true;
    reply["play_url"] = std::string(r.play_url);
    reply["source"] = std::string(r.source);
    reply["song_id"] = std::string(r.song_id);
    reply["singer"] = std::string(r.singer);
    reply["song"] = std::string(r.song);
    Server::debug("[resolve_music] keyword=%s singer=%s song=%s play_url=%s", kw.c_str(), r.singer, r.song,
                  r.play_url);
    music_free_resolve_result(&r);
    return server_send_data(bev, reply);
}

bool Server::server_list_music(struct bufferevent *bev, const Json::Value &root)
{
    Json::Value reply(Json::objectValue);
    Json::Value music(Json::arrayValue);
    int page = 1;
    int page_size = DEFAULT_PAGE_SIZE;
    int total = 0;
    int total_pages = 0;
    std::string keyword;

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

    keyword = json_string_or_empty(root, "keyword");
    trim_keyword(keyword);

    if (keyword.empty() || music_remote_keyword_is_vague(keyword)) {
        fill_list_music_from_local_cache(music, page, page_size, total, total_pages);
        reply["online_search_enabled"] = true;
    } else {
        if (!music_api_configured()) {
            reply["online_search_enabled"] = false;
            total = 0;
            total_pages = 0;
            music = Json::Value(Json::arrayValue);
        } else {
            reply["online_search_enabled"] = true;
            int remote_total = 0;
            int remote_tp = 0;
            if (music_remote_list_music_page(keyword, page, page_size, music, remote_total, remote_tp) &&
                music.size() > 0) {
                total = remote_total;
                total_pages = remote_tp;
                for (Json::ArrayIndex i = 0; i < music.size(); ++i) {
                    const Json::Value &it = music[i];
                    if (it.isMember("play_url") && it["play_url"].isString()) {
                        const std::string &pu = it["play_url"].asString();
                        if (!pu.empty()) {
                            Server::debug("[list_music] keyword=%s singer=%s song=%s play_url=%s",
                                          keyword.c_str(), it["singer"].asCString(),
                                          it["song"].asCString(), pu.c_str());
                        }
                    }
                }
            } else {
                fill_list_music_from_local_keyword(keyword, music, page, page_size, total, total_pages);
            }
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

int Server::server_try_read_one_json(struct bufferevent *bev, Json::Value *root)
{
    struct evbuffer *in;
    const int MAX_MSG_LEN = 1024 * 1024;
    char len_buf[sizeof(int)];
    int msg_len;
    size_t nbuf;

    if (bev == NULL || root == NULL) {
        return -1;
    }
    in = bufferevent_get_input(bev);
    nbuf = evbuffer_get_length(in);
    if (nbuf < sizeof(int)) {
        return 0;
    }
    evbuffer_copyout(in, len_buf, sizeof(len_buf));
    memcpy(&msg_len, len_buf, sizeof(msg_len));

    if (msg_len <= 0 || msg_len > MAX_MSG_LEN) {
        Server::debug("无效消息长度：%d（允许范围：1-%d）", msg_len, MAX_MSG_LEN);
        evbuffer_drain(in, sizeof(int));
        return -1;
    }
    if (nbuf < sizeof(int) + (size_t)msg_len) {
        return 0;
    }

    evbuffer_drain(in, sizeof(int));
    std::string msg;
    msg.resize((size_t)msg_len);
    evbuffer_remove(in, &msg[0], (size_t)msg_len);

    Json::Reader reader;
    if (!reader.parse(msg, *root)) {
        Server::debug("JSON解析失败：%s", reader.getFormattedErrorMessages().c_str());
        return -1;
    }
    if (!root->isObject()) {
        Server::debug("JSON类型错误：必须是对象类型");
        return -1;
    }
    return 1;
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
    char buf[8192] = {0};
    vsnprintf(buf, sizeof(buf), s, args);
    va_end(args);

    time_t t = time(NULL);
    char time_buf[64] = {0};
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
    std::cout << "[" << time_buf << "] " << buf << std::endl;
    app_log_emit(time_buf, buf);
}
