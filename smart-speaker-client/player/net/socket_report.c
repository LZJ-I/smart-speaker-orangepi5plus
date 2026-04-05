#include "socket_report.h"
#include "socket.h"
#include <sys/socket.h>
#include "player.h"
#include "shm.h"
#include "main.h"
#include "device.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <json-c/json.h>
#include "debug_log.h"

#define TAG "SOCKET"

int socket_send_data(json_object *data)
{
    const char *json_str = json_object_to_json_string(data);
    size_t json_len;
    size_t pkt_len;
    char *buf = NULL;
    size_t sent = 0;

    if (NULL == json_str) {
        LOGE(TAG, "JSON转换失败");
        json_object_put(data);
        return -1;
    }
    json_len = strlen(json_str);
    if (json_len > (size_t)INT_MAX - sizeof(int)) {
        LOGE(TAG, "JSON 过长");
        json_object_put(data);
        return -1;
    }
    pkt_len = sizeof(int) + json_len;
    buf = (char *)malloc(pkt_len);
    if (buf == NULL) {
        LOGE(TAG, "分配发送缓冲失败");
        json_object_put(data);
        return -1;
    }
    {
        int len_i = (int)json_len;
        memcpy(buf, &len_i, sizeof(len_i));
    }
    memcpy(buf + sizeof(int), json_str, json_len);

    while (sent < pkt_len) {
        ssize_t n = send(g_socket_fd, buf + sent, pkt_len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOGE(TAG, "发送失败: %s", strerror(errno));
            free(buf);
            json_object_put(data);
            return -1;
        }
        sent += (size_t)n;
    }
    free(buf);
    json_object_put(data);
    return 0;
}

static void* report_thread(void *arg)
{
    int volume;
    while (1) {
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
        json_object_object_add(json, "deviceid", json_object_new_string(DEVICE_ID));
        json_object_object_add(json, "cur_volume", json_object_new_int(volume));
        socket_send_data(json);
        sleep(1);
    }
    return NULL;
}

int socket_report_start_thread(pthread_t *out_tid)
{
    if (out_tid == NULL) return -1;
    if (pthread_create(out_tid, NULL, report_thread, NULL) != 0) {
        LOGE(TAG, "创建上报线程失败");
        return -1;
    }
    return 0;
}
