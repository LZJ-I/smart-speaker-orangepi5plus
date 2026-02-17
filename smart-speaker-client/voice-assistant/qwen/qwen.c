#define LOG_LEVEL 4
#include "../debug_log.h"
#include <stdio.h>
#include <stdint.h>
#include <json-c/json.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#define TAG "qwen"

// 配置项
#define QWEN_SCRIPT_PATH "../qwen/qwen.sh"
#define TTS_FIFO_PATH "../../fifo/tts_fifo"
#define MAX_COMMAND_LEN 1024
#define MAX_RESPONSE_LEN 4096

// 错误码枚举
typedef enum {
    ERR_OK = 0,
    ERR_INVALID_ARGS,
    ERR_POPEN_FAILED,
    ERR_JSON_PARSE_FAILED,
    ERR_NO_CHOICES,
    ERR_NO_MESSAGE,
    ERR_NO_CONTENT,
    ERR_OPEN_FIFO_FAILED,
    ERR_WRITE_FIFO_FAILED
} ErrorCode;

// 获取错误信息字符串
static const char* get_error_message(ErrorCode code) {
    switch (code) {
        case ERR_OK: return "成功";
        case ERR_INVALID_ARGS: return "请加上你的问题";
        case ERR_POPEN_FAILED: return "执行sh命令失败";
        case ERR_JSON_PARSE_FAILED: return "JSON解析失败";
        case ERR_NO_CHOICES: return "未找到choices字段";
        case ERR_NO_MESSAGE: return "未找到message字段";
        case ERR_NO_CONTENT: return "未找到content字段";
        case ERR_OPEN_FIFO_FAILED: return "打开tts_fifo失败";
        case ERR_WRITE_FIFO_FAILED: return "写入tts_fifo失败";
        default: return "未知错误";
    }
}

// 解析 Qwen API 的 JSON 响应，提取 content 内容
static ErrorCode parse_json_response(const char* json_str, char* out_content, size_t out_len) {
    LOGD(TAG, "开始解析 JSON 响应");
    json_object* json = json_tokener_parse(json_str);
    if (json == NULL) {
        LOGE(TAG, "JSON 解析失败");
        return ERR_JSON_PARSE_FAILED;
    }

    ErrorCode ret = ERR_OK;
    json_object* choices = NULL;
    json_object* choice_0 = NULL;
    json_object* message = NULL;
    json_object* content = NULL;

    if (!(choices = json_object_object_get(json, "choices"))) {
        LOGE(TAG, "未找到 choices 字段");
        ret = ERR_NO_CHOICES;
        goto cleanup;
    }

    if ((choice_0 = json_object_array_get_idx(choices, 0)) == NULL) {
        LOGE(TAG, "choices 数组为空");
        ret = ERR_NO_CHOICES;
        goto cleanup;
    }

    if (!(message = json_object_object_get(choice_0, "message"))) {
        LOGE(TAG, "未找到 message 字段");
        ret = ERR_NO_MESSAGE;
        goto cleanup;
    }

    if (!(content = json_object_object_get(message, "content"))) {
        LOGE(TAG, "未找到 content 字段");
        ret = ERR_NO_CONTENT;
        goto cleanup;
    }

    const char* content_str = json_object_get_string(content);
    if (content_str == NULL || strlen(content_str) == 0) {
        LOGE(TAG, "content 字段为空");
        ret = ERR_NO_CONTENT;
        goto cleanup;
    }

    strncpy(out_content, content_str, out_len - 1);
    out_content[out_len - 1] = '\0';
    LOGI(TAG, "解析 Json 响应成功，内容: %s", content_str);

cleanup:
    json_object_put(json);
    return ret;
}

// 将内容发送到 TTS FIFO 管道
static ErrorCode send_to_tts(const char* content) {
    LOGD(TAG, "打开 FIFO: %s", TTS_FIFO_PATH);
    int tts_fd = open(TTS_FIFO_PATH, O_WRONLY | O_NONBLOCK);
    if (tts_fd < 0) {
        LOGE(TAG, "打开 FIFO 失败: %s", strerror(errno));
        return ERR_OPEN_FIFO_FAILED;
    }

    LOGD(TAG, "写入 FIFO，长度: %zu", strlen(content));
    ssize_t written = write(tts_fd, content, strlen(content));
    close(tts_fd);

    if (written == -1) {
        LOGE(TAG, "写入 FIFO 失败: %s", strerror(errno));
        return ERR_WRITE_FIFO_FAILED;
    }

    LOGI(TAG, "写入 FIFO 成功: %s", content);
    return ERR_OK;
}

// 主函数：调用 Qwen API 并将结果发送到 TTS
int main(int argc, char const *argv[]) {
    LOGD(TAG, "程序启动");
    
    if (argc != 2) {
        LOGE(TAG, "%s", get_error_message(ERR_INVALID_ARGS));
        return 1;
    }

    LOGI(TAG, "用户问题: %s", argv[1]);

    char command[MAX_COMMAND_LEN] = {0};
    snprintf(command, sizeof(command), "cd ../qwen && ./qwen.sh \"%s\" 2>>/dev/null", argv[1]);
    LOGD(TAG, "执行命令: %s", command);

    FILE* fp = popen(command, "r");
    if (fp == NULL) {
        LOGE(TAG, "%s", get_error_message(ERR_POPEN_FAILED));
        return 1;
    }

    char response[MAX_RESPONSE_LEN] = {0};
    size_t total_read = 0;
    size_t buf_remaining = sizeof(response) - 1;
    char* ptr = response;

    while (buf_remaining > 0 && !feof(fp)) {
        size_t read = fread(ptr, 1, buf_remaining, fp);
        if (read <= 0) break;
        total_read += read;
        ptr += read;
        buf_remaining -= read;
    }
    response[total_read] = '\0';
    pclose(fp);
    LOGD(TAG, "收到响应，长度: %zu", total_read);

    char content[MAX_RESPONSE_LEN] = {0};
    ErrorCode err = parse_json_response(response, content, sizeof(content));

    if (err != ERR_OK) {
        LOGE(TAG, "%s", get_error_message(err));
        return 1;
    }

    err = send_to_tts(content);
    if (err != ERR_OK) {
        LOGE(TAG, "%s", get_error_message(err));
        return 1;
    }

    LOGI(TAG, "处理完成");
    return 0;
}
