#include "alsa_output.h"
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

snd_pcm_t *g_pcm_handle = NULL;
unsigned int g_alsa_playback_rate = 0;

void cleanup_alsa_output(void) {
    if (g_pcm_handle != NULL) {
        snd_pcm_drain(g_pcm_handle);
        snd_pcm_close(g_pcm_handle);
        g_pcm_handle = NULL;
    }
}

int pcm_write_all(snd_pcm_t *pcm, const int16_t *buf, snd_pcm_uframes_t frames) {
    int zero_write_retries = 0;
    while (frames > 0) {
        snd_pcm_sframes_t n = snd_pcm_writei(pcm, buf, frames);
        if (n == -EPIPE) {
            snd_pcm_prepare(pcm);
            continue;
        }
        if (n < 0) {
            n = snd_pcm_recover(pcm, (int)n, 1);
            if (n < 0) return (int)n;
            continue;
        }
        if (n == 0) {
            if (++zero_write_retries >= 5) {
                return -EIO;
            }
            snd_pcm_wait(pcm, 100);
            continue;
        }
        zero_write_retries = 0;
        buf += n * CHANNELS;
        frames -= (snd_pcm_uframes_t)n;
    }
    return 0;
}

void pcm_write_silence(snd_pcm_t *pcm) {
    snd_pcm_uframes_t buffer_size = 0, period_size = 0;
    if (snd_pcm_get_params(pcm, &buffer_size, &period_size) < 0 || period_size == 0) {
        return;
    }
    snd_pcm_uframes_t to_write = period_size * 2;
    if (to_write > buffer_size) to_write = buffer_size;
    static int16_t *silence_buf = NULL;
    static snd_pcm_uframes_t silence_buf_frames = 0;
    if (silence_buf_frames < to_write) {
        free(silence_buf);
        silence_buf = (int16_t *)calloc((size_t)to_write * CHANNELS, sizeof(int16_t));
        silence_buf_frames = silence_buf ? to_write : 0;
    }
    if (silence_buf && silence_buf_frames >= to_write) {
        pcm_write_all(pcm, silence_buf, to_write);
    }
}

// aplay -l最大缓冲区大小
#define BUFFER_SIZE 1024
// 最大设备名称长度
#define MAX_DEV_NAME 32

/**
 * @brief 检测播放设备并返回指定格式的设备名称
 * 
 * @param target_device 要查找的目标设备名称（如"audiocodec"）
 * @param result 用于存储结果的缓冲区，格式为"hw:X,Y"
 * @return int 0表示成功，-1表示失败
 */
int get_audio_playback_device(const char* target_device, char* result) {
    int pipefd[2];
    pid_t pid;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    int status;
    
    if(strstr(target_device, "default"))
    {
        strcpy(result, "default");
        return 0;
    }

    // 创建管道
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return -1;
    }
    
    // 创建子进程
    pid = fork();
    if (pid == -1) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    
    if (pid == 0) {  // 子进程
        // 重定向标准输出到管道写入端
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        
        // 执行aplay -l命令（播放设备列表）
        execlp("aplay", "aplay", "-l", (char*)NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    } else {  // 父进程
        close(pipefd[1]);  // 关闭管道写入端
        
        // 读取管道内容
        bytes_read = read(pipefd[0], buffer, BUFFER_SIZE - 1);
        if (bytes_read == -1) {
            perror("read");
            close(pipefd[0]);
            waitpid(pid, &status, 0);
            return -1;
        }
        
        buffer[bytes_read] = '\0';  // 添加字符串结束符
        close(pipefd[0]);
        
        // 等待子进程结束
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fprintf(stderr, "aplay command failed\n");
            return -1;
        }
        
        // 解析输出，查找目标设备
        char* line = strtok(buffer, "\n");
        while (line != NULL) {
            // 查找包含"card"和目标设备名称的行
            if (strstr(line, "card") != NULL && strstr(line, target_device) != NULL) {
                int card, device;
                
                // 解析card和device编号
                if (sscanf(line, "card %d:", &card) == 1) {
                    // 查找device编号
                    char* device_str = strstr(line, "device ");
                    if (device_str != NULL) {
                        if (sscanf(device_str, "device %d:", &device) == 1) {
                            // 构造结果字符串
                            snprintf(result, MAX_DEV_NAME, "hw:%d,%d", card, device);
                            return 0;  // 成功
                        }
                    }
                }
            }
            line = strtok(NULL, "\n");
        }
        
        // 如果没有找到目标设备
        fprintf(stderr, "Playback device '%s' not found\n", target_device);
        return -1;
    }
}

int init_alsa_output(unsigned int rate, char* playback_device_name)
{
    int ret = 0;

    // 1. 打开PCM输出设备（阻塞, PLAYBACK模式）
    ret = snd_pcm_open(&g_pcm_handle, playback_device_name, SND_PCM_STREAM_PLAYBACK, 0);
    if(ret < 0)
    {
        fprintf(stderr, "[ERROR] 打开PCM输出设备失败: %s\n", snd_strerror(ret));
        return -1;
    }
    
    // 2. 初始化硬件参数结构体
    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    if(snd_pcm_hw_params_any(g_pcm_handle, params) < 0)
    {
        fprintf(stderr, "[ERROR] 初始化硬件参数结构体失败\n");
        snd_pcm_close(g_pcm_handle);
        return -1;
    }

    // 3. 设置访问模式：多声道交错存储（单声道无影响，兼容标准）
    if(snd_pcm_hw_params_set_access(g_pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
    {
        fprintf(stderr, "[ERROR] 设置访问模式失败\n");
        snd_pcm_close(g_pcm_handle);
        return -1;
    }

    // 4. 设置采样格式：16位有符号小端
    if(snd_pcm_hw_params_set_format(g_pcm_handle, params, SND_PCM_FORMAT_S16_LE) < 0)
    {
        fprintf(stderr, "[ERROR] 设置采样格式失败\n");
        snd_pcm_close(g_pcm_handle);
        return -1;
    }    

    // 5. 设置声道数：单声道
    if(snd_pcm_hw_params_set_channels(g_pcm_handle, params, CHANNELS) < 0)
    {
        fprintf(stderr, "[ERROR] 设置声道数失败\n");
        snd_pcm_close(g_pcm_handle);
        return -1;
    }

    // 6. 设置采样率(就近设置)
    unsigned int actual_rate = rate;
    if(snd_pcm_hw_params_set_rate_near(g_pcm_handle, params, &actual_rate, 0) < 0)
    {
        fprintf(stderr, "[ERROR] 设置采样率失败\n");
        snd_pcm_close(g_pcm_handle);
        return -1;
    }
    if(rate != actual_rate)
        fprintf(stderr, "[WARNING] 实际采样率: %d Hz, 请求采样率: %d Hz（重采样会自动适配）\n", actual_rate, rate);
    g_alsa_playback_rate = actual_rate;

    // 7. 设置缓冲区周期大小（要设置足够的大，因为模型输出的音频数据量较大）
    snd_pcm_uframes_t period_size = PERIOD_SIZE;
    if(snd_pcm_hw_params_set_period_size_near(g_pcm_handle, params, &period_size, 0) < 0)
    {
        fprintf(stderr, "[ERROR] 设置缓冲区周期大小失败\n");
        snd_pcm_close(g_pcm_handle);
        return -1;
    }
    if(period_size != PERIOD_SIZE)
    {
        fprintf(stderr, "[WARNING] 实际周期大小: %ld, 请求周期大小: %d\n", period_size, PERIOD_SIZE);
    }

    // 8. 应用参数并准备设备
    if(snd_pcm_hw_params(g_pcm_handle, params) < 0)
    {
        fprintf(stderr, "[ERROR] 应用参数失败\n");
        snd_pcm_close(g_pcm_handle);
        return -1;
    }

    // // 9. 准备PCM设备（将其状态设置为READY）
    // if(snd_pcm_prepare(g_pcm_handle) < 0)
    // {
    //     fprintf(stderr, "[ERROR] 准备设备失败\n");
    //     snd_pcm_close(g_pcm_handle);
    //     return -1;
    // }

    return 0;
}






