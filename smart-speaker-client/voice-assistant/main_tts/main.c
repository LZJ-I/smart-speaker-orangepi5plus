#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>

#define LOG_LEVEL 4
#include "../../debug_log.h"
#include "../common/ipc_protocol.h"
#include "../common/alsa.h"
#include "../tts/alsa_output.h"
#include "../tts/sherpa_tts.h"

#define TAG "TTS_MAIN"
#define PLAYBACK_BUFFER_SIZE (16 * 1024)

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

int running = 1;
int tts_fd = -1;

pthread_t playback_thread = 0;
pthread_mutex_t playback_mutex = PTHREAD_MUTEX_INITIALIZER;
int playback_should_stop = 0;
float *playback_samples = NULL;
int32_t playback_n = 0;
char *playback_wav_filename = NULL;

void handle_ipc_message(IPCMessage *msg);

static void check_and_process_stop_command(void) {
    fd_set readfds;
    struct timeval timeout;
    
    FD_ZERO(&readfds);
    FD_SET(tts_fd, &readfds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;
    
    int ret = select(tts_fd + 1, &readfds, NULL, NULL, &timeout);
    if (ret > 0 && FD_ISSET(tts_fd, &readfds)) {
        IPCMessage msg;
        size_t total_read = 0;
        uint8_t *ptr = (uint8_t *)&msg;
        memset(&msg, 0, sizeof(msg));
        
        while (total_read < sizeof(msg)) {
            ssize_t read_ret = read(tts_fd, ptr + total_read, sizeof(msg) - total_read);
            if (read_ret < 0) {
                if (errno == EINTR) continue;
                return;
            } else if (read_ret == 0) {
                return;
            }
            total_read += read_ret;
        }
        
        if (total_read == sizeof(msg)) {
            LOGD(TAG, "check_and_process_stop_command 收到消息，类型: %d", msg.type);
            handle_ipc_message(&msg);
        }
    }
}

void sigint_handler(int signum) {
    LOGI(TAG, "收到退出信号，正在清理资源...");
    
    pthread_mutex_lock(&playback_mutex);
    playback_should_stop = 1;
    pthread_mutex_unlock(&playback_mutex);
    
    if (playback_thread != 0) {
        pthread_join(playback_thread, NULL);
    }
    
    if (tts_fd != -1) {
        close(tts_fd);
    }
    
    pthread_mutex_destroy(&playback_mutex);
    
    if (playback_samples != NULL) {
        destroy_generated_audio(playback_samples);
        playback_samples = NULL;
    }
    if (playback_wav_filename != NULL) {
        free(playback_wav_filename);
        playback_wav_filename = NULL;
    }
    
    cleanup_alsa_output();
    cleanup_sherpa_tts();
    LOGI(TAG, "TTS进程退出");
    running = 0;
    exit(0);
}

void sigusr1_handler(int signum) {
    LOGI(TAG, "收到SIGUSR1信号，停止当前播放");
    pthread_mutex_lock(&playback_mutex);
    playback_should_stop = 1;
    pthread_mutex_unlock(&playback_mutex);
}

static void* playback_text_thread(void *arg) {
    LOGI(TAG, "播放线程启动");
    
    float *samples = NULL;
    int32_t n = 0;
    
    pthread_mutex_lock(&playback_mutex);
    samples = playback_samples;
    n = playback_n;
    playback_samples = NULL;
    playback_n = 0;
    pthread_mutex_unlock(&playback_mutex);
    
    if (samples == NULL || n == 0) {
        LOGE(TAG, "没有可播放的音频数据");
        return NULL;
    }
    
    if (g_pcm_handle != NULL) {
        if (snd_pcm_prepare(g_pcm_handle) < 0) {
            LOGE(TAG, "准备PCM设备失败");
            destroy_generated_audio(samples);
            return NULL;
        }
    }
    
    int16_t *processed_samples = malloc(PERIOD_SIZE * sizeof(int16_t));
    if (processed_samples == NULL) {
        LOGE(TAG, "分配内存失败");
        destroy_generated_audio(samples);
        return NULL;
    }
    
    int32_t offset = 0;
    int should_stop_local = 0;
    
    while (offset < n && !should_stop_local) {
        pthread_mutex_lock(&playback_mutex);
        should_stop_local = playback_should_stop;
        pthread_mutex_unlock(&playback_mutex);
        
        if (should_stop_local) {
            break;
        }
        
        int32_t samples_to_process = (n - offset) > PERIOD_SIZE ? PERIOD_SIZE : (n - offset);
        
        float t;
        for (int32_t i = 0; i < samples_to_process; i++) {
            t = samples[offset + i];
            if (t < -1.0f) t = -1.0f;
            else if (t > 1.0f) t = 1.0f;
            processed_samples[i] = (int16_t)(t * 32767);
        }
        
        int ret = snd_pcm_writei(g_pcm_handle, processed_samples, samples_to_process);
        if (ret == -EPIPE) {
            snd_pcm_prepare(g_pcm_handle);
            ret = snd_pcm_writei(g_pcm_handle, processed_samples, samples_to_process);
        }
        if (ret < 0) {
            LOGE(TAG, "写入PCM失败: %s", snd_strerror(ret));
            break;
        }
        
        offset += samples_to_process;
    }
    
    pthread_mutex_lock(&playback_mutex);
    should_stop_local = playback_should_stop;
    pthread_mutex_unlock(&playback_mutex);
    
    if (!should_stop_local && g_pcm_handle != NULL) {
        snd_pcm_drain(g_pcm_handle);
    }
    
    free(processed_samples);
    destroy_generated_audio(samples);
    
    if (should_stop_local) {
        LOGI(TAG, "文本播放已被打断");
    } else {
        LOGI(TAG, "文本播放完成");
    }
    
    return NULL;
}

static void* playback_wav_thread(void *arg) {
    LOGI(TAG, "WAV播放线程启动");
    
    char *filename = NULL;
    
    pthread_mutex_lock(&playback_mutex);
    filename = playback_wav_filename;
    playback_wav_filename = NULL;
    pthread_mutex_unlock(&playback_mutex);
    
    if (filename == NULL) {
        LOGE(TAG, "没有WAV文件名");
        return NULL;
    }
    
    LOGI(TAG, "开始播放WAV文件: %s", filename);
    FILE *wav_file = fopen(filename, "rb");
    if (wav_file == NULL) {
        LOGE(TAG, "无法打开文件: %s", filename);
        free(filename);
        return NULL;
    }
    
    WavHeader header;
    size_t read_size = fread(&header, 1, sizeof(WavHeader), wav_file);
    if (read_size != sizeof(WavHeader)) {
        LOGE(TAG, "读取WAV头失败");
        fclose(wav_file);
        free(filename);
        return NULL;
    }
    
    if (memcmp(header.riff, "RIFF", 4) != 0 || memcmp(header.wave, "WAVE", 4) != 0) {
        LOGE(TAG, "不是有效的WAV文件: %s", filename);
        fclose(wav_file);
        free(filename);
        return NULL;
    }
    
    if (header.audio_format != 1) {
        LOGE(TAG, "不支持的音频格式: %d", header.audio_format);
        fclose(wav_file);
        free(filename);
        return NULL;
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
        free(filename);
        return NULL;
    }
    
    uint32_t bytes_per_sample = header.bits_per_sample / 8;
    uint32_t buffer_size_bytes = PLAYBACK_BUFFER_SIZE * bytes_per_sample;
    uint8_t *buffer = (uint8_t *)malloc(buffer_size_bytes);
    
    if (buffer == NULL) {
        LOGE(TAG, "内存分配失败");
        fclose(wav_file);
        free(filename);
        return NULL;
    }
    
    uint32_t total_bytes_read = 0;
    LOGI(TAG, "开始播放 (总数据大小: %u bytes, 块大小: %u bytes)", chunk_size, buffer_size_bytes);
    
    int should_stop_local = 0;
    while (total_bytes_read < chunk_size && !should_stop_local) {
        pthread_mutex_lock(&playback_mutex);
        should_stop_local = playback_should_stop;
        pthread_mutex_unlock(&playback_mutex);
        
        if (should_stop_local) {
            break;
        }
        
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
    
    pthread_mutex_lock(&playback_mutex);
    should_stop_local = playback_should_stop;
    pthread_mutex_unlock(&playback_mutex);
    
    if (!should_stop_local && g_pcm_handle != NULL) {
        snd_pcm_drain(g_pcm_handle);
    }
    
    free(buffer);
    fclose(wav_file);
    free(filename);
    
    if (should_stop_local) {
        LOGI(TAG, "WAV播放已停止");
    } else {
        LOGI(TAG, "WAV播放完成");
    }
    
    return NULL;
}

void play_wake_response(void) {
    const char *wake_files[] = {
        "./voice-assistant/wake_audio/nihao.wav",
        "./voice-assistant/wake_audio/wozai.wav",
        "./voice-assistant/wake_audio/zaine.wav"
    };
    int num_files = 3;

    srand((unsigned int)time(NULL));
    int idx = rand() % num_files;

    LOGI(TAG, "播放唤醒响应: %s", wake_files[idx]);
    
    if (access(wake_files[idx], F_OK) == 0) {
        if (g_pcm_handle != NULL) {
            if (snd_pcm_prepare(g_pcm_handle) < 0) {
                LOGE(TAG, "准备PCM设备失败");
                return;
            }
        }
        
        pthread_mutex_lock(&playback_mutex);
        playback_should_stop = 0;
        if (playback_wav_filename != NULL) {
            free(playback_wav_filename);
        }
        playback_wav_filename = strdup(wake_files[idx]);
        pthread_mutex_unlock(&playback_mutex);
        
        if (playback_thread != 0) {
            pthread_join(playback_thread, NULL);
            playback_thread = 0;
        }
        
        pthread_create(&playback_thread, NULL, playback_wav_thread, NULL);
    } else {
        LOGW(TAG, "唤醒音频文件不存在: %s", wake_files[idx]);
    }
}

void handle_ipc_message(IPCMessage *msg) {
    switch (msg->type) {
        case IPC_CMD_PLAY_TEXT:
            LOGI(TAG, "收到播放文本命令: %s", msg->text);
            LOGD(TAG, "文本长度: %zu", strlen(msg->text));
            if (strlen(msg->text) == 0) {
                LOGW(TAG, "文本为空，跳过播放");
                return;
            }
            
            pthread_mutex_lock(&playback_mutex);
            playback_should_stop = 1;
            pthread_mutex_unlock(&playback_mutex);
            
            if (playback_thread != 0) {
                pthread_join(playback_thread, NULL);
                playback_thread = 0;
            }
            
            float *samples = NULL;
            int32_t n = 0;
            uint32_t sample_rate = 0;
            
            if (generate_tts_audio_full(msg->text, &samples, &n, &sample_rate) != 0) {
                LOGE(TAG, "生成TTS音频失败");
                return;
            }
            
            LOGI(TAG, "TTS音频生成完成，样本数: %d, 采样率: %u", n, sample_rate);
            
            pthread_mutex_lock(&playback_mutex);
            playback_should_stop = 0;
            if (playback_samples != NULL) {
                destroy_generated_audio(playback_samples);
            }
            playback_samples = samples;
            playback_n = n;
            pthread_mutex_unlock(&playback_mutex);
            
            pthread_create(&playback_thread, NULL, playback_text_thread, NULL);
            break;

        case IPC_CMD_PLAY_AUDIO_FILE:
            LOGI(TAG, "收到播放音频文件命令: %s", msg->filename);
            if (access(msg->filename, F_OK) == 0) {
                char cmd[512];
                snprintf(cmd, sizeof(cmd), "aplay %s 2>/dev/null", msg->filename);
                system(cmd);
            }
            break;

        case IPC_CMD_STOP_PLAYING:
            LOGI(TAG, "收到停止播放命令");
            pthread_mutex_lock(&playback_mutex);
            playback_should_stop = 1;
            pthread_mutex_unlock(&playback_mutex);
            
            if (g_pcm_handle != NULL) {
                snd_pcm_drop(g_pcm_handle);
            }
            break;

        case IPC_CMD_PLAY_WAKE_RESPONSE:
            LOGI(TAG, "收到播放唤醒响应命令");
            
            pthread_mutex_lock(&playback_mutex);
            playback_should_stop = 1;
            pthread_mutex_unlock(&playback_mutex);
            
            if (playback_thread != 0) {
                pthread_join(playback_thread, NULL);
                playback_thread = 0;
            }
            
            play_wake_response();
            break;

        default:
            LOGW(TAG, "收到未知命令: %d", msg->type);
            break;
    }
}

int open_pipes(void) {
    mkdir(FIFO_DIR_PATH, 0777);
    if (access(TTS_FIFO_PATH, F_OK) == -1) {
        if (mkfifo(TTS_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
            LOGE(TAG, "创建TTS管道失败: %s", strerror(errno));
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
    LOGI(TAG, "=== TTS进程启动 ===");

    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        LOGE(TAG, "注册SIGINT信号处理失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (signal(SIGUSR1, sigusr1_handler) == SIG_ERR) {
        LOGE(TAG, "注册SIGUSR1信号处理失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (init_sherpa_tts_with_chunk_size(1) != 0) {
        LOGE(TAG, "初始化TTS模型失败");
        return -1;
    }

    char playback_device[256] = "default";
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

    LOGD(TAG, "IPCMessage 大小: %zu bytes", sizeof(IPCMessage));

    IPCMessage msg;
    int consecutive_zero_reads = 0;
    const int MAX_CONSECUTIVE_ZERO_READS = 5;
    
    while (running) {
        memset(&msg, 0, sizeof(msg));
        size_t total_read = 0;
        uint8_t *ptr = (uint8_t *)&msg;
        
        while (total_read < sizeof(msg)) {
            ssize_t ret = read(tts_fd, ptr + total_read, sizeof(msg) - total_read);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                LOGE(TAG, "读取管道失败: %s", strerror(errno));
                break;
            } else if (ret == 0) {
                consecutive_zero_reads++;
                LOGD(TAG, "read() 返回 0，次数: %d", consecutive_zero_reads);
                usleep(10000);
                
                if (consecutive_zero_reads >= MAX_CONSECUTIVE_ZERO_READS) {
                    LOGI(TAG, "多次 read() 返回 0，认为管道对端已关闭，TTS 进程退出");
                    running = 0;
                    break;
                }
                continue;
            }
            
            consecutive_zero_reads = 0;
            total_read += ret;
            LOGD(TAG, "已读取 %zu/%zu 字节", total_read, sizeof(msg));
        }
        
        if (!running) {
            break;
        }
        
        if (total_read == sizeof(msg)) {
            LOGD(TAG, "收到完整消息，类型: %d", msg.type);
            handle_ipc_message(&msg);
        }
    }

    return 0;
}
