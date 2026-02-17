#define LOG_LEVEL 4
#include "../../debug_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "../../common/alsa.h"
#include "../sherpa_kws.h"

#define TAG "KWS-TEST"
#define KWS_FIFO_PATH "../fifo/kws_fifo"

int running = 1;
int16_t *alsa_buf = NULL;

void sigint_handler(int sig) {
    LOGI(TAG, "收到退出信号，正在清理资源...");
    running = 0;
}

int main(int argc, char const *argv[])
{
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        LOGE(TAG, "注册SIGINT信号处理失败: %s", strerror(errno));
        return -1;
    }

    LOGI(TAG, "=== KWS独立测试启动 ===");
    LOGI(TAG, "配置信息：模型采样率=%d Hz，声道数=%d，ALSA周期大小=%d",
            MODEL_SAMPLE_RATE, CHANNELS, PERIOD_SIZE);

    if (init_alsa() != 0) {
        LOGE(TAG, "初始化ALSA失败");
        return -1;
    }

    if (init_sherpa_kws() != 0) {
        LOGE(TAG, "初始化KWS模型失败");
        return -1;
    }
    LOGI(TAG, "KWS模型加载完成");

    alsa_buf = (int16_t *)malloc(PERIOD_SIZE * CHANNELS * sizeof(int16_t));
    if (alsa_buf == NULL) {
        LOGE(TAG, "分配ALSA录音缓冲区失败");
        return -1;
    }

    LOGI(TAG, "开始KWS识别");

    while (running) {
        snd_pcm_sframes_t read_frames = snd_pcm_readi(g_pcm_handle, alsa_buf, PERIOD_SIZE);
        if (read_frames < 0) {
            LOGW(TAG, "ALSA读取失败: %s", snd_strerror(read_frames));
            if (read_frames == -EPIPE) {
                snd_pcm_prepare(g_pcm_handle);
                continue;
            }
            break;
        }
        if (read_frames == 0) {
            continue;
        }

        float model_audio[PERIOD_SIZE * CHANNELS];
        for (int i = 0; i < read_frames * CHANNELS; i++) {
            model_audio[i] = alsa_buf[i] / 32768.0f;
        }

        char keyword[256] = {0};
        if(process_kws_result(model_audio, (int)read_frames, keyword, sizeof(keyword)))
        {
            LOGI(TAG, "检测到关键词: %s", keyword);
        }
    }

    free(alsa_buf);
    cleanup_sherpa_kws();
    cleanup_alsa();

    LOGI(TAG, "KWS测试程序退出");
    return 0;
}
