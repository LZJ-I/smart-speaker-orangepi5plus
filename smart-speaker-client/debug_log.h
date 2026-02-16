#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stdio.h>
#include <time.h>

#define LOG_COLOR_NONE   "\033[0m"
#define LOG_COLOR_RED    "\033[31m"
#define LOG_COLOR_YELLOW "\033[33m"
#define LOG_COLOR_GREEN  "\033[32m"
#define LOG_COLOR_BLUE   "\033[34m"

#ifndef LOG_LEVEL
#define LOG_LEVEL 4
#endif

#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4
#define LOG_LEVEL_VERBOSE 5

static inline const char* get_log_prefix(int level) {
    switch(level) {
        case LOG_LEVEL_ERROR:   return "E";
        case LOG_LEVEL_WARN:    return "W";
        case LOG_LEVEL_INFO:    return "I";
        case LOG_LEVEL_DEBUG:   return "D";
        case LOG_LEVEL_VERBOSE: return "V";
        default:                return "?";
    }
}

static inline const char* get_log_color(int level) {
    switch(level) {
        case LOG_LEVEL_ERROR:   return LOG_COLOR_RED;
        case LOG_LEVEL_WARN:    return LOG_COLOR_YELLOW;
        case LOG_LEVEL_INFO:    return LOG_COLOR_GREEN;
        case LOG_LEVEL_DEBUG:   return LOG_COLOR_BLUE;
        case LOG_LEVEL_VERBOSE: return LOG_COLOR_NONE;
        default:                return LOG_COLOR_NONE;
    }
}

#define LOG_IMPL(level, tag, fmt, ...) \
    do { \
        if (level <= LOG_LEVEL) { \
            time_t now = time(NULL); \
            struct tm* tm_info = localtime(&now); \
            fprintf(stderr, "%s(%s) %s [%02d:%02d:%02d] " fmt "%s\n", \
                    get_log_color(level), get_log_prefix(level), tag, \
                    tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, \
                    ##__VA_ARGS__, LOG_COLOR_NONE); \
        } \
    } while(0)

#define LOGE(tag, fmt, ...) LOG_IMPL(LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) LOG_IMPL(LOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) LOG_IMPL(LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#define LOGD(tag, fmt, ...) LOG_IMPL(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOGV(tag, fmt, ...) LOG_IMPL(LOG_LEVEL_VERBOSE, tag, fmt, ##__VA_ARGS__)

#endif
