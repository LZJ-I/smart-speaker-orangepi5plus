#include "music_source_manager.h"

#include <arpa/inet.h>
#include <ctype.h>
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

#include "player_constants.h"

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

static const char *music_server_ip(void)
{
    const char *value = getenv(SERVER_IP_ENV);
    if (value != NULL && value[0] != '\0') {
        return value;
    }
    return SERVER_IP;
}

static int music_server_port(void)
{
    const char *value = getenv(SERVER_PORT_ENV);
    char *end = NULL;
    long port;
    if (value == NULL || value[0] == '\0') {
        return SERVER_PORT;
    }
    port = strtol(value, &end, 10);
    if (end == value || *end != '\0' || port <= 0 || port > 65535) {
        return SERVER_PORT;
    }
    return (int)port;
}

static const char *music_server_base_url(void)
{
    static char url[256];
    const char *value = getenv(SERVER_MUSIC_BASE_URL_ENV);
    if (value != NULL && value[0] != '\0') {
        return value;
    }
    snprintf(url, sizeof(url), "http://%s/music/", music_server_ip());
    return url;
}

static int append_encoded_segment(char *buf, size_t buf_size, size_t *off, const char *seg, size_t seg_len)
{
    size_t i;
    if (seg_len == 0)
        return 0;
    for (i = 0; i < seg_len; i++) {
        unsigned char c = (unsigned char)seg[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            if (*off + 1 >= buf_size)
                return -1;
            buf[(*off)++] = (char)c;
        } else {
            if (*off + 3 >= buf_size)
                return -1;
            snprintf(buf + *off, buf_size - *off, "%%%02X", (unsigned)c);
            *off += 3;
        }
    }
    buf[*off] = '\0';
    return 0;
}

static int encode_path_segments(const char *song_id, char *out, size_t out_size)
{
    const char *start = song_id;
    const char *p;
    size_t off = 0;
    out[0] = '\0';
    if (song_id == NULL || song_id[0] == '\0')
        return -1;
    for (p = song_id;; p++) {
        if (*p == '/' || *p == '\0') {
            if (append_encoded_segment(out, out_size, &off, start, (size_t)(p - start)) != 0)
                return -1;
            if (*p == '/') {
                if (off + 1 >= out_size)
                    return -1;
                out[off++] = '/';
                out[off] = '\0';
                start = p + 1;
            }
            if (*p == '\0')
                break;
        }
    }
    return 0;
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
        struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
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

static int server_parse_music_item(json_object *item_obj, MusicSourceItem *item)
{
    json_object *value;
    const char *raw_song = NULL;
    const char *raw_singer = NULL;
    if (item_obj == NULL || item == NULL) return -1;
    memset(item, 0, sizeof(*item));
    server_copy_text(item->source, sizeof(item->source), "server");

    if (json_object_object_get_ex(item_obj, "source", &value)) {
        const char *s = json_object_get_string(value);
        if (s != NULL && s[0] != '\0') {
            server_copy_text(item->source, sizeof(item->source), s);
        }
    }
    if (json_object_object_get_ex(item_obj, "song_id", &value)) {
        server_copy_text(item->song_id, sizeof(item->song_id), json_object_get_string(value));
    }
    if (json_object_object_get_ex(item_obj, "path", &value)) {
        const char *p = json_object_get_string(value);
        if (p != NULL && p[0] != '\0' && item->song_id[0] == '\0') {
            server_copy_text(item->song_id, sizeof(item->song_id), p);
        }
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
    server_parse_song_meta(raw_song, item->song_id, raw_singer,
                           item->song_name, sizeof(item->song_name),
                           item->singer, sizeof(item->singer));
    if (item->song_name[0] == '\0') {
        return -1;
    }
    if (item->song_id[0] == '\0') {
        server_copy_text(item->song_id, sizeof(item->song_id), item->song_name);
    }
    return 0;
}

static int music_source_server_search(const char *keyword, int page, int page_size, MusicSourceResult *result)
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
    json_object_object_add(request, "cmd", json_object_new_string("list_music"));
    json_object_object_add(request, "page", json_object_new_int(page));
    json_object_object_add(request, "page_size", json_object_new_int(page_size));
    if (keyword != NULL && keyword[0] != '\0') {
        json_object_object_add(request, "keyword", json_object_new_string(keyword));
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

    if (!json_object_object_get_ex(root, "music", &music) || !json_object_is_type(music, json_type_array)) {
        ret = 0;
        goto done;
    }
    result->count = json_object_array_length(music);
    if (result->count > 0) {
        result->items = (MusicSourceItem *)calloc((size_t)result->count, sizeof(MusicSourceItem));
        if (result->items == NULL) goto done;
        for (i = 0; i < result->count; ++i) {
            if (server_parse_music_item(json_object_array_get_idx(music, i), &result->items[i]) != 0) {
                free(result->items);
                result->items = NULL;
                result->count = 0;
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
    json_object_object_add(request, "cmd", json_object_new_string("get_play_url"));
    json_object_object_add(request, "source", json_object_new_string(source));
    json_object_object_add(request, "song_id", json_object_new_string(song_id));
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
    if (json_object_object_get_ex(root, "result", &value)) {
        const char *rs = json_object_get_string(value);
        if (rs != NULL && strcmp(rs, "disabled") == 0) {
            goto done;
        }
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
    char encoded[MUSIC_ID_MAX * 3];
    if (source == NULL || song_id == NULL || url_buf == NULL || url_size == 0)
        return -1;
    if (strcmp(source, "server") == 0) {
        if (encode_path_segments(song_id, encoded, sizeof(encoded)) != 0)
            return -1;
        if (snprintf(url_buf, url_size, "%s%s", music_server_base_url(), encoded) >= (int)url_size) {
            return -1;
        }
        return 0;
    }
    return server_fetch_play_url(source, song_id, url_buf, url_size);
}

static void music_source_server_free_result(MusicSourceResult *result)
{
    if (result == NULL) return;
    free(result->items);
    server_result_reset(result);
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
