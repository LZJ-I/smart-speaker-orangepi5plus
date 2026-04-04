#define LOG_LEVEL 4
#include "debug_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

#include "select.h"
#include "link.h"
#include "shm.h"
#include "socket.h"
#include "device.h"
#include "player.h"
#define TAG "PLAYER-MAIN"

static void handle_exit_signal(int sig)
{
    (void)sig;
    g_player_shutdown_requested = 1;
}

static int ends_with(const char *text, const char *suffix)
{
    size_t text_len;
    size_t suffix_len;
    if (text == NULL || suffix == NULL) return 0;
    text_len = strlen(text);
    suffix_len = strlen(suffix);
    if (text_len < suffix_len) return 0;
    return strcmp(text + text_len - suffix_len, suffix) == 0;
}

static void run_init_script(void)
{
    char exe_path[PATH_MAX] = {0};
    char exe_dir[PATH_MAX] = {0};
    ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (n > 0) {
        char *slash;
        exe_path[n] = '\0';
        strncpy(exe_dir, exe_path, sizeof(exe_dir) - 1);
        slash = strrchr(exe_dir, '/');
        if (slash != NULL) {
            *slash = '\0';
            if (ends_with(exe_dir, "/player")) {
                char *parent = strrchr(exe_dir, '/');
                if (parent != NULL) {
                    *parent = '\0';
                    if (chdir(exe_dir) == 0 && access("./player/init.sh", X_OK) == 0) {
                        system("./player/init.sh");
                        return;
                    }
                }
            } else if (ends_with(exe_dir, "/build/bin")) {
                char *parent = strrchr(exe_dir, '/');
                if (parent != NULL) {
                    *parent = '\0';
                    parent = strrchr(exe_dir, '/');
                    if (parent != NULL) {
                        *parent = '\0';
                        if (chdir(exe_dir) == 0 && access("./player/init.sh", X_OK) == 0) {
                            system("./player/init.sh");
                            return;
                        }
                    }
                }
            } else if (access("./init.sh", X_OK) == 0) {
                system("./init.sh");
                return;
            }
        }
    }
    if (access("./player/init.sh", X_OK) == 0) {
        system("./player/init.sh");
        return;
    }
    if (access("./init.sh", X_OK) == 0) {
        system("./init.sh");
    }
}

static void install_no_restart_handler(int signum, void (*handler)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(signum, &sa, NULL);
}

int main(int argc, char const *argv[])
{
    (void)argc;
    (void)argv;
    run_init_script();
    if (access("./fifo/asr_fifo", F_OK) != 0 && access("../fifo/asr_fifo", F_OK) == 0) {
        chdir("..");
    } else if (access("./fifo/asr_fifo", F_OK) != 0 && access("../../fifo/asr_fifo", F_OK) == 0) {
        chdir("../..");
    }

    /* SA_RESTART 默认会重启阻塞的 select，导致 EOF 后无法及时处理 g_playlist_eof_flag */
    install_no_restart_handler(SIGUSR1, player_handle_playlist_eof);
    install_no_restart_handler(SIGCHLD, player_handle_sigchld);
    signal(SIGINT, handle_exit_signal);
    signal(SIGTERM, handle_exit_signal);
    signal(SIGHUP, handle_exit_signal);
    // 注册信号处理函数， 用于处理定时器到期信号
	signal(SIGALRM, button_handler);

    if(select_init() != 0)
    {
        LOGE(TAG, "select初始化失败");
        return -1;
    }
    LOGI(TAG, "select初始化成功！");

    // 打开并监听asr模型的管道文件
    if(init_asr_fifo() != 0)
    {
        LOGE(TAG, "打开asr语音识别管道失败");
        return -1;
    }
    LOGI(TAG, "打开asr语音识别管道成功！");

    if (init_kws_fifo() == 0)
    {
        LOGI(TAG, "打开kws关键词识别管道成功！");
    }
    else
    {
        LOGW(TAG, "打开kws关键词识别管道失败，继续启动");
    }

    // 打开tts模型的管道文件
    if(init_tts_fifo() != 0)
    {
        LOGE(TAG, "打开tts语音合成管道失败");
        return -1;
    }
    LOGI(TAG, "打开tts语音合成管道成功！");

    if(init_player_ctrl_fifo() != 0)
    {
        LOGE(TAG, "打开player控制管道失败");
        return -1;
    }
    LOGI(TAG, "打开player控制管道成功！");


    if(link_init() != 0)
    {
        LOGE(TAG, "链表初始化失败");
        return -1;
    }
    LOGI(TAG, "链表初始化成功！");

    if(shm_sem_init() != 0)
    {
        LOGE(TAG, "信号量初始化失败");
        return -1;
    }
    LOGI(TAG, "信号量初始化成功！");

    if(shm_init() != 0)
    {
        LOGE(TAG, "共享内存初始化失败");
        return -1;
    }
    LOGI(TAG, "共享内存初始化成功！");

    player_set_mode(ORDER_PLAY);

    player_apply_env_mode();
    if (player_env_forces_offline()) {
        if (player_offline_init_storage_and_library(0) != 0) {
            LOGE(TAG, "离线模式初始化失败");
            return -1;
        }
    }

    device_set_volume(DEFAULT_VOLUME);

    if (player_env_forces_offline()) {
        LOGI(TAG, "离线模式（环境变量），跳过 TCP 长连");
    } else if (socket_init() != 0) {
        LOGW(TAG, "TCP 长连失败，进入离线模式（挂载 SD 并载入本地曲库）");
        if (player_offline_init_storage_and_library(0) != 0) {
            LOGW(TAG, "离线模式初始化失败，本地曲库可能不可用");
        }
    } else {
        LOGI(TAG, "TCP 长连成功，已注册 select 与定时上报");
    }

    // // 初始化按键
    // if(key_init() != 0)
    // {
    //     LOGE(TAG, "按键初始化失败");
    // }
    // LOGI(TAG, "按键初始化成功！");
   
    select_run();   // 启动事件监听
    player_stop_play();
    player_cmd_fifo_close();
    shm_detach();

    return 0;
}
