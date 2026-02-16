#define LOG_LEVEL 4
#include "../../debug_log.h"
#include "mysamplerate.h"

static float *src_output_buf = NULL;
static const int MAX_OUTPUT_FRAMES = PERIOD_SIZE * 3;

#define TAG "RESAMPLER"

int init_resampler(void)
{   
    if (g_actual_rate <= 0) {
        LOGE(TAG, "重采样初始化失败：g_actual_rate无效（%u），请检查ALSA初始化", g_actual_rate);
        return -1;
    }

    if (MODEL_SAMPLE_RATE <= 0) {
        LOGE(TAG, "重采样初始化失败：MODEL_SAMPLE_RATE无效（%d）", MODEL_SAMPLE_RATE);
        return -1;
    }

    if (CHANNELS != 1) {
        LOGW(TAG, "当前仅支持单声道！请确保ALSA配置为单声道（CHANNELS=%d）", CHANNELS);
        return -1;
    }

    src_output_buf = (float *)malloc(MAX_OUTPUT_FRAMES * sizeof(float));
    if (src_output_buf == NULL) {
        LOGE(TAG, "分配输出缓冲区失败（内存不足）");
        return -1;
    }

    if (g_actual_rate == MODEL_SAMPLE_RATE) {
        LOGI(TAG, "采样率匹配：%u Hz == %d Hz，无需重采样，仅做格式转换", g_actual_rate, MODEL_SAMPLE_RATE);
    } else {
        LOGI(TAG, "采样率不匹配：%u Hz → %d Hz，需要重采样", g_actual_rate, MODEL_SAMPLE_RATE);
    }
    return 0;
}

int resample_audio(const int16_t *input, int input_frames, float **output, int *output_frames)
{
    if (input == NULL || output == NULL || output_frames == NULL) {
        LOGE(TAG, "重采样参数无效（空指针）");
        return -EINVAL;
    }
    if (input_frames <= 0) {
        LOGW(TAG, "重采样输入帧数为0");
        *output_frames = 0;
        return 0;
    }

    if (g_actual_rate == MODEL_SAMPLE_RATE) {
        for (int i = 0; i < input_frames * CHANNELS; i++) {
            src_output_buf[i] = (float)input[i] / 32768.0f;
        }
        *output = src_output_buf;
        *output_frames = input_frames;
    } else {
        LOGE(TAG, "采样率不匹配，暂不支持重采样");
        return -1;
    }

    return 0;
}

void cleanup_resampler(void)
{
    if (src_output_buf != NULL) {
        free(src_output_buf);
        src_output_buf = NULL;
    }
    LOGI(TAG, "重采样器资源清理完成");
}
