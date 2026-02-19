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
#define PLAYBACK_BUFFER_SIZE (16 * 1024)

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

static void print_usage(const char *program) {
    printf("用法: %s <wav文件> [ALSA设备]\n", program);
    printf("  ALSA设备: 可选，默认为 default\n");
    printf("示例: %s test.wav hw:0,0\n", program);
}

static int play_wav_file(const char *filename, const char *alsa_device) {
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
    
    if (init_alsa_output(header.sample_rate, (char*)alsa_device) != 0) {
        LOGE(TAG, "ALSA初始化失败");
        fclose(wav_file);
        return -1;
    }
    
    uint32_t bytes_per_sample = header.bits_per_sample / 8;
    uint32_t buffer_size_bytes = PLAYBACK_BUFFER_SIZE * bytes_per_sample;
    uint8_t *buffer = (uint8_t *)malloc(buffer_size_bytes);
    
    if (buffer == NULL) {
        LOGE(TAG, "内存分配失败");
        fclose(wav_file);
        cleanup_alsa_output();
        return -1;
    }
    
    uint32_t total_bytes_read = 0;
    LOGI(TAG, "开始播放 (总数据大小: %u bytes, 块大小: %u bytes)", chunk_size, buffer_size_bytes);
    
    while (total_bytes_read < chunk_size && !g_stop_playback) {
        uint32_t bytes_to_read = (chunk_size - total_bytes_read) < buffer_size_bytes ? 
                                  (chunk_size - total_bytes_read) : buffer_size_bytes;
        
        size_t bytes_read = fread(buffer, 1, bytes_to_read, wav_file);
        
        if (bytes_read != bytes_to_read) {
            LOGE(TAG, "读取音频数据失败 (已读 %zu, 预期 %u)", bytes_read, bytes_to_read);
            break;
        }
        
        int32_t num_samples = bytes_read / bytes_per_sample;
        int ret = snd_pcm_writei(g_pcm_handle, buffer, num_samples);
        
        if (ret == -EPIPE) {
            snd_pcm_prepare(g_pcm_handle);
            ret = snd_pcm_writei(g_pcm_handle, buffer, num_samples);
        }
        
        if (ret < 0) {
            LOGE(TAG, "ALSA写入失败: %s", snd_strerror(ret));
            break;
        }
        
        total_bytes_read += bytes_read;
    }
    
    if (!g_stop_playback) {
        snd_pcm_drain(g_pcm_handle);
    }
    
    free(buffer);
    fclose(wav_file);
    cleanup_alsa_output();
    
    if (g_stop_playback) {
        LOGI(TAG, "播放已停止");
    } else {
        LOGI(TAG, "WAV播放完成");
    }
    
    return g_stop_playback ? -1 : 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return -1;
    }
    
    const char *wav_file = argv[1];
    const char *alsa_device = (argc >= 3) ? argv[2] : "default";
    
    // 取消设置DISPLAY环境变量，防止ALSA尝试连接X11，避免xcb相关输出
    unsetenv("DISPLAY");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    int ret = play_wav_file(wav_file, alsa_device);
    
    return ret;
}
