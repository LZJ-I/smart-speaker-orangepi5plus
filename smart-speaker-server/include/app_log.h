#ifndef APP_LOG_H
#define APP_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

void app_log_init(const char *subdir);
void app_log_emit(const char *time_str, const char *msg);

#ifdef __cplusplus
}
#endif

#endif
