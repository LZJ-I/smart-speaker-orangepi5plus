#define LOG_LEVEL 4
#include "../../debug_log.h"
#include "llm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <json-c/json.h>

#define TAG "LLM"

#ifdef KWS_TEST_MODE
#define MODEL_PREFIX "../../3rdparty"
#define LLM_SCRIPT_PATH "../../llm.sh"
#elif defined(PROCESS_MODE)
#define MODEL_PREFIX "./3rdparty"
#define LLM_SCRIPT_PATH "./voice-assistant/llm.sh"
#else
#define MODEL_PREFIX "../3rdparty"
#define LLM_SCRIPT_PATH "./llm.sh"
#endif
#define MAX_COMMAND_LEN 1024
#define MAX_RESPONSE_LEN 4096

typedef enum {
    ERR_OK = 0,
    ERR_INVALID_ARGS,
    ERR_POPEN_FAILED,
    ERR_JSON_PARSE_FAILED,
    ERR_NO_CHOICES,
    ERR_NO_MESSAGE,
    ERR_NO_CONTENT
} ErrorCode;

static const char* get_error_message(ErrorCode code) {
    switch (code) {
        case ERR_OK: return "成功";
        case ERR_INVALID_ARGS: return "参数无效";
        case ERR_POPEN_FAILED: return "执行命令失败";
        case ERR_JSON_PARSE_FAILED: return "JSON解析失败";
        case ERR_NO_CHOICES: return "未找到choices字段";
        case ERR_NO_MESSAGE: return "未找到message字段";
        case ERR_NO_CONTENT: return "未找到content字段";
        default: return "未知错误";
    }
}

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

    for (size_t i = 0; i < strlen(out_content); i++) {
        if (out_content[i] == '\n' || out_content[i] == '\r' || out_content[i] == '\t') {
            out_content[i] = ' ';
        }
    }

    LOGI(TAG, "解析响应成功，内容: %s", out_content);

cleanup:
    json_object_put(json);
    return ret;
}

int init_llm(void) {
    LOGI(TAG, "LLM 初始化完成");
    return 0;
}

int generate_llm_response(const char *question, char *response, size_t response_len) {
    if (question == NULL || strlen(question) == 0) {
        LOGE(TAG, "%s", get_error_message(ERR_INVALID_ARGS));
        return -1;
    }

    LOGI(TAG, "用户问题: %s", question);

    char command[MAX_COMMAND_LEN] = {0};
    snprintf(command, sizeof(command), "cd ./voice-assistant/llm && ./llm.sh \"%s\" 2>>/dev/null", question);
    LOGD(TAG, "执行命令: %s", command);

    FILE* fp = popen(command, "r");
    if (fp == NULL) {
        LOGE(TAG, "%s", get_error_message(ERR_POPEN_FAILED));
        return -1;
    }

    char json_response[MAX_RESPONSE_LEN] = {0};
    size_t total_read = 0;
    size_t buf_remaining = sizeof(json_response) - 1;
    char* ptr = json_response;

    while (buf_remaining > 0 && !feof(fp)) {
        size_t read = fread(ptr, 1, buf_remaining, fp);
        if (read <= 0) break;
        total_read += read;
        ptr += read;
        buf_remaining -= read;
    }
    json_response[total_read] = '\0';
    pclose(fp);
    LOGD(TAG, "收到响应，长度: %zu", total_read);

    ErrorCode err = parse_json_response(json_response, response, response_len);

    if (err != ERR_OK) {
        LOGE(TAG, "%s", get_error_message(err));
        return -1;
    }

    return 0;
}

int query_llm(const char *question, char *response, size_t response_len) {
    return generate_llm_response(question, response, response_len);
}

void cleanup_llm(void) {
    LOGI(TAG, "LLM 资源清理完成");
}
