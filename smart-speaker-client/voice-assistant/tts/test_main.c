#define LOG_LEVEL 4
#include "../../debug_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include "sherpa_tts.h"
#include "alsa_output.h"

#define TAG "TTS-TEST"

int running = 1;
FILE *g_wav_file = NULL;
int32_t g_total_samples = 0;
uint32_t g_sample_rate = 0;
int g_use_alsa = 1;
int g_save_file = 1;
int g_first_chunk = 1;
int g_chunk_count = 0;
struct timespec g_start_time;

void sigint_handler(int sig) {
    LOGI(TAG, "收到退出信号，正在清理资源...");
    running = 0;
}

void write_wav_header(FILE *file, uint32_t sample_rate) {
    uint8_t header[44];
    uint32_t data_size = 0;
    uint32_t file_size = 36 + data_size;

    memcpy(header, "RIFF", 4);
    memcpy(header + 4, &file_size, 4);
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    
    uint32_t fmt_chunk_size = 16;
    memcpy(header + 16, &fmt_chunk_size, 4);
    
    uint16_t audio_format = 1;
    memcpy(header + 20, &audio_format, 2);
    
    uint16_t num_channels = 1;
    memcpy(header + 22, &num_channels, 2);
    
    memcpy(header + 24, &sample_rate, 4);
    
    uint32_t byte_rate = sample_rate * 2;
    memcpy(header + 28, &byte_rate, 4);
    
    uint16_t block_align = 2;
    memcpy(header + 32, &block_align, 2);
    
    uint16_t bits_per_sample = 16;
    memcpy(header + 34, &bits_per_sample, 2);
    
    memcpy(header + 36, "data", 4);
    memcpy(header + 40, &data_size, 4);
    
    fwrite(header, 1, 44, file);
}

void update_wav_header(FILE *file, int32_t total_samples) {
    uint32_t data_size = total_samples * 2;
    uint32_t file_size = 36 + data_size;
    
    fseek(file, 4, SEEK_SET);
    fwrite(&file_size, 4, 1, file);
    
    fseek(file, 40, SEEK_SET);
    fwrite(&data_size, 4, 1, file);
    
    fseek(file, 0, SEEK_END);
}

int32_t tts_callback(const float *samples, int32_t n, void *arg) {
    g_chunk_count++;
    
    if (g_first_chunk) {
        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double elapsed = (end_time.tv_sec - g_start_time.tv_sec) + 
                        (end_time.tv_nsec - g_start_time.tv_nsec) / 1e9;
        LOGI(TAG, ">>> 第一块音频到达！耗时: %.3f 秒 <<<", elapsed);
        g_first_chunk = 0;
    }
    
    LOGI(TAG, "[块 %d] 收到音频数据: %d 个样本 (%.2f 秒)", g_chunk_count, n, (float)n / g_sample_rate);
    
    int16_t *pcm_data = (int16_t *)malloc(n * sizeof(int16_t));
    for (int32_t i = 0; i < n; i++) {
        float s = samples[i];
        if (s < -1.0f) s = -1.0f;
        if (s > 1.0f) s = 1.0f;
        pcm_data[i] = (int16_t)(s * 32767.0f);
    }
    
    if (g_use_alsa && g_pcm_handle != NULL) {
        LOGI(TAG, "[块 %d] 正在播放...", g_chunk_count);
        int ret = snd_pcm_writei(g_pcm_handle, pcm_data, n);
        if (ret == -EPIPE) {
            LOGW(TAG, "ALSA缓冲区欠载，重新准备设备");
            snd_pcm_prepare(g_pcm_handle);
            ret = snd_pcm_writei(g_pcm_handle, pcm_data, n);
        }
        if (ret < 0) {
            LOGE(TAG, "ALSA写入失败: %s", snd_strerror(ret));
        }
    }
    
    if (g_save_file && g_wav_file != NULL) {
        fwrite(pcm_data, sizeof(int16_t), n, g_wav_file);
        g_total_samples += n;
        update_wav_header(g_wav_file, g_total_samples);
        LOGI(TAG, "[块 %d] 已写入文件，累计: %d 样本 (%.2f 秒)", 
               g_chunk_count, g_total_samples, (float)g_total_samples / g_sample_rate);
    }
    
    free(pcm_data);
    
    return 1;
}

int main(int argc, char const *argv[])
{
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        LOGE(TAG, "注册SIGINT信号处理失败: %s", strerror(errno));
        return -1;
    }

    LOGI(TAG, "=== TTS独立测试启动 (快速输出模式) ===");

    if (init_sherpa_tts() != 0) {
        LOGE(TAG, "初始化TTS模型失败");
        return -1;
    }
    LOGI(TAG, "TTS模型加载完成");

    const char *text = "你好，这是一个智能音箱测试。今天天气不错，适合出去走走。"
                      "我们来测试一下快速输出功能，看看第一块音频多快能到达。看看第二块音频多快能到达。看看第三块音频多快能到达。";
    int64_t sid = 4;
    float speed = 1.0;
    const char *output_filename = "./generated_fast.wav";

    g_sample_rate = g_tts_sample_rate;
    g_total_samples = 0;
    g_first_chunk = 1;
    g_chunk_count = 0;
    clock_gettime(CLOCK_MONOTONIC, &g_start_time);
    
    if (g_use_alsa) {
        char playback_device[256] = "hw:5,0";
        LOGI(TAG, "正在初始化ALSA输出设备: EDIFIER W820NB 双金标版...");
        
        if (init_alsa_output(g_sample_rate, playback_device) != 0) {
            LOGE(TAG, "初始化ALSA失败，尝试使用 default 设备...");
            strcpy(playback_device, "default");
            if (init_alsa_output(g_sample_rate, playback_device) != 0) {
                LOGE(TAG, "初始化ALSA失败，将不使用ALSA输出");
                g_use_alsa = 0;
            } else {
                LOGI(TAG, "ALSA初始化成功 (default 设备)，采样率: %d Hz", g_sample_rate);
            }
        } else {
            LOGI(TAG, "ALSA初始化成功 (EDIFIER W820NB)，采样率: %d Hz", g_sample_rate);
        }
    }
    
    if (g_save_file) {
        g_wav_file = fopen(output_filename, "wb");
        if (g_wav_file == NULL) {
            LOGE(TAG, "无法打开输出文件: %s", strerror(errno));
            g_save_file = 0;
        } else {
            write_wav_header(g_wav_file, g_sample_rate);
            LOGI(TAG, "输出文件已打开: %s", output_filename);
        }
    }

    LOGI(TAG, "开始生成音频...");
    LOGI(TAG, "文本内容: %s", text);

    if (g_use_alsa && g_pcm_handle != NULL) {
        snd_pcm_prepare(g_pcm_handle);
    }

    if (generate_tts_audio_with_callback(text, sid, speed, tts_callback, NULL) != 0) {
        LOGE(TAG, "生成音频失败");
        if (g_wav_file != NULL) fclose(g_wav_file);
        if (g_pcm_handle != NULL) snd_pcm_close(g_pcm_handle);
        cleanup_sherpa_tts();
        return -1;
    }

    if (g_use_alsa && g_pcm_handle != NULL) {
        snd_pcm_drain(g_pcm_handle);
    }

    if (g_wav_file != NULL) {
        fclose(g_wav_file);
        g_wav_file = NULL;
    }

    if (g_pcm_handle != NULL) {
        snd_pcm_close(g_pcm_handle);
        g_pcm_handle = NULL;
    }

    printf("\n");
    LOGI(TAG, "========= TTS测试完成 =========\n");
    cleanup_sherpa_tts();

    LOGI(TAG, "TTS测试程序退出");
    return 0;
}
