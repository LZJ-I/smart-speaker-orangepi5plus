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
#include <poll.h>

#define LOG_LEVEL 4
#include "../../debug_log.h"
#include "../common/ipc_protocol.h"
#include "../../ipc/ipc_message.h"
#include "../common/alsa.h"
#include "../common/mysamplerate.h"
#include "../asr/sherpa_asr.h"
#include "../kws/sherpa_kws.h"
#include "asr_kws_constants.h"
#include "asr_kws_types.h"
#include "asr_kws_pipe.h"

#define TAG "ASR_KWS_MAIN"

char g_last_asr_text[1024] = {0};
struct timespec g_last_asr_update_time = {0, 0};
int g_asr_result_updated = 0;

int running = 1;
int asr_fd = -1;
int kws_fd = -1;
int tts_fd = -1;
int asr_ctrl_fd = -1;
int16_t *alsa_buf = NULL;
uint32_t g_tts_seq = 0;

int current_state = STATE_KWS;
enum OnlineMode g_current_online_mode = ONLINE_MODE_YES;

static void wait_for_tts_wake_done(void)
{
    int rfd = open(TTS_WAKE_DONE_FIFO_PATH, O_RDONLY | O_NONBLOCK);
    struct pollfd pfd;
    char b = 0;
    int pr;

    if (rfd < 0) {
        LOGW(TAG, "打开tts唤醒完成管道失败: %s", strerror(errno));
        return;
    }

    pfd.fd = rfd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    pr = poll(&pfd, 1, 5000);
    if (pr > 0 && (pfd.revents & POLLIN)) {
        if (read(rfd, &b, 1) != 1) {
            LOGW(TAG, "读取tts唤醒完成信号失败: %s", strerror(errno));
        }
    } else if (pr == 0) {
        LOGW(TAG, "等待tts唤醒完成超时，继续进入ASR模式");
    } else if (pr < 0) {
        LOGW(TAG, "等待tts唤醒完成失败: %s", strerror(errno));
    } else {
        LOGW(TAG, "tts唤醒完成管道异常关闭，继续进入ASR模式");
    }

    close(rfd);
}

int send_tts_command(IPCCommandType type, const char *text, const char *filename) {
    if (tts_fd == -1) {
        asr_kws_pipe_open(&asr_fd, &kws_fd, &tts_fd, &asr_ctrl_fd);
        if (tts_fd == -1) {
            LOGW(TAG, "TTS管道未打开");
            return -1;
        }
    }

    const char *payload = NULL;
    if (type == IPC_CMD_PLAY_TEXT) {
        payload = text;
    } else if (type == IPC_CMD_PLAY_AUDIO_FILE) {
        payload = filename;
    }

    int retry_count = 3;
    while (retry_count > 0) {
        uint32_t body_len = payload ? (uint32_t)(strlen(payload) + 1) : 0;
        if (ipc_send_message(tts_fd, (uint16_t)type, payload, body_len, &g_tts_seq) == 0) {
            LOGD(TAG, "发送TTS命令成功: type=%d", type);
            return 0;
        }
        LOGE(TAG, "发送TTS命令失败: %s (errno=%d)", strerror(errno), errno);
        if (errno == EPIPE || errno == ENXIO) {
            LOGW(TAG, "管道破裂，尝试重新打开");
            close(tts_fd);
            tts_fd = -1;
            asr_kws_pipe_open(&asr_fd, &kws_fd, &tts_fd, &asr_ctrl_fd);
        }
        retry_count--;
        struct pollfd pfd = { .fd = 0, .events = 0 };
        poll(&pfd, 1, 50);
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
            send_tts_command(IPC_CMD_STOP_PLAYING, NULL, NULL);
            asr_kws_pipe_write_text(&asr_fd, ASR_FIFO_PATH, g_last_asr_text);
        } else {
            char line[64];
            snprintf(line, sizeof(line), "%s\n", ASR_TIMEOUT_SENTINEL);
            if (asr_fd >= 0) {
                write(asr_fd, line, strlen(line));
            }
        }

        memset(g_last_asr_text, 0, sizeof(g_last_asr_text));
        g_asr_result_updated = 0;
        current_state = STATE_KWS;
        LOGI(TAG, "=========关键词识别模式=========");
    }
}

void sigint_handler(int sig) {
    LOGI(TAG, "收到退出信号，正在清理资源...");
    if (alsa_buf != NULL) {
        free(alsa_buf);
    }
    if (asr_fd != -1) close(asr_fd);
    if (kws_fd != -1) close(kws_fd);
    if (tts_fd != -1) close(tts_fd);
    if (asr_ctrl_fd != -1) close(asr_ctrl_fd);
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

int main(int argc, char const *argv[]) {
    app_log_init("kws-asr");
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

    asr_kws_pipe_open(&asr_fd, &kws_fd, &tts_fd, &asr_ctrl_fd);
    LOGI(TAG, "=========关键词识别模式=========");

    while (running) {
        asr_kws_pipe_process_ctrl(&asr_ctrl_fd, (int *)&g_current_online_mode);
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
                    send_tts_command(IPC_CMD_STOP_PLAYING, NULL, NULL);
                    asr_kws_pipe_write_text(&asr_fd, ASR_FIFO_PATH, g_last_asr_text);
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
                int is_wake = (!strcmp(keyword, "小米小米") || !strcmp(keyword, "小刘同学"));
                if (!is_wake)
                    continue;
                LOGI(TAG, "检测到唤醒词: %s", keyword);
                asr_kws_pipe_write_text(&kws_fd, KWS_FIFO_PATH, keyword);
                send_tts_command(IPC_CMD_STOP_PLAYING, NULL, NULL);
                send_tts_command(IPC_CMD_PLAY_WAKE_RESPONSE, NULL, NULL);
                wait_for_tts_wake_done();
                clock_gettime(CLOCK_MONOTONIC, &g_last_asr_update_time);
                memset(g_last_asr_text, 0, sizeof(g_last_asr_text));
                g_asr_result_updated = 0;
                current_state = STATE_ASR;
                LOGI(TAG, "=========语音识别模式=========");
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
