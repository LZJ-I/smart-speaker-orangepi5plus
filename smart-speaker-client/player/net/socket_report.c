#include "socket_report.h"
#include "socket.h"
#include <signal.h>
#include <sys/socket.h>
#include "player.h"
#include "shm.h"
#include "runtime_config.h"
#include "device.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <json-c/json.h>
#include "debug_log.h"
#include "player_constants.h"
#include <stdlib.h>

#define TAG "SOCKET"

static volatile sig_atomic_t g_report_stop;

static void socket_report_add_queue_snapshot_fields(json_object *json, const Shm_Data *data)
{
    int current_index;
    player_playlist_ctx_t pl;

    if (json == NULL || data == NULL) {
        return;
    }
    current_index = link_get_current_index(data->current_source, data->current_song_id);
    json_object_object_add(json, "playlist_version", json_object_new_int64((int64_t)link_get_playlist_version()));
    json_object_object_add(json, "current_index", json_object_new_int(current_index));
    json_object_object_add(json, "current_source", json_object_new_string(data->current_source));
    json_object_object_add(json, "current_song_id", json_object_new_string(data->current_song_id));
    player_get_playlist_ctx(&pl);
    json_object_object_add(json, "playlist_page", json_object_new_int(pl.current_page));
    json_object_object_add(json, "playlist_total_pages", json_object_new_int(pl.total_pages));
}

int socket_send_data(json_object *data)
{
    const char *json_str = json_object_to_json_string(data);
    int len;
    size_t total;
    char *buf;

    if (NULL == json_str) {
        LOGE(TAG, "JSON转换失败");
        json_object_put(data);
        return -1;
    }
    len = (int)strlen(json_str);
    if (len < 0 || len > SOCKET_JSON_BUF_MAX - (int)sizeof(int)) {
        LOGE(TAG, "JSON 过长: %d", len);
        json_object_put(data);
        return -1;
    }
    total = sizeof(int) + (size_t)len;
    buf = malloc(total);
    if (buf == NULL) {
        LOGE(TAG, "分配发送缓冲失败");
        json_object_put(data);
        return -1;
    }
    memcpy(buf, &len, sizeof(len));
    memcpy(buf + sizeof(len), json_str, (size_t)len);
    if (-1 == send(g_socket_fd, buf, total, MSG_NOSIGNAL)) {
        LOGE(TAG, "发送失败: %s", strerror(errno));
        free(buf);
        json_object_put(data);
        return -1;
    }
    free(buf);
    json_object_put(data);
    return 0;
}

static void* report_thread(void *arg)
{
    int volume;
    (void)arg;
    while (!g_report_stop) {
        Shm_Data data = {0};
        shm_get(&data);
        device_get_volume(&volume);
        json_object *json = json_object_new_object();
        json_object_object_add(json, "cmd", json_object_new_string("device_report"));
        json_object_object_add(json, "cur_singer", json_object_new_string(data.current_singer));
        json_object_object_add(json, "cur_music", json_object_new_string(data.current_music));
        json_object_object_add(json, "cur_mode", json_object_new_int(data.current_mode));
        if (g_current_state == PLAY_STATE_STOP) {
            json_object_object_add(json, "state", json_object_new_string("stop"));
        } else if (g_current_state == PLAY_STATE_PLAY && g_current_suspend == PLAY_SUSPEND_NO) {
            json_object_object_add(json, "state", json_object_new_string("play"));
        } else if (g_current_state == PLAY_STATE_PLAY && g_current_suspend == PLAY_SUSPEND_YES) {
            json_object_object_add(json, "state", json_object_new_string("suspend"));
        }
        json_object_object_add(json, "deviceid", json_object_new_string(player_runtime_device_id()));
        json_object_object_add(json, "cur_volume", json_object_new_int(volume));
        socket_report_add_queue_snapshot_fields(json, &data);
        if (socket_send_data(json) != 0) {
            break;
        }
        for (int i = 0; i < 10 && !g_report_stop; i++) {
            usleep(100000);
        }
    }
    return NULL;
}

int socket_report_start_thread(pthread_t *out_tid)
{
    if (out_tid == NULL) return -1;
    g_report_stop = 0;
    if (pthread_create(out_tid, NULL, report_thread, NULL) != 0) {
        LOGE(TAG, "创建上报线程失败");
        return -1;
    }
    return 0;
}

void socket_report_stop_thread(int sock_fd, pthread_t tid)
{
    g_report_stop = 1;
    if (sock_fd >= 0) {
        (void)shutdown(sock_fd, SHUT_RDWR);
    }
    pthread_join(tid, NULL);
}
