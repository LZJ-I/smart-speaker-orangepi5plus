#define LOG_LEVEL 4
#include "../../debug_log.h"
#include "alsa.h"
#include <sys/types.h>
#include <sys/wait.h>

// 全局PCM句柄和实际采样率（供main和重采样使用）
snd_pcm_t *g_pcm_handle = NULL;
// 实际采样率（由ALSA初始化后设置）
unsigned int g_actual_rate = 0;

#define TAG "ALSA"

// 缓冲区大小（用于arecord -l输出）
#define BUFFER_SIZE 1024
// 最大设备名称长度（"hw:X,Y"格式）
#define MAX_DEV_NAME 32

/**
 * @brief 检测录音设备并返回指定格式的设备名称
 * 
 * @param target_device 要查找的目标设备名称（如"USB Composite Device"）
 * @param result 用于存储结果的缓冲区，格式为"hw:X,Y"
 * @return int 0表示成功，-1表示失败
 */
int get_audio_device(const char* target_device, char* result) {
    int pipefd[2];
    pid_t pid;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    int status;
    
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
        
        // 执行arecord -l命令
        execlp("arecord", "arecord", "-l", (char*)NULL);

        // 如果execlp失败
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
        // printf("[Debug] arecord -l 输出:\n%s", buffer);
        close(pipefd[0]);
        
        // 等待子进程结束
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            LOGE(TAG, "arecord command failed");
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
                            snprintf(result, MAX_DEV_NAME, "plughw:%d,%d", card, device);
                            return 0;  // 成功
                        }
                    }
                }
            }
            line = strtok(NULL, "\n");
        }
        
        // 如果没有找到目标设备
        LOGE(TAG, "Device '%s' not found", target_device);
        return -1;
    }
}


// ALSA初始化
int init_alsa(void)
{
    int ret = 0;
    // 0. 查找录音设备
    char record_device[MAX_DEV_NAME];
    if(get_audio_device(RECORD_DEVICE, record_device) != 0)
    {
        LOGE(TAG, "查找录音设备失败: %s", RECORD_DEVICE);
        return -1;
    }
    LOGI(TAG, "找到录音设备: %s", record_device);

    // 1. 打开PCM录音设备（阻塞模式）
    ret = snd_pcm_open(&g_pcm_handle, record_device, SND_PCM_STREAM_CAPTURE, 0);
    if(ret < 0)
    {
        LOGE(TAG, "打开PCM设备失败: %s", snd_strerror(ret));
        return -1;
    }
    
    // 2. 初始化硬件参数结构体
    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    if(snd_pcm_hw_params_any(g_pcm_handle, params) < 0)
    {
        LOGE(TAG, "初始化硬件参数结构体失败");
        snd_pcm_close(g_pcm_handle);
        return -1;
    }

    // 3. 设置访问模式：多声道交错存储（单声道无影响，兼容标准）
    if(snd_pcm_hw_params_set_access(g_pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
    {
        LOGE(TAG, "设置访问模式失败");
        snd_pcm_close(g_pcm_handle);
        return -1;
    }

    // 4. 设置采样格式：16位有符号小端（S16_LE，模型常用格式）
    if(snd_pcm_hw_params_set_format(g_pcm_handle, params, SND_PCM_FORMAT_S16_LE) < 0)
    {
        LOGE(TAG, "设置采样格式失败");
        snd_pcm_close(g_pcm_handle);
        return -1;
    }    

    // 5. 设置声道数：尝试动态回退
    int channels = CHANNELS;
    if(snd_pcm_hw_params_set_channels(g_pcm_handle, params, channels) < 0)
    {
        LOGW(TAG, "当前设备不支持 %d 通道，尝试切换到 2 通道", channels);
        channels = 2;
        if(snd_pcm_hw_params_set_channels(g_pcm_handle, params, channels) < 0)
        {
            LOGE(TAG, "设置声道数失败（尝试了1和2通道）");
            snd_pcm_close(g_pcm_handle);
            return -1;
        }
    }

    // 6. 设置采样率（取硬件支持的最近值）
    g_actual_rate = RATE;
    if(snd_pcm_hw_params_set_rate_near(g_pcm_handle, params, &g_actual_rate, 0) < 0)
    {
        LOGE(TAG, "设置采样率失败");
        snd_pcm_close(g_pcm_handle);
        return -1;
    }
    if(g_actual_rate != RATE)
    {
        LOGW(TAG, "实际采样率: %d Hz, 请求采样率: %d Hz（重采样会自动适配）", g_actual_rate, RATE);
    }

    // 7. 设置缓冲区周期大小（减少延迟，实时识别关键）
    snd_pcm_uframes_t period_size = PERIOD_SIZE;
    if(snd_pcm_hw_params_set_period_size_near(g_pcm_handle, params, &period_size, 0) < 0)
    {
        LOGE(TAG, "设置缓冲区周期大小失败");
        snd_pcm_close(g_pcm_handle);
        return -1;
    }
    if(period_size != PERIOD_SIZE)
    {
        LOGW(TAG, "实际周期大小: %ld, 请求周期大小: %d", period_size, PERIOD_SIZE);
    }

    // 8. 应用参数并准备设备
    if(snd_pcm_hw_params(g_pcm_handle, params) < 0)
    {
        LOGE(TAG, "应用参数失败");
        snd_pcm_close(g_pcm_handle);
        return -1;
    }

    // 确认实际生效的采样率
    unsigned int confirmed_rate = 0;
    int dir = 0;
    if (snd_pcm_hw_params_get_rate(params, &confirmed_rate, &dir) == 0)
    {
        LOGI(TAG, "ALSA 实际采样率：%u Hz (dir=%d)", confirmed_rate, dir);
        g_actual_rate = confirmed_rate;
    }
    else
    {
        LOGI(TAG, "无法从参数中获取实际采样率，使用设置值：%u Hz", g_actual_rate);
    }

    if(snd_pcm_prepare(g_pcm_handle) < 0)
    {
        LOGE(TAG, "准备设备失败");
        snd_pcm_close(g_pcm_handle);
        return -1;
    }

    return 0;
}

// ALSA资源清理
void cleanup_alsa(void)
{
    if(g_pcm_handle != NULL)
    {
        snd_pcm_drain(g_pcm_handle);  // 等待缓冲区数据处理完成
        snd_pcm_close(g_pcm_handle);  // 关闭PCM设备
        g_pcm_handle = NULL;
    }
    LOGI(TAG, "ALSA资源清理完成");
}
