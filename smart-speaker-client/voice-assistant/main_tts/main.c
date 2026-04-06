#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

#define LOG_LEVEL 4
#include "../../debug_log.h"
#include "../common/ipc_protocol.h"
#include "../../ipc/ipc_message.h"
#include "../tts/alsa_output.h"
#include "../tts/sherpa_tts.h"
#include "tts_playback.h"
#include "tts_ipc_handler.h"

#define TAG "TTS_MAIN"

int running = 1;
int tts_fd = -1;

void sigint_handler(int signum) {
    LOGI(TAG, "收到退出信号，正在清理资源...");
    tts_playback_cleanup();
    if (tts_fd != -1) {
        close(tts_fd);
    }
    cleanup_alsa_output();
    cleanup_sherpa_tts();
    LOGI(TAG, "TTS进程退出");
    running = 0;
    exit(0);
}

void sigusr1_handler(int signum) {
    LOGI(TAG, "收到SIGUSR1信号，停止当前播放");
    tts_playback_stop();
}

int open_pipes(void) {
    mkdir(FIFO_DIR_PATH, 0777);
    if (access(TTS_FIFO_PATH, F_OK) == -1) {
        if (mkfifo(TTS_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
            LOGE(TAG, "创建TTS管道失败: %s", strerror(errno));
            return -1;
        }
    }
    if (access(TTS_WAKE_DONE_FIFO_PATH, F_OK) == -1) {
        if (mkfifo(TTS_WAKE_DONE_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
            LOGE(TAG, "创建唤醒完成管道失败: %s", strerror(errno));
            return -1;
        }
    }
    tts_fd = open(TTS_FIFO_PATH, O_RDONLY);
    if (tts_fd == -1) {
        LOGE(TAG, "打开TTS管道失败: %s", strerror(errno));
        return -1;
    }
    LOGI(TAG, "TTS管道打开成功");
    return 0;
}

int main(int argc, char const *argv[]) {
    app_log_init("tts");
    LOGI(TAG, "=== TTS进程启动 ===");
    unsetenv("DISPLAY");

    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        LOGE(TAG, "注册SIGINT信号处理失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (signal(SIGUSR1, sigusr1_handler) == SIG_ERR) {
        LOGE(TAG, "注册SIGUSR1信号处理失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (init_sherpa_tts_with_chunk_size(-1) != 0) {
        LOGE(TAG, "初始化TTS模型失败");
        return -1;
    }

    char playback_device[256] = "dmix:CARD=rockchipes8388,DEV=0";
    LOGI(TAG, "使用播放设备: %s", playback_device);

    if (init_alsa_output(g_tts_sample_rate, playback_device) != 0) {
        LOGE(TAG, "初始化ALSA输出失败");
        cleanup_sherpa_tts();
        return -1;
    }

    if (open_pipes() != 0) {
        LOGE(TAG, "打开管道失败");
        cleanup_alsa_output();
        cleanup_sherpa_tts();
        return -1;
    }

    LOGI(TAG, "TTS进程就绪，等待命令...");
    LOGD(TAG, "IPC协议头大小: %zu bytes", sizeof(IPCHeader));

    while (running) {
        struct pollfd pfd = { .fd = tts_fd, .events = POLLIN };
        int pr = poll(&pfd, 1, 100);
        if (pr <= 0) {
            continue;
        }
        while (running) {
            IPCHeader header;
            uint8_t *body = NULL;
            int ret = ipc_recv_message(tts_fd, &header, &body);
            if (ret == 1) {
                break;
            }
            if (ret == 2) {
                continue;
            }
            if (ret != 0) {
                LOGE(TAG, "读取IPC消息失败: %s", strerror(errno));
                break;
            }
            LOGD(TAG, "收到完整消息，type=%u, body_len=%u, seq=%u", header.type, header.body_len, header.seq);
            handle_ipc_message(header.type, body, header.body_len);
            free(body);
        }
    }

    return 0;
}
