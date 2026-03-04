#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "player.h"
#include "select.h"
#include "../debug_log.h"

#define TAG "PLAYER"

int init_asr_fifo()
{
    g_asr_fd = open(ASR_PIPE_PATH, O_RDONLY | O_NONBLOCK);
    if (g_asr_fd == -1) {
        LOGE(TAG, "打开asr语音识别管道失败: %s", strerror(errno));
        return -1;
    }
    FD_SET(g_asr_fd, &READSET);
    update_max_fd();
    return 0;
}

int init_kws_fifo()
{
    g_kws_fd = open(KWS_PIPE_PATH, O_RDONLY | O_NONBLOCK);
    if (g_kws_fd == -1) {
        LOGE(TAG, "打开kws关键词识别管道失败: %s", strerror(errno));
        return -1;
    }
    FD_SET(g_kws_fd, &READSET);
    update_max_fd();
    return 0;
}

int init_tts_fifo()
{
    g_tts_fd = open(TTS_PIPE_PATH, O_WRONLY | O_NONBLOCK);
    if (g_tts_fd == -1) {
        LOGW(TAG, "打开tts语音合成管道失败: %s", strerror(errno));
    }
    return 0;
}

int init_player_ctrl_fifo()
{
    g_player_ctrl_fd = open(PLAYER_CTRL_PIPE_PATH, O_RDONLY | O_NONBLOCK);
    if (g_player_ctrl_fd == -1) {
        LOGW(TAG, "打开player控制管道失败: %s", strerror(errno));
        return -1;
    }
    FD_SET(g_player_ctrl_fd, &READSET);
    update_max_fd();
    return 0;
}
