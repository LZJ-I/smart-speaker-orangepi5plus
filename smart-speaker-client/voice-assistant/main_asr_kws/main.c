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

#define LOG_LEVEL 4
#include "../../debug_log.h"
#include "../common/ipc_protocol.h"
#include "../common/alsa.h"
#include "../common/mysamplerate.h"
#include "../asr/sherpa_asr.h"
#include "../kws/sherpa_kws.h"
#include "../llm/llm.h"

#define TAG "ASR_KWS_MAIN"

char g_last_asr_text[1024] = {0};
struct timespec g_last_asr_update_time = {0, 0};
int g_asr_result_updated = 0;

int running = 1;
int asr_fd = -1;
int kws_fd = -1;
int tts_fd = -1;
int16_t *alsa_buf = NULL;

enum SherpaOnnxState {
    STATE_KWS,
    STATE_ASR
};

int current_state = STATE_KWS;

enum OnlineMode {
    ONLINE_MODE_NO,
    ONLINE_MODE_YES
};

enum OnlineMode g_current_online_mode = ONLINE_MODE_YES;

#define ASR_TIMEOUT_SECONDS 5

void sigint_handler(int sig) {
    LOGI(TAG, "收到退出信号，正在清理资源...");
    if (alsa_buf != NULL) {
        free(alsa_buf);
    }
    if (asr_fd != -1) close(asr_fd);
    if (kws_fd != -1) close(kws_fd);
    if (tts_fd != -1) close(tts_fd);
    cleanup_sherpa_asr();
    cleanup_resampler();
    cleanup_alsa();
    cleanup_sherpa_kws();
    LOGI(TAG, "ASR+KWS进程退出");
    running = 0;
    exit(0);
}

static void offline_handler(int s) {
    LOGI(TAG, "收到SIGUSR1信号，切换到离线模式");
    g_current_online_mode = ONLINE_MODE_NO;
}

static void online_handler(int s) {
    LOGI(TAG, "收到SIGUSR2信号，切换到在线模式");
    g_current_online_mode = ONLINE_MODE_YES;
}

static int open_pipes(void) {
    if (asr_fd != -1) {
        close(asr_fd);
        asr_fd = -1;
    }
    if (kws_fd != -1) {
        close(kws_fd);
        kws_fd = -1;
    }
    if (tts_fd != -1) {
        close(tts_fd);
        tts_fd = -1;
    }

    mkdir(FIFO_DIR_PATH, 0777);
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
    if (access(TTS_FIFO_PATH, F_OK) == -1) {
        if (mkfifo(TTS_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
            LOGW(TAG, "创建TTS管道失败: %s", strerror(errno));
        }
    }

    LOGD(TAG, "尝试打开ASR管道");
    //asr_fd = open(ASR_FIFO_PATH, O_WRONLY);
    LOGD(TAG, "尝试打开KWS管道");
    //kws_fd = open(KWS_FIFO_PATH, O_WRONLY);
    LOGD(TAG, "尝试打开TTS管道");
    tts_fd = open(TTS_FIFO_PATH, O_WRONLY);

    if (asr_fd != -1 || kws_fd != -1 || tts_fd != -1) {
        LOGI(TAG, "管道打开成功！asr_fd=%d, kws_fd=%d, tts_fd=%d", asr_fd, kws_fd, tts_fd);
    }
    return 0;
}

int send_tts_command(IPCCommandType type, const char *text, const char *filename) {
    if (tts_fd == -1) {
        LOGW(TAG, "TTS管道未打开");
        return -1;
    }

    IPCMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = type;
    if (text != NULL) {
        strncpy(msg.text, text, sizeof(msg.text) - 1);
    }
    if (filename != NULL) {
        strncpy(msg.filename, filename, sizeof(msg.filename) - 1);
    }

    if (type == IPC_CMD_PLAY_TEXT) {
        LOGD(TAG, "即将发送文本: [%s] (长度=%zu)", msg.text, strlen(msg.text));
    }

    int retry_count = 3;
    while (retry_count > 0) {
        size_t total_written = 0;
        const uint8_t *ptr = (const uint8_t *)&msg;
        int success = 1;

        while (total_written < sizeof(msg)) {
            ssize_t ret = write(tts_fd, ptr + total_written, sizeof(msg) - total_written);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                LOGE(TAG, "发送TTS命令失败: %s (errno=%d)", strerror(errno), errno);
                if (errno == EPIPE) {
                    LOGW(TAG, "管道破裂，尝试重新打开");
                    close(tts_fd);
                    tts_fd = -1;
                    open_pipes();
                }
                success = 0;
                break;
            }
            total_written += ret;
        }

        if (success && total_written == sizeof(msg)) {
            LOGD(TAG, "发送TTS命令成功: type=%d", type);
            return 0;
        }
        retry_count--;
        usleep(50000);
    }
    LOGE(TAG, "发送TTS命令失败，重试次数耗尽");
    return -1;
}

void check_asr_timeout(void) {
    if (current_state != STATE_ASR) {
        return;
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = (now.tv_sec - g_last_asr_update_time.tv_sec) * 1000.0;
    elapsed += (now.tv_nsec - g_last_asr_update_time.tv_nsec) / 1000000.0;

    if (elapsed > ASR_TIMEOUT_SECONDS * 1000) {
        LOGI(TAG, "ASR超时(%d秒)，返回关键词监听模式", ASR_TIMEOUT_SECONDS);
        
        if (strlen(g_last_asr_text) > 0) {
            LOGI(TAG, "最终识别结果: %s", g_last_asr_text);
            usleep(300000);
            
            char llm_response[MAX_TEXT_LEN] = {0};
            if (query_llm(g_last_asr_text, llm_response, sizeof(llm_response)) == 0) {
                LOGI(TAG, "LLM响应: %s", llm_response);
                if (strlen(llm_response) > 0) {
                    LOGI(TAG, "发送播放文本命令");
                    send_tts_command(IPC_CMD_PLAY_TEXT, llm_response, NULL);
                }
            }
        }

        memset(g_last_asr_text, 0, sizeof(g_last_asr_text));
        g_asr_result_updated = 0;
        current_state = STATE_KWS;
        LOGI(TAG, "=========关键词识别模式=========");
    }
}

int main(int argc, char const *argv[]) {
    LOGI(TAG, "=== ASR+KWS进程启动 ===");

    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        LOGE(TAG, "注册SIGINT信号处理失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (signal(SIGUSR1, offline_handler) == SIG_ERR) {
        LOGE(TAG, "注册SIGUSR1信号处理失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (signal(SIGUSR2, online_handler) == SIG_ERR) {
        LOGE(TAG, "注册SIGUSR2信号处理失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

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

    if (!running) return 0;

    if (init_sherpa_asr() != 0) {
        LOGE(TAG, "初始化语音识别模型失败!");
        running = 0;
    }
    LOGI(TAG, "语音识别模型加载完成");

    if (!running) goto CLEAR;

    alsa_buf = (int16_t *)malloc(PERIOD_SIZE * CHANNELS * sizeof(int16_t));
    if (alsa_buf == NULL) {
        LOGE(TAG, "分配ALSA录音缓冲区失败（内存不足）");
        running = 0;
    }

    if (!running) goto CLEAR;

    if (init_sherpa_kws() != 0) {
        LOGE(TAG, "初始化关键词识别模型失败!");
        running = 0;
    }
    LOGI(TAG, "关键词识别模型加载完成");

    open_pipes();
    LOGI(TAG, "=========关键词识别模式=========");

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

        if (current_state == STATE_ASR && g_current_online_mode == ONLINE_MODE_YES) {
            if (process_asr_result(model_audio, model_frames) == 0) {
                LOGI(TAG, "识别完成，结果: %s", g_last_asr_text);
                
                if (strlen(g_last_asr_text) > 0) {
                    LOGI(TAG, "发送停止播放命令");
                    send_tts_command(IPC_CMD_STOP_PLAYING, NULL, NULL);
                    usleep(300000);
                    
                    char llm_response[MAX_TEXT_LEN] = {0};
                    if (query_llm(g_last_asr_text, llm_response, sizeof(llm_response)) == 0) {
                        LOGI(TAG, "LLM响应: %s", llm_response);
                        if (strlen(llm_response) > 0) {
                            LOGI(TAG, "发送播放文本命令");
                            send_tts_command(IPC_CMD_PLAY_TEXT, llm_response, NULL);
                        }
                    }
                }

                memset(g_last_asr_text, 0, sizeof(g_last_asr_text));
                g_asr_result_updated = 0;
                current_state = STATE_KWS;
                LOGI(TAG, "=========关键词识别模式=========");
            } else {
                check_asr_timeout();
            }
        } else if (current_state == STATE_KWS) {
            char keyword[256] = {0};
            if (process_kws_result(model_audio, model_frames, keyword, sizeof(keyword))) {
                LOGI(TAG, "检测到关键词: %s", keyword);

                if (kws_fd != -1) {
                    char buf[260] = {0};
                    snprintf(buf, sizeof(buf), "%s\n", keyword);
                    write(kws_fd, buf, strlen(buf));
                }

                if (g_current_online_mode == ONLINE_MODE_YES) {
                    if (!strcmp(keyword, "小米小米") || !strcmp(keyword, "小刘同学") || !strcmp(keyword, "小爱同学")) {
                        LOGI(TAG, "发送停止播放命令");
                        send_tts_command(IPC_CMD_STOP_PLAYING, NULL, NULL);
                        usleep(300000);
                        LOGI(TAG, "发送唤醒响应命令");
                        send_tts_command(IPC_CMD_PLAY_WAKE_RESPONSE, NULL, NULL);
                        
                        clock_gettime(CLOCK_MONOTONIC, &g_last_asr_update_time);
                        memset(g_last_asr_text, 0, sizeof(g_last_asr_text));
                        g_asr_result_updated = 0;
                        
                        current_state = STATE_ASR;
                        LOGI(TAG, "=========语音识别模式=========");
                    }
                }
                snd_pcm_drop(g_pcm_handle);
                snd_pcm_prepare(g_pcm_handle);
            }
        }
    }

CLEAR:
    if (alsa_buf != NULL) {
        free(alsa_buf);
    }
    cleanup_sherpa_asr();
    cleanup_resampler();
    cleanup_alsa();
    cleanup_sherpa_kws();
    LOGI(TAG, "语音识别系统退出...");
    return 0;
}
