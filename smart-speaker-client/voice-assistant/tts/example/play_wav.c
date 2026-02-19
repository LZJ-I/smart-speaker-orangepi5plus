#define LOG_LEVEL 3
#include "../../debug_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

#include "../alsa_output.h"

#define TAG "WAV-PLAYER"

static volatile int g_stop_playback = 0;

static void signal_handler(int sig) {
    g_stop_playback = 1;
    if (g_pcm_handle != NULL) {
        snd_pcm_drop(g_pcm_handle);
    }
}

typedef struct {
    char riff[4];
    uint32_t file_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} WavHeader;

static int play_wav_file(const char *filename) {
    LOGI(TAG, "开始播放WAV文件: %s", filename);
    FILE *wav_file = fopen(filename, "rb");
    if (wav_file == NULL) {
        LOGE(TAG, "无法打开文件: %s", filename);
        return -1;
    }
    
    WavHeader header;
    size_t read_size = fread(&header, 1, sizeof(WavHeader), wav_file);
    if (read_size != sizeof(WavHeader)) {
        LOGE(TAG, "读取WAV头失败");
        fclose(wav_file);
        return -1;
    }
    
    if (memcmp(header.riff, "RIFF", 4) != 0 || memcmp(header.wave, "WAVE", 4) != 0) {
        LOGE(TAG, "不是有效的WAV文件: %s", filename);
        fclose(wav_file);
        return -1;
    }
    
    if (header.audio_format != 1) {
        LOGE(TAG, "不支持的音频格式: %d", header.audio_format);
        fclose(wav_file);
        return -1;
    }
    
    LOGI(TAG, "WAV文件信息: 采样率=%d Hz, 声道数=%d, 位深=%d bit",
           header.sample_rate, header.num_channels, header.bits_per_sample);
    
    char chunk_id[5] = {0};
    uint32_t chunk_size;
    
    while (1) {
        if (fread(chunk_id, 1, 4, wav_file) != 4) {
            break;
        }
        if (fread(&chunk_size, 1, 4, wav_file) != 4) {
            break;
        }
        
        if (memcmp(chunk_id, "data", 4) == 0) {
            break;
        }
        
        fseek(wav_file, chunk_size, SEEK_CUR);
    }
    
    if (memcmp(chunk_id, "data", 4) != 0) {
        LOGE(TAG, "未找到data块");
        fclose(wav_file);
        return -1;
    }
    
    if (init_alsa_output(header.sample_rate, "default") != 0) {
        LOGE(TAG, "ALSA初始化失败");
        fclose(wav_file);
        return -1;
    }
    
    uint32_t bytes_per_sample = header.bits_per_sample / 8;
    
    int16_t *buffer = (int16_t *)malloc(chunk_size);
    if (buffer == NULL) {
        LOGE(TAG, "内存分配失败");
        fclose(wav_file);
        cleanup_alsa_output();
        return -1;
    }
    
    size_t bytes_read = fread(buffer, 1, chunk_size, wav_file);
    fclose(wav_file);
    
    if (bytes_read != chunk_size) {
        LOGE(TAG, "读取音频数据不完整");
        free(buffer);
        cleanup_alsa_output();
        return -1;
    }
    
    int32_t num_samples = chunk_size / bytes_per_sample;
    
    LOGD(TAG, "准备播放 %d 个采样点，时长: %.2f 秒", num_samples, (double)num_samples / header.sample_rate);
    
    int ret = snd_pcm_writei(g_pcm_handle, buffer, num_samples);
    if (ret == -EPIPE) {
        snd_pcm_prepare(g_pcm_handle);
        ret = snd_pcm_writei(g_pcm_handle, buffer, num_samples);
    }
    if (ret < 0) {
        LOGE(TAG, "ALSA写入失败: %s", snd_strerror(ret));
        free(buffer);
        cleanup_alsa_output();
        return -1;
    }
    
    double play_time_seconds = (double)num_samples / header.sample_rate;
    int sleep_us = (int)(play_time_seconds * 1000000) + 500000;
    usleep(sleep_us);
    
    snd_pcm_drain(g_pcm_handle);
    
    free(buffer);
    cleanup_alsa_output();
    
    LOGI(TAG, "WAV播放完成");
    return 0;
}

static void print_usage(const char *program) {
    printf("用法: %s <wav文件>\n", program);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return -1;
    }
    
    const char *wav_file = argv[1];
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGUSR1, signal_handler);
    
    int ret = play_wav_file(wav_file);
    
    return ret;
}
