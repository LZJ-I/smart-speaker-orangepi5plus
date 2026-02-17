#define LOG_LEVEL 4
#include "../../debug_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "sherpa_qwen.h"

#define TAG "QWEN-TEST"
#define MAX_RESPONSE_LEN 4096

int running = 1;

void sigint_handler(int sig) {
    LOGI(TAG, "收到退出信号");
    running = 0;
}

int main(int argc, char const *argv[]) {
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        LOGE(TAG, "注册信号处理失败");
        return -1;
    }

    LOGI(TAG, "=== Qwen 独立测试启动 ===");

    if (argc != 2) {
        LOGE(TAG, "用法: %s <你的问题>", argv[0]);
        return -1;
    }

    if (init_sherpa_qwen() != 0) {
        LOGE(TAG, "Qwen 初始化失败");
        return -1;
    }
    LOGI(TAG, "Qwen 初始化完成");

    char response[MAX_RESPONSE_LEN] = {0};
    if (generate_qwen_response(argv[1], response, sizeof(response)) != 0) {
        LOGE(TAG, "生成响应失败");
        cleanup_sherpa_qwen();
        return -1;
    }

    LOGI(TAG, "Qwen 回复: %s", response);

    cleanup_sherpa_qwen();

    LOGI(TAG, "Qwen 测试程序退出");
    return 0;
}
