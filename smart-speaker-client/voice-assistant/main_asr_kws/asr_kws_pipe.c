#include "asr_kws_pipe.h"
#include "asr_kws_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define LOG_LEVEL 4
#include "../../debug_log.h"
#include "../common/ipc_protocol.h"

#define TAG "ASR_KWS_MAIN"

int asr_kws_pipe_open(int *asr_fd, int *kws_fd, int *tts_fd, int *asr_ctrl_fd) {
    if (asr_fd == NULL || kws_fd == NULL || tts_fd == NULL || asr_ctrl_fd == NULL) {
        return -1;
    }
    if (*asr_fd != -1) { close(*asr_fd); *asr_fd = -1; }
    if (*kws_fd != -1) { close(*kws_fd); *kws_fd = -1; }
    if (*tts_fd != -1) { close(*tts_fd); *tts_fd = -1; }

    mkdir(FIFO_DIR_PATH, 0777);
    if (access(ASR_FIFO_PATH, F_OK) == -1) {
        if (mkfifo(ASR_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
            LOGW(TAG, "创建ASR管道失败: %s", strerror(errno));
        }
    }
    if (access(KWS_FIFO_PATH, F_OK) == -1) {
        if (mkfifo(KWS_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
            LOGW(TAG, "创建KWS管道失败: %s", strerror(errno));
        }
    }
    if (access(TTS_FIFO_PATH, F_OK) == -1) {
        if (mkfifo(TTS_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
            LOGW(TAG, "创建TTS管道失败: %s", strerror(errno));
        }
    }
    if (access(ASR_CTRL_FIFO_PATH, F_OK) == -1) {
        if (mkfifo(ASR_CTRL_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
            LOGW(TAG, "创建ASR控制管道失败: %s", strerror(errno));
        }
    }
    if (access(TTS_WAKE_DONE_FIFO_PATH, F_OK) == -1) {
        if (mkfifo(TTS_WAKE_DONE_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
            LOGW(TAG, "创建唤醒完成管道失败: %s", strerror(errno));
        }
    }

    LOGD(TAG, "尝试打开ASR管道");
    *asr_fd = open(ASR_FIFO_PATH, O_WRONLY | O_NONBLOCK);
    LOGD(TAG, "尝试打开KWS管道");
    *kws_fd = open(KWS_FIFO_PATH, O_WRONLY | O_NONBLOCK);
    LOGD(TAG, "尝试打开TTS管道");
    *tts_fd = open(TTS_FIFO_PATH, O_WRONLY | O_NONBLOCK);

    if (*asr_fd != -1 || *kws_fd != -1 || *tts_fd != -1) {
        LOGI(TAG, "管道打开成功！asr_fd=%d, kws_fd=%d, tts_fd=%d", *asr_fd, *kws_fd, *tts_fd);
    }
    return 0;
}

int asr_kws_pipe_write_text(int *fd, const char *path, const char *text) {
    if (text == NULL || text[0] == '\0') {
        return -1;
    }
    if (fd == NULL || path == NULL) {
        return -1;
    }
    if (*fd == -1) {
        *fd = open(path, O_WRONLY | O_NONBLOCK);
    }
    if (*fd == -1) {
        if (!(strstr(path, "kws") != NULL && errno == ENXIO)) {
            LOGW(TAG, "打开管道失败 %s: %s", path, strerror(errno));
        }
        return -1;
    }
    char buf[MAX_TEXT_LEN + 4] = {0};
    snprintf(buf, sizeof(buf), "%s\n", text);
    ssize_t ret = write(*fd, buf, strlen(buf));
    if (ret < 0) {
        LOGW(TAG, "写入管道失败 %s: %s", path, strerror(errno));
        if (errno == EPIPE || errno == ENXIO) {
            close(*fd);
            *fd = -1;
        }
        return -1;
    }
    return 0;
}

void asr_kws_pipe_process_ctrl(int *asr_ctrl_fd, int *online_mode) {
    if (asr_ctrl_fd == NULL || online_mode == NULL) {
        return;
    }
    if (*asr_ctrl_fd == -1) {
        *asr_ctrl_fd = open(ASR_CTRL_FIFO_PATH, O_RDONLY | O_NONBLOCK);
        if (*asr_ctrl_fd == -1) {
            return;
        }
    }
    char buf[128] = {0};
    ssize_t n = read(*asr_ctrl_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        if (n == 0) {
            close(*asr_ctrl_fd);
            *asr_ctrl_fd = -1;
        }
        return;
    }
    if (strstr(buf, "mode:offline")) {
        *online_mode = ONLINE_MODE_NO;
        LOGI(TAG, "通过控制管道切换到离线模式");
    } else if (strstr(buf, "mode:online")) {
        *online_mode = ONLINE_MODE_YES;
        LOGI(TAG, "通过控制管道切换到在线模式");
    }
}
