#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define LOG_LEVEL 4
#include "../debug_log.h"
#include "common/alsa.h"
#include "sherpa-onnx/c-api/c-api.h"
#include "asr/sherpa_asr.h"
#include "common/mysamplerate.h"
#include "kws/sherpa_kws.h"

#define TAG "MAIN"

int running = 1;
int asr_fd = -1;
int kws_fd = -1;
int16_t *alsa_buf = NULL;

#define ASR_FIFO_PATH "../fifo/asr_fifo"
#define KWS_FIFO_PATH "../fifo/kws_fifo"

// 模型识别 等待关键词唤醒、 开始识别录音、 处理、 等待结果、 合成语音播放
enum SherpaOnnxState{
    STATE_KWS,  // 关键词识别状态
    STATE_ASR  // 语音识别状态
};

int current_state = STATE_KWS;  // 当前状态，初始为关键词识别状态

enum OnlineMode g_current_online_mode = ONLINE_MODE_YES;  // 初始为在线模式



void sigint_handler(int sig) {
    LOGI(TAG, "收到退出信号，正在清理资源...");
    if(alsa_buf != NULL)
    {
        free(alsa_buf);
    }
    cleanup_sherpa_asr();
    cleanup_resampler();
    cleanup_alsa();
    cleanup_sherpa_kws();
    LOGI(TAG, "语音识别系统退出...");
    running = 0;
    exit(0);
}

static int open_pipes() {
    if (asr_fd != -1) {
        close(asr_fd);
        asr_fd = -1;
    }
    if (kws_fd != -1) {
        close(kws_fd);
        kws_fd = -1;
    }

    mkdir("../fifo", 0777);
    if (access(ASR_FIFO_PATH, F_OK) == -1) {
        if (mkfifo(ASR_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
            LOGW(TAG, "创建ASR管道失败: %s", strerror(errno));
        }
    }
    if (access(KWS_FIFO_PATH, F_OK) == -1) {
        if (mkfifo(KWS_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
            LOGW(TAG, "创建KWS管道失败: %s", strerror(errno));
        }
    }

    asr_fd = open(ASR_FIFO_PATH, O_WRONLY);
    if (asr_fd == -1) {
        LOGW(TAG, "打开ASR管道失败: %s (程序继续运行，不影响识别)", strerror(errno));
    }

    kws_fd = open(KWS_FIFO_PATH, O_WRONLY);
    if (kws_fd == -1) {
        LOGW(TAG, "打开KWS管道失败: %s (程序继续运行，不影响识别)", strerror(errno));
    }

    if (asr_fd != -1 || kws_fd != -1) {
        LOGI(TAG, "管道打开成功！asr_fd=%d, kws_fd=%d", asr_fd, kws_fd);
    }
    return 0;
}

static void offline_handler(int s)
{
    LOGI(TAG, "收到SIGUSR1信号，切换到离线模式");
    g_current_online_mode = ONLINE_MODE_NO;
}
static void online_handler(int s)
{
    LOGI(TAG, "收到SIGUSR2信号，切换到在线模式");
    g_current_online_mode = ONLINE_MODE_YES;
}



// 主函数（整合ALSA+重采样+ASR）
int main(int argc, char const *argv[])
{
    // 注册信号处理函数（捕获Ctrl+C）
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        LOGE(TAG, "注册SIGINT信号处理失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    // 注册user1信号，用于通知asr-kws进程 当前模式为离线模式
    if(signal(SIGUSR1, offline_handler) == SIG_ERR){
        LOGE(TAG, "注册SIGUSR1信号处理失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    // 注册user2信号，用于通知asr-kws进程 当前模式为在线模式
    if(signal(SIGUSR2, online_handler) == SIG_ERR){
        LOGE(TAG, "注册SIGUSR2信号处理失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }


    LOGI(TAG, "=== 系统启动 ===");
    LOGI(TAG, "配置信息：模型采样率=%d Hz，声道数=%d，ALSA周期大小=%d",
            MODEL_SAMPLE_RATE, CHANNELS, PERIOD_SIZE);

    if (init_alsa() != 0) {
        LOGE(TAG, "初始化ALSA失败!");
        running = 0;
    }

    if (init_resampler() != 0) {
        LOGE(TAG, "初始化重采样器失败!");
        running = 0;
    }

    if(!running)    return 0;

    if (init_sherpa_asr() != 0) {
        LOGE(TAG, "初始化语音识别模型失败!");
        running = 0;
    }
    LOGI(TAG, "语音识别模型加载完成");

    if(!running)    goto CLEAR;   
    
    alsa_buf = (int16_t *)malloc(PERIOD_SIZE * CHANNELS * sizeof(int16_t));
    if (alsa_buf == NULL) {
        LOGE(TAG, "分配ALSA录音缓冲区失败（内存不足）");
        running = 0;
    }

    if(!running)    goto CLEAR;   

    if (init_sherpa_kws() != 0) {
        LOGE(TAG, "初始化关键词识别模型失败!");
        running = 0;
    }
    LOGI(TAG, "关键词识别模型加载完成");
    
    open_pipes();
    LOGI(TAG, "=========关键词识别模式=========");


    // 5. 主循环：录音→重采样→ 模型（asr+kws）识别
    // printf("开始录音识别（按Ctrl+C退出）...\n");
    while (running) {
        // 5.1 从ALSA读取音频（int16_t格式）
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

        if(current_state == STATE_ASR && g_current_online_mode == ONLINE_MODE_YES)
        {
            if(process_asr_result(model_audio, model_frames) == 0)
            {
                current_state = STATE_KWS;
                LOGI(TAG, "=========关键词识别模式=========");
            }
        }
        else if(current_state == STATE_KWS)
        {
            char keyword[256] = {0};
            if(process_kws_result(model_audio, model_frames, keyword, sizeof(keyword)))
            {
                LOGI(TAG, "检测到关键词: %s", keyword);
                
                if(g_current_online_mode == ONLINE_MODE_YES)
                {
                    if(kws_fd != -1)
                    {
                        char buf[260] = {0};
                        snprintf(buf, sizeof(buf), "%s\n", keyword);
                        write(kws_fd, buf, strlen(buf));
                    }
                    if(!strcmp(keyword, "小米小米") || !strcmp(keyword, "小刘同学") || !strcmp(keyword, "小爱同学"))
                    {
                        current_state = STATE_ASR;
                        LOGI(TAG, "=========语音识别模式=========");
                        // usleep(1200000);
                    }
                }
                else
                {
                    if(kws_fd != -1)
                    {
                        char buf[260] = {0};
                        snprintf(buf, sizeof(buf), "%s\n", keyword);
                        write(kws_fd, buf, strlen(buf));
                    }
                }
                snd_pcm_drop(g_pcm_handle);
                snd_pcm_prepare(g_pcm_handle);
            }
        }
        
    }

CLEAR:

    free(alsa_buf);
    cleanup_sherpa_asr();
    cleanup_resampler();
    cleanup_alsa();
    cleanup_sherpa_kws();
    LOGI(TAG, "语音识别系统退出...");
    return 0;
}
