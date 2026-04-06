#include "music_source_manager.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "music_source_server.h"
#include "player_constants.h"
#include "runtime_config.h"
#include "debug_log.h"

#define TAG "MUSIC-SERVER"

static const char *music_source_server_effective_source(const char *source)
{
    if (source != NULL && source[0] != '\0') {
        return source;
    }
    return player_runtime_music_search_source();
}

static void music_source_server_free_result(MusicSourceResult *result);

static void server_result_reset(MusicSourceResult *result)
{
    if (result == NULL) return;
    memset(result, 0, sizeof(*result));
}

static void server_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) return;
    dst[0] = '\0';
    if (src == NULL) return;
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void server_trim_spaces(char *text)
{
    char *start;
    size_t len;

    if (text == NULL || text[0] == '\0') return;
    start = text;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }
    len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t')) {
        text[--len] = '\0';
    }
}

static void server_parse_song_meta(const char *raw_song, const char *song_id, const char *raw_singer,
                                   char *song_name, size_t song_name_size,
                                   char *singer, size_t singer_size)
{
    char base[MUSIC_MAX_NAME];
    char parsed_singer[SINGER_MAX_NAME];
    const char *name_src;
    const char *slash;
    char *ext;
    char *sep;

    if (song_name == NULL || song_name_size == 0 || singer == NULL || singer_size == 0) return;
    song_name[0] = '\0';
    singer[0] = '\0';
    parsed_singer[0] = '\0';

    name_src = (raw_song != NULL && raw_song[0] != '\0') ? raw_song : song_id;
    if (name_src == NULL) {
        return;
    }
    slash = strrchr(name_src, '/');
    if (slash != NULL && slash[1] != '\0') {
        name_src = slash + 1;
    }
    server_copy_text(base, sizeof(base), name_src);
    ext = strrchr(base, '.');
    if (ext != NULL) {
        *ext = '\0';
    }
    sep = strstr(base, " - ");
    if (sep != NULL && sep != base && sep[3] != '\0') {
        *sep = '\0';
        server_copy_text(song_name, song_name_size, base);
        server_copy_text(parsed_singer, sizeof(parsed_singer), sep + 3);
        server_trim_spaces(song_name);
        server_trim_spaces(parsed_singer);
    } else {
        server_copy_text(song_name, song_name_size, base);
        server_trim_spaces(song_name);
    }

    server_copy_text(singer, singer_size, raw_singer);
    server_trim_spaces(singer);
    if (singer[0] == '\0' && parsed_singer[0] != '\0') {
        server_copy_text(singer, singer_size, parsed_singer);
    }
}

static void server_sync_item_aliases(MusicSourceItem *item)
{
    if (item == NULL) return;
    if (item->id[0] == '\0' && item->song_id[0] != '\0') {
        server_copy_text(item->id, sizeof(item->id), item->song_id);
    }
    if (item->song_id[0] == '\0' && item->id[0] != '\0') {
        server_copy_text(item->song_id, sizeof(item->song_id), item->id);
    }
    if (item->title[0] == '\0' && item->song_name[0] != '\0') {
        server_copy_text(item->title, sizeof(item->title), item->song_name);
    }
    if (item->song_name[0] == '\0' && item->title[0] != '\0') {
        server_copy_text(item->song_name, sizeof(item->song_name), item->title);
    }
    if (item->subtitle[0] == '\0' && item->singer[0] != '\0') {
        server_copy_text(item->subtitle, sizeof(item->subtitle), item->singer);
    }
    if (item->singer[0] == '\0' && item->subtitle[0] != '\0') {
        server_copy_text(item->singer, sizeof(item->singer), item->subtitle);
    }
}

static void server_log_result_preview(const char *label, const char *keyword, const MusicSourceResult *result)
{
    if (result == NULL) {
        return;
    }
    if (result->count > 0) {
        const MusicSourceItem *first = &result->items[0];
        LOGI(TAG, "%s keyword=%s count=%d first=%s/%s/%s/%s",
             label,
             keyword != NULL ? keyword : "",
             result->count,
             first->source,
             first->id[0] != '\0' ? first->id : first->song_id,
             first->title[0] != '\0' ? first->title : first->song_name,
             first->subtitle[0] != '\0' ? first->subtitle : first->singer);
    } else {
        LOGI(TAG, "%s keyword=%s count=0", label, keyword != NULL ? keyword : "");
    }
}

static const char *music_server_ip(void)
{
    return player_runtime_server_ip();
}

static int music_server_port(void)
{
    return player_runtime_server_port();
}

static int server_connect_once(void)
{
    int fd;
    struct sockaddr_in addr;
    int flags;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(music_server_port());
    addr.sin_addr.s_addr = inet_addr(music_server_ip());

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        close(fd);
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        if (errno != EINPROGRESS) {
            close(fd);
            return -1;
        }
        {
            fd_set wfds;
            struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
            int so_error = 0;
            socklen_t so_error_len = sizeof(so_error);
            int sel_ret;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            sel_ret = select(fd + 1, NULL, &wfds, NULL, &tv);
            if (sel_ret <= 0 ||
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) != 0 ||
                so_error != 0) {
                close(fd);
                return -1;
            }
        }
    }
    if (fcntl(fd, F_SETFL, flags) != 0) {
        close(fd);
        return -1;
    }
    {
        /* 与 smart-speaker-server → music-service 超时一致，避免酷我搜索未返回就断读 */
        struct timeval tv = {.tv_sec = 30, .tv_usec = 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
    return fd;
}

static int server_read_full(int fd, void *buf, size_t size)
{
    size_t done = 0;
    while (done < size) {
        ssize_t n = read(fd, (char *)buf + done, size - done);
        if (n <= 0) return -1;
        done += (size_t)n;
    }
    return 0;
}

static int server_write_full(int fd, const void *buf, size_t size)
{
    size_t done = 0;
    while (done < size) {
        ssize_t n = write(fd, (const char *)buf + done, size - done);
        if (n <= 0) return -1;
        done += (size_t)n;
    }
    return 0;
}

static int server_send_request(int fd, json_object *request)
{
    const char *payload;
    unsigned int len;
    payload = json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN);
    if (payload == NULL) return -1;
    len = (unsigned int)strlen(payload);
    if (server_write_full(fd, &len, sizeof(len)) != 0) return -1;
    if (server_write_full(fd, payload, len) != 0) return -1;
    return 0;
}

static int server_recv_response(int fd, char **payload_out)
{
    unsigned int len = 0;
    char *payload;
    if (payload_out == NULL) return -1;
    *payload_out = NULL;
    if (server_read_full(fd, &len, sizeof(len)) != 0) return -1;
    if (len == 0 || len > 1024 * 1024) return -1;
    payload = (char *)calloc((size_t)len + 1, 1);
    if (payload == NULL) return -1;
    if (server_read_full(fd, payload, len) != 0) {
        free(payload);
        return -1;
    }
    payload[len] = '\0';
    *payload_out = payload;
    return 0;
}

int music_source_server_parse_music_item(json_object *item_obj, MusicSourceItem *item)
{
    json_object *value;
    const char *raw_song = NULL;
    const char *raw_singer = NULL;
    char parsed_title[MUSIC_MAX_NAME];
    char parsed_subtitle[SINGER_MAX_NAME];
    if (item_obj == NULL || item == NULL) return -1;
    memset(item, 0, sizeof(*item));
    server_copy_text(item->source, sizeof(item->source), "server");
    parsed_title[0] = '\0';
    parsed_subtitle[0] = '\0';

    if (json_object_object_get_ex(item_obj, "source", &value)) {
        const char *s = json_object_get_string(value);
        if (s != NULL && s[0] != '\0') {
            server_copy_text(item->source, sizeof(item->source), s);
        }
    }
    if (json_object_object_get_ex(item_obj, "id", &value)) {
        server_copy_text(item->id, sizeof(item->id), json_object_get_string(value));
    }
    if (json_object_object_get_ex(item_obj, "song_id", &value)) {
        server_copy_text(item->song_id, sizeof(item->song_id), json_object_get_string(value));
    }
    if (json_object_object_get_ex(item_obj, "path", &value)) {
        const char *p = json_object_get_string(value);
        if (p != NULL && p[0] != '\0') {
            if (item->song_id[0] == '\0') {
                server_copy_text(item->song_id, sizeof(item->song_id), p);
            }
            if (item->id[0] == '\0') {
                server_copy_text(item->id, sizeof(item->id), p);
            }
        }
    }
    if (json_object_object_get_ex(item_obj, "title", &value)) {
        server_copy_text(item->title, sizeof(item->title), json_object_get_string(value));
    }
    if (json_object_object_get_ex(item_obj, "subtitle", &value)) {
        server_copy_text(item->subtitle, sizeof(item->subtitle), json_object_get_string(value));
    }
    if (json_object_object_get_ex(item_obj, "song", &value)) {
        raw_song = json_object_get_string(value);
    }
    if (json_object_object_get_ex(item_obj, "singer", &value)) {
        raw_singer = json_object_get_string(value);
    }
    if (json_object_object_get_ex(item_obj, "play_url", &value)) {
        server_copy_text(item->play_url, sizeof(item->play_url), json_object_get_string(value));
    }
    server_parse_song_meta(raw_song, item->id[0] != '\0' ? item->id : item->song_id, raw_singer,
                           parsed_title, sizeof(parsed_title),
                           parsed_subtitle, sizeof(parsed_subtitle));
    if (item->title[0] == '\0') {
        server_copy_text(item->title, sizeof(item->title), parsed_title);
    }
    if (item->subtitle[0] == '\0') {
        server_copy_text(item->subtitle, sizeof(item->subtitle), parsed_subtitle);
    }
    server_sync_item_aliases(item);
    if (item->title[0] == '\0') {
        return -1;
    }
    if (item->id[0] == '\0') {
        server_copy_text(item->id, sizeof(item->id), item->title);
    }
    server_sync_item_aliases(item);
    return 0;
}

static int music_source_server_list_page_cmd(const char *cmd, const char *log_label,
                                             const char *keyword, const char *source,
                                             int page, int page_size,
                                             MusicSourceResult *result)
{
    int fd;
    int ret = -1;
    char *payload = NULL;
    json_object *request = NULL;
    json_object *root = NULL;
    json_object *music = NULL;
    json_object *value = NULL;
    int i;
    if (page <= 0 || page_size <= 0 || result == NULL) {
        return -1;
    }
    server_result_reset(result);
    fd = server_connect_once();
    if (fd < 0) return -1;

    request = json_object_new_object();
    json_object_object_add(request, "cmd", json_object_new_string(cmd));
    json_object_object_add(request, "page", json_object_new_int(page));
    json_object_object_add(request, "page_size", json_object_new_int(page_size));
    if (keyword != NULL && keyword[0] != '\0') {
        json_object_object_add(request, "keyword", json_object_new_string(keyword));
    }
    {
        const char *src_eff = music_source_server_effective_source(source);
        json_object_object_add(request, "source", json_object_new_string(src_eff));
    }
    if (server_send_request(fd, request) != 0) goto done;
    if (server_recv_response(fd, &payload) != 0) goto done;

    root = json_tokener_parse(payload);
    if (root == NULL) goto done;
    if (!json_object_object_get_ex(root, "result", &value) ||
        strcmp(json_object_get_string(value), "ok") != 0) {
        ret = 0;
        goto done;
    }
    if (json_object_object_get_ex(root, "total_pages", &value)) {
        result->total_pages = json_object_get_int(value);
    }
    if (json_object_object_get_ex(root, "total", &value)) {
        result->total = json_object_get_int(value);
    }
    result->current_page = page;

    if (json_object_object_get_ex(root, "online_search_enabled", &value)) {
        if (json_object_get_type(value) == json_type_boolean && !json_object_get_boolean(value)) {
            result->online_search_disabled = 1;
        }
    }

    if (!json_object_object_get_ex(root, "items", &music) || !json_object_is_type(music, json_type_array)) {
        if (!json_object_object_get_ex(root, "music", &music) || !json_object_is_type(music, json_type_array)) {
            ret = 0;
            goto done;
        }
    }
    result->count = json_object_array_length(music);
    if (result->count > 0) {
        result->items = (MusicSourceItem *)calloc((size_t)result->count, sizeof(MusicSourceItem));
        if (result->items == NULL) goto done;
        for (i = 0; i < result->count; ++i) {
            if (music_source_server_parse_music_item(json_object_array_get_idx(music, i), &result->items[i]) != 0) {
                free(result->items);
                result->items = NULL;
                result->count = 0;
                goto done;
            }
        }
    }
    server_log_result_preview(log_label, keyword, result);
    ret = 0;

done:
    if (request != NULL) json_object_put(request);
    if (root != NULL) json_object_put(root);
    free(payload);
    close(fd);
    return ret;
}

int music_source_server_list_music_page(const char *keyword, const char *source,
                                        int page, int page_size, MusicSourceResult *result)
{
    LOGI(TAG, "search.song source=%s keyword=%s page=%d page_size=%d",
         music_source_server_effective_source(source), keyword != NULL ? keyword : "", page, page_size);
    return music_source_server_list_page_cmd("music.search.song", "search.song.reply", keyword, source,
                                             page, page_size, result);
}

int music_source_server_list_playlist_page(const char *keyword, const char *source,
                                           int page, int page_size, MusicSourceResult *result)
{
    LOGI(TAG, "search.playlist source=%s keyword=%s page=%d page_size=%d",
         music_source_server_effective_source(source), keyword != NULL ? keyword : "", page, page_size);
    return music_source_server_list_page_cmd("music.search.playlist", "search.playlist.reply", keyword, source,
                                             page, page_size, result);
}

int music_source_server_search_playlist_page(const char *keyword, const char *source,
                                             int page, int page_size, MusicSourceResult *out_result)
{
    return music_source_server_list_playlist_page(keyword, source, page, page_size, out_result);
}

static int music_source_server_search(const char *keyword, int page, int page_size, MusicSourceResult *result)
{
    return music_source_server_list_music_page(keyword, NULL, page, page_size, result);
}

static int music_source_server_playlist_detail(const char *playlist_id, const char *source, MusicSourceResult *result)
{
    int fd;
    int ret = -1;
    char *payload = NULL;
    json_object *request = NULL;
    json_object *root = NULL;
    json_object *music = NULL;
    json_object *value = NULL;
    int i;

    if (playlist_id == NULL || playlist_id[0] == '\0' || result == NULL) {
        return -1;
    }
    server_result_reset(result);
    fd = server_connect_once();
    if (fd < 0) return -1;

    request = json_object_new_object();
    json_object_object_add(request, "cmd", json_object_new_string("music.playlist.detail"));
    json_object_object_add(request, "id", json_object_new_string(playlist_id));
    if (source != NULL && source[0] != '\0') {
        json_object_object_add(request, "source", json_object_new_string(source));
    }
    if (server_send_request(fd, request) != 0) goto done;
    if (server_recv_response(fd, &payload) != 0) goto done;

    root = json_tokener_parse(payload);
    if (root == NULL) goto done;
    if (!json_object_object_get_ex(root, "result", &value) ||
        strcmp(json_object_get_string(value), "ok") != 0) {
        ret = 0;
        goto done;
    }
    if (json_object_object_get_ex(root, "total_pages", &value)) {
        result->total_pages = json_object_get_int(value);
    }
    if (json_object_object_get_ex(root, "total", &value)) {
        result->total = json_object_get_int(value);
    }
    result->current_page = 1;

    if (!json_object_object_get_ex(root, "items", &music) || !json_object_is_type(music, json_type_array)) {
        ret = 0;
        goto done;
    }
    result->count = json_object_array_length(music);
    if (result->count > 0) {
        result->items = (MusicSourceItem *)calloc((size_t)result->count, sizeof(MusicSourceItem));
        if (result->items == NULL) goto done;
        for (i = 0; i < result->count; ++i) {
            if (music_source_server_parse_music_item(json_object_array_get_idx(music, i), &result->items[i]) != 0) {
                free(result->items);
                result->items = NULL;
                result->count = 0;
                goto done;
            }
        }
    }
    server_log_result_preview("playlist.detail.reply", playlist_id, result);
    ret = 0;

done:
    if (request != NULL) json_object_put(request);
    if (root != NULL) json_object_put(root);
    free(payload);
    close(fd);
    return ret;
}

static int server_fetch_play_url(const char *source, const char *song_id, char *url_buf, size_t url_size)
{
    int fd;
    int ret = -1;
    char *payload = NULL;
    json_object *request = NULL;
    json_object *root = NULL;
    json_object *value = NULL;
    const char *pu;

    if (source == NULL || song_id == NULL || source[0] == '\0' || song_id[0] == '\0' || url_buf == NULL || url_size == 0) {
        return -1;
    }
    url_buf[0] = '\0';
    fd = server_connect_once();
    if (fd < 0) {
        return -1;
    }
    request = json_object_new_object();
    json_object_object_add(request, "cmd", json_object_new_string("music.url.resolve"));
    json_object_object_add(request, "source", json_object_new_string(source));
    json_object_object_add(request, "id", json_object_new_string(song_id));
    if (server_send_request(fd, request) != 0) {
        goto done;
    }
    if (server_recv_response(fd, &payload) != 0) {
        goto done;
    }
    root = json_tokener_parse(payload);
    if (root == NULL) {
        goto done;
    }
    if (!json_object_object_get_ex(root, "result", &value) || strcmp(json_object_get_string(value), "ok") != 0) {
        goto done;
    }
    if (!json_object_object_get_ex(root, "play_url", &value)) {
        goto done;
    }
    pu = json_object_get_string(value);
    if (pu == NULL || pu[0] == '\0') {
        goto done;
    }
    if (snprintf(url_buf, url_size, "%s", pu) >= (int)url_size) {
        goto done;
    }
    LOGI(TAG, "resolve.play_url source=%s id=%s play_url=%s", source, song_id, pu);
    ret = 0;

done:
    if (request != NULL) {
        json_object_put(request);
    }
    if (root != NULL) {
        json_object_put(root);
    }
    free(payload);
    close(fd);
    return ret;
}

static int music_source_server_get_url(const char *source, const char *song_id, char *url_buf, size_t url_size)
{
    if (source == NULL || song_id == NULL || url_buf == NULL || url_size == 0) {
        return -1;
    }
    return server_fetch_play_url(source, song_id, url_buf, url_size);
}

static void music_source_server_free_result(MusicSourceResult *result)
{
    if (result == NULL) return;
    free(result->items);
    server_result_reset(result);
}

int music_source_server_resolve_keyword(const char *keyword, const char *source, MusicSourceItem *out_item)
{
    MusicSourceResult result;

    if (keyword == NULL || keyword[0] == '\0' || out_item == NULL) {
        return -1;
    }
    memset(out_item, 0, sizeof(*out_item));
    memset(&result, 0, sizeof(result));
    if (music_source_server_list_music_page(keyword, source, 1, 1, &result) != 0 || result.count <= 0) {
        return -1;
    }
    *out_item = result.items[0];
    server_sync_item_aliases(out_item);
    if (out_item->play_url[0] == '\0') {
        (void)server_fetch_play_url(out_item->source,
                                    out_item->id[0] != '\0' ? out_item->id : out_item->song_id,
                                    out_item->play_url, sizeof(out_item->play_url));
    }
    LOGI(TAG, "resolve.keyword source=%s keyword=%s first=%s/%s/%s/%s play_url=%s",
         music_source_server_effective_source(source),
         keyword,
         out_item->source,
         out_item->id[0] != '\0' ? out_item->id : out_item->song_id,
         out_item->title[0] != '\0' ? out_item->title : out_item->song_name,
         out_item->subtitle[0] != '\0' ? out_item->subtitle : out_item->singer,
         out_item->play_url);
    music_source_server_free_result(&result);
    return 0;
}

int music_source_server_resolve_playlist_keyword(const char *keyword, const char *source, MusicSourceResult *out_result)
{
    MusicSourceResult playlists;
    const MusicSourceItem *playlist;
    const char *playlist_id;

    if (out_result == NULL) {
        return -1;
    }
    memset(out_result, 0, sizeof(*out_result));
    memset(&playlists, 0, sizeof(playlists));
    if (music_source_server_list_playlist_page(keyword, source, 1, 1, &playlists) != 0 || playlists.count <= 0) {
        music_source_server_free_result(&playlists);
        return -1;
    }
    playlist = &playlists.items[0];
    playlist_id = playlist->id[0] != '\0' ? playlist->id : playlist->song_id;
    LOGI(TAG, "resolve.playlist source=%s keyword=%s first=%s/%s/%s/%s",
         music_source_server_effective_source(source),
         keyword != NULL ? keyword : "",
         playlist->source,
         playlist_id,
         playlist->title[0] != '\0' ? playlist->title : playlist->song_name,
         playlist->subtitle[0] != '\0' ? playlist->subtitle : playlist->singer);
    if (playlist_id == NULL || playlist_id[0] == '\0' ||
        music_source_server_playlist_detail(playlist_id, playlist->source, out_result) != 0) {
        music_source_server_free_result(&playlists);
        return -1;
    }
    music_source_server_free_result(&playlists);
    return 0;
}

int music_source_server_load_playlist_detail(const char *playlist_id, const char *source, MusicSourceResult *out_result)
{
    return music_source_server_playlist_detail(playlist_id, source, out_result);
}

int music_source_server_load_playlist_detail_page(const char *playlist_id, const char *source,
                                                   int page, int page_size, MusicSourceResult *out_result)
{
    int fd;
    int ret = -1;
    char *payload = NULL;
    json_object *request = NULL;
    json_object *root = NULL;
    json_object *music = NULL;
    json_object *value = NULL;
    int i;

    if (playlist_id == NULL || playlist_id[0] == '\0' || out_result == NULL) {
        return -1;
    }
    server_result_reset(out_result);
    fd = server_connect_once();
    if (fd < 0) return -1;

    request = json_object_new_object();
    json_object_object_add(request, "cmd", json_object_new_string("music.playlist.detail"));
    json_object_object_add(request, "id", json_object_new_string(playlist_id));
    if (source != NULL && source[0] != '\0') {
        json_object_object_add(request, "source", json_object_new_string(source));
    }
    json_object_object_add(request, "page", json_object_new_int(page));
    json_object_object_add(request, "page_size", json_object_new_int(page_size));
    if (server_send_request(fd, request) != 0) goto done;
    if (server_recv_response(fd, &payload) != 0) goto done;

    root = json_tokener_parse(payload);
    if (root == NULL) goto done;
    if (!json_object_object_get_ex(root, "result", &value) ||
        strcmp(json_object_get_string(value), "ok") != 0) {
        ret = 0;
        goto done;
    }
    if (json_object_object_get_ex(root, "total_pages", &value)) {
        out_result->total_pages = json_object_get_int(value);
    }
    if (json_object_object_get_ex(root, "total", &value)) {
        out_result->total = json_object_get_int(value);
    }
    out_result->current_page = page;

    if (!json_object_object_get_ex(root, "items", &music) || !json_object_is_type(music, json_type_array)) {
        ret = 0;
        goto done;
    }
    out_result->count = json_object_array_length(music);
    if (out_result->count > 0) {
        out_result->items = (MusicSourceItem *)calloc((size_t)out_result->count, sizeof(MusicSourceItem));
        if (out_result->items == NULL) goto done;
        for (i = 0; i < out_result->count; ++i) {
            if (music_source_server_parse_music_item(json_object_array_get_idx(music, i), &out_result->items[i]) != 0) {
                free(out_result->items);
                out_result->items = NULL;
                out_result->count = 0;
                goto done;
            }
        }
    }
    ret = 0;

done:
    if (request != NULL) json_object_put(request);
    if (root != NULL) json_object_put(root);
    free(payload);
    close(fd);
    return ret;
}

const MusicSourceBackend *music_source_server_backend(void)
{
    static const MusicSourceBackend backend = {
        .name = "server",
        .search = music_source_server_search,
        .get_url = music_source_server_get_url,
        .free_result = music_source_server_free_result
    };
    return &backend;
}
