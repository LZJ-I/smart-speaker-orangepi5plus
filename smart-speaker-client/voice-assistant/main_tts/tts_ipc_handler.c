#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define LOG_LEVEL 4
#include "../../debug_log.h"
#include "../common/ipc_protocol.h"
#include "../tts/alsa_output.h"
#include "tts_playback.h"

#define TAG "TTS_MAIN"
#define DEBUG_LOG_PATH "/home/lzj/smart-speaker-orangepi5plus/smart-speaker-client/.cursor/debug-55e7c0.log"

static void dbg(const char *msg, const char *k, int v) {
    FILE *f = fopen(DEBUG_LOG_PATH, "a");
    if (f) { fprintf(f, "{\"sessionId\":\"55e7c0\",\"message\":\"%s\",\"data\":{\"%s\":%d},\"timestamp\":%lu}\n", msg, k, v, (unsigned long)time(NULL)*1000); fclose(f); }
}

void handle_ipc_message(uint16_t type, const uint8_t *body, uint32_t body_len) {
    switch (type) {
        case IPC_CMD_PLAY_TEXT: {
            if (body == NULL || body_len == 0) {
                LOGW(TAG, "文本为空，跳过播放");
                return;
            }
            char *text = (char *)malloc(body_len + 1);
            if (!text) return;
            memcpy(text, body, body_len);
            text[body_len] = '\0';
            LOGI(TAG, "收到播放文本命令, 长度: %u", body_len);
            dbg("play_text_enter", "body_len", (int)body_len);
            tts_playback_stop();
            if (tts_playback_request_text(text) != 0) {
                tts_playback_notify_player("tts:done");
            }
            free(text);
            break;
        }

        case IPC_CMD_PLAY_AUDIO_FILE: {
            if (body == NULL || body_len == 0) {
                LOGW(TAG, "音频文件路径为空，跳过播放");
                tts_playback_notify_player("tts:done");
                break;
            }
            if (tts_playback_get_content_session()) {
                break;
            }
            char *path = (char *)malloc(body_len + 1);
            if (path == NULL) {
                LOGE(TAG, "分配音频路径内存失败");
                tts_playback_notify_player("tts:done");
                break;
            }
            memcpy(path, body, body_len);
            path[body_len] = '\0';
            if (path[0] == '\0') {
                free(path);
                tts_playback_notify_player("tts:done");
                break;
            }
            LOGI(TAG, "收到播放音频文件命令: %s", path);
            if (access(path, F_OK) != 0) {
                LOGW(TAG, "音频文件不存在: %s", path);
                free(path);
                tts_playback_notify_player("tts:done");
                break;
            }
            tts_playback_notify_player("tts:start");
            tts_playback_play_wav_file(path);
            free(path);
            tts_playback_join();
            tts_playback_notify_player("tts:done");
            break;
        }

        case IPC_CMD_STOP_PLAYING:
            LOGI(TAG, "收到停止播放命令");
            tts_playback_stop();
            break;

        case IPC_CMD_PLAY_WAKE_RESPONSE: {
            dbg("play_wake_enter", "x", 1);
            LOGI(TAG, "收到播放唤醒响应命令");
            tts_playback_stop();
            tts_playback_notify_player("tts:start");
            tts_playback_wake_response();
            if (!tts_playback_is_playing()) {
                tts_playback_notify_player("tts:done");
            }
            tts_playback_join();
            dbg("play_wake_joined", "x", 1);
            int wfd = open(TTS_WAKE_DONE_FIFO_PATH, O_WRONLY | O_NONBLOCK);
            dbg("wake_done_open", "fd", wfd);
            if (wfd >= 0) {
                write(wfd, "1", 1);
                close(wfd);
            }
            break;
        }

        default:
            LOGW(TAG, "收到未知命令: %d", type);
            break;
    }
}
