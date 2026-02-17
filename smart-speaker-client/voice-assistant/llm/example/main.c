#define LOG_LEVEL 4
#include "../../debug_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "../llm.h"

#define TAG "LLM-TEST"
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

    LOGI(TAG, "=== LLM 独立测试启动 ===");

    if (argc != 2) {
        LOGE(TAG, "用法: %s <你的问题>", argv[0]);
        return -1;
    }

    if (init_llm() != 0) {
        LOGE(TAG, "LLM 初始化失败");
        return -1;
    }
    LOGI(TAG, "LLM 初始化完成");

    char response[MAX_RESPONSE_LEN] = {0};
    if (generate_llm_response(argv[1], response, sizeof(response)) != 0) {
        LOGE(TAG, "生成响应失败");
        cleanup_llm();
        return -1;
    }

    LOGI(TAG, "LLM 回复: %s", response);

    cleanup_llm();

    LOGI(TAG, "LLM 测试程序退出");
    return 0;
}
