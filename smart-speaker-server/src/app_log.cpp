#include "app_log.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <sys/stat.h>

static FILE *g_app_log_fp;
static pthread_mutex_t g_app_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static int mkdir_p(char *path)
{
    char *p;
    for (p = path + 1; *p; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
        *p = '/';
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

void app_log_init(const char *subdir)
{
    char dir[384];
    char path[448];
    char dir_copy[384];

    if (subdir == NULL || subdir[0] == '\0') {
        return;
    }
    if (snprintf(dir, sizeof(dir), "data/%s", subdir) >= (int)sizeof(dir)) {
        return;
    }
    strncpy(dir_copy, dir, sizeof(dir_copy) - 1);
    dir_copy[sizeof(dir_copy) - 1] = '\0';
    if (mkdir_p(dir_copy) != 0) {
        return;
    }
    if (snprintf(path, sizeof(path), "data/%s/app.log", subdir) >= (int)sizeof(path)) {
        return;
    }

    pthread_mutex_lock(&g_app_log_mutex);
    if (g_app_log_fp != NULL) {
        fclose(g_app_log_fp);
        g_app_log_fp = NULL;
    }
    g_app_log_fp = fopen(path, "a");
    pthread_mutex_unlock(&g_app_log_mutex);
}

void app_log_emit(const char *time_str, const char *msg)
{
    if (time_str == NULL || msg == NULL) {
        return;
    }
    pthread_mutex_lock(&g_app_log_mutex);
    if (g_app_log_fp != NULL) {
        fprintf(g_app_log_fp, "[%s] %s\n", time_str, msg);
        fflush(g_app_log_fp);
    }
    pthread_mutex_unlock(&g_app_log_mutex);
}
