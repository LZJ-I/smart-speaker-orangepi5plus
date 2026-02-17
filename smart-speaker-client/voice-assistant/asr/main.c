#define LOG_LEVEL 4
#include "../../debug_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "../common/alsa.h"
#include "../common/mysamplerate.h"
#include "sherpa_asr.h"

#define TAG "ASR-TEST"

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

    LOGI(TAG, "=== ASR独立测试启动 ===");
    LOGI(TAG, "配置信息：模型采样率=%d Hz，声道数=%d，ALSA周期大小=%d",
            MODEL_SAMPLE_RATE, CHANNELS, PERIOD_SIZE);

    if (init_alsa() != 0) {
        LOGE(TAG, "初始化ALSA失败");
        return -1;
    }

    if (init_resampler() != 0) {
        LOGE(TAG, "初始化重采样器失败");
        return -1;
    }

    if (init_sherpa_asr() != 0) {
        LOGE(TAG, "初始化ASR模型失败");
        return -1;
    }
    LOGI(TAG, "ASR模型加载完成");

    alsa_buf = (int16_t *)malloc(PERIOD_SIZE * CHANNELS * sizeof(int16_t));
    if (alsa_buf == NULL) {
        LOGE(TAG, "分配ALSA录音缓冲区失败");
        return -1;
    }

    LOGI(TAG, "========= 开始ASR识别 =========\n");

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

        float *model_audio = NULL;
        int model_frames = 0;
        if (resample_audio(alsa_buf, (int)read_frames, &model_audio, &model_frames) != 0) {
            LOGW(TAG, "重采样失败，跳过当前帧");
            continue;
        }
        if (model_frames == 0) {
            continue;
        }

        process_asr_result(model_audio, model_frames);
    }

    free(alsa_buf);
    cleanup_sherpa_asr();
    cleanup_resampler();
    cleanup_alsa();

    LOGI(TAG, "ASR测试程序退出");
    return 0;
}
