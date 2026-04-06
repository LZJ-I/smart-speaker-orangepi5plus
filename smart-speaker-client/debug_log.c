#include "debug_log.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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

    if (subdir == NULL || subdir[0] == '\0') {
        return;
    }
    if (snprintf(dir, sizeof(dir), "data/%s", subdir) >= (int)sizeof(dir)) {
        return;
    }
    {
        char dir_copy[384];
        strncpy(dir_copy, dir, sizeof(dir_copy) - 1);
        dir_copy[sizeof(dir_copy) - 1] = '\0';
        if (mkdir_p(dir_copy) != 0) {
            return;
        }
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

void debug_log_emit(int level, const char *tag, const char *fmt, ...)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    va_list ap;

    fprintf(stderr, "%s(%s) %s [%02d:%02d:%02d] ",
            get_log_color(level), get_log_prefix(level), tag,
            tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", LOG_COLOR_NONE);

    pthread_mutex_lock(&g_app_log_mutex);
    if (g_app_log_fp != NULL) {
        fprintf(g_app_log_fp, "(%s) %s [%02d:%02d:%02d] ",
                get_log_prefix(level), tag,
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
        va_start(ap, fmt);
        vfprintf(g_app_log_fp, fmt, ap);
        va_end(ap);
        fprintf(g_app_log_fp, "\n");
        fflush(g_app_log_fp);
    }
    pthread_mutex_unlock(&g_app_log_mutex);
}
