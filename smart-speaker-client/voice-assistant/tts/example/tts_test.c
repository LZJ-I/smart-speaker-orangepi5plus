#define LOG_LEVEL 4
#include "../../debug_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include "../sherpa_tts.h"
#include "../alsa_output.h"

#define TAG "TTS-TEST"

typedef enum {
    MODE_SINGLE_FILE,
    MODE_SINGLE_ALSA
} OutputMode;

typedef struct {
    OutputMode mode;
    const char* text;
    const char* output_file;
    const char* alsa_device;
} TTSConfig;

int g_running = 1;
FILE* g_wav_file = NULL;
int32_t g_total_samples = 0;
uint32_t g_sample_rate = 0;
struct timespec g_start_time;
TTSConfig* g_config = NULL;

void signal_handler(int sig) {
    LOGI(TAG, "收到信号 %d，正在清理资源...", sig);
    g_running = 0;
}

void write_wav_header(FILE* file, uint32_t sample_rate, uint32_t data_size) {
    uint8_t header[44];
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

void convert_to_pcm(const float* samples, int16_t* pcm_data, int32_t n) {
    for (int32_t i = 0; i < n; i++) {
        float s = samples[i];
        if (s < -1.0f) s = -1.0f;
        if (s > 1.0f) s = 1.0f;
        pcm_data[i] = (int16_t)(s * 32767.0f);
    }
}

void play_audio(int16_t* pcm_data, int32_t n) {
    if (g_pcm_handle == NULL) return;
    
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

void print_config(TTSConfig* config) {
    LOGI(TAG, "================ 配置信息 ================");
    LOGI(TAG, "模式: %s", 
           config->mode == MODE_SINGLE_FILE ? "单次输出到文件" :
           "单次输出到ALSA");
    LOGI(TAG, "文本: %s", config->text);
    if (config->output_file) {
        LOGI(TAG, "输出文件: %s", config->output_file);
    }
    if (config->alsa_device) {
        LOGI(TAG, "ALSA设备: %s", config->alsa_device);
    }
    LOGI(TAG, "==========================================");
}

int init_output(TTSConfig* config) {
    if (config->alsa_device && config->mode == MODE_SINGLE_ALSA) {
        LOGI(TAG, "正在初始化ALSA设备: %s", config->alsa_device);
        if (init_alsa_output(g_sample_rate, (char*)config->alsa_device) != 0) {
            LOGE(TAG, "ALSA初始化失败，请使用 aplay -l 查看可用设备");
            return -1;
        }
        LOGI(TAG, "ALSA初始化成功");
    }
    
    return 0;
}

void cleanup_output(void) {
    if (g_pcm_handle != NULL) {
        if (g_config->mode == MODE_SINGLE_ALSA) {
            snd_pcm_drain(g_pcm_handle);
        }
        snd_pcm_close(g_pcm_handle);
        g_pcm_handle = NULL;
    }
}

void print_usage(const char* program_name) {
    printf("用法: %s [选项]\n", program_name);
    printf("\n选项:\n");
    printf("  -m, --mode <模式>    输出模式 (1-2)\n");
    printf("                       1: 单次输出到文件 (默认)\n");
    printf("                       2: 单次输出到ALSA\n");
    printf("  -t, --text <文本>    要合成的文本\n");
    printf("  -o, --output <文件>  输出文件 (默认: generated.wav)\n");
    printf("  -d, --device <设备>  ALSA设备 (使用 aplay -l 查看)\n");
    printf("  -h, --help          显示帮助信息\n");
    printf("\n示例:\n");
    printf("  %s                                      # 使用默认设置\n", program_name);
    printf("  %s -m 2 -t \"你好世界\" -d hw:5,0    # 输出到指定ALSA设备\n", program_name);
}

int parse_args(int argc, char* argv[], TTSConfig* config) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return -2;
        } else if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mode") == 0) && i + 1 < argc) {
            int mode = atoi(argv[++i]);
            if (mode >= 1 && mode <= 2) {
                config->mode = (OutputMode)(mode - 1);
            }
        } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--text") == 0) && i + 1 < argc) {
            config->text = argv[++i];
        } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            config->output_file = argv[++i];
        } else if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--device") == 0) && i + 1 < argc) {
            config->alsa_device = argv[++i];
        }
    }
    return 0;
}

int main(int argc, char* argv[]) {
    TTSConfig config = {
        .mode = MODE_SINGLE_FILE,
        .text = "你好，这是一个智能音箱测试。今天天气不错，适合出去走走。",
        .output_file = "generated.wav",
        .alsa_device = NULL
    };
    
    g_config = &config;
    
    int parse_result = parse_args(argc, argv, &config);
    if (parse_result == -2) {
        return 0;
    }
    
    if (signal(SIGINT, signal_handler) == SIG_ERR || 
        signal(SIGTERM, signal_handler) == SIG_ERR) {
        LOGE(TAG, "注册信号处理失败: %s", strerror(errno));
        return -1;
    }
    
    LOGI(TAG, "======== TTS测试程序启动 ========");
    print_config(&config);
    
    if (init_sherpa_tts() != 0) {
        LOGE(TAG, "TTS模型初始化失败");
        return -1;
    }
    g_sample_rate = g_tts_sample_rate;
    LOGI(TAG, "TTS模型加载完成，采样率: %d Hz", g_sample_rate);
    
    if (init_output(&config) != 0) {
        cleanup_sherpa_tts();
        return -1;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &g_start_time);
    
    LOGI(TAG, "开始生成音频...");
    
    int result = 0;
    float* samples = NULL;
    int32_t n = 0;
    uint32_t sr = 0;
    
    result = generate_tts_audio_full(config.text, &samples, &n, &sr);
    
    if (result == 0 && samples != NULL) {
        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double elapsed = (end_time.tv_sec - g_start_time.tv_sec) + 
                        (end_time.tv_nsec - g_start_time.tv_nsec) / 1e9;
        LOGI(TAG, ">>> 完整音频生成完成！耗时: %.3f 秒 <<<", elapsed);
        
        int16_t* pcm_data = (int16_t*)malloc(n * sizeof(int16_t));
        convert_to_pcm(samples, pcm_data, n);
        
        if (config.mode == MODE_SINGLE_ALSA) {
            LOGI(TAG, "正在播放...");
            play_audio(pcm_data, n);
        }
        if (config.mode == MODE_SINGLE_FILE) {
            FILE* file = fopen(config.output_file, "wb");
            if (file != NULL) {
                write_wav_header(file, sr, n * 2);
                fwrite(pcm_data, sizeof(int16_t), n, file);
                fclose(file);
                LOGI(TAG, "音频已保存到: %s，总计: %d 样本 (%.2f 秒)", 
                       config.output_file, n, (float)n / sr);
            } else {
                LOGE(TAG, "无法打开输出文件: %s", strerror(errno));
                result = -1;
            }
        }
        
        free(pcm_data);
        destroy_generated_audio(samples);
    }
    
    if (result != 0) {
        LOGE(TAG, "音频生成失败");
    } else {
        LOGI(TAG, "音频生成完成！");
    }
    
    cleanup_output();
    cleanup_sherpa_tts();
    
    LOGI(TAG, "======== TTS测试程序退出 ========");
    return result;
}
