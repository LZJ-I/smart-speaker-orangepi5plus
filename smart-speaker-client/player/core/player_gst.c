#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "player.h"
#include "debug_log.h"

#define TAG "GST"
#define FIFO_LINE_MAX 4096

#if defined(__has_include)
#if __has_include(<glib.h>) && __has_include(<gst/gst.h>)
#define HAVE_GSTREAMER_HEADERS 1
#endif
#endif

#ifdef HAVE_GSTREAMER_HEADERS

#include <glib.h>
#include <gst/gst.h>

typedef struct {
    GMainLoop *loop;
    GstElement *playbin;
    int fifo_fd;
    gboolean paused;
    char line_buf[FIFO_LINE_MAX];
    size_t line_len;
} GstPlayerData;

static void setup_local_gst_plugin_path(void)
{
    static const char *rel = "/3rdparty/gstreamer-alsa/usr/lib/aarch64-linux-gnu/gstreamer-1.0";
    char exe_path[PATH_MAX];
    char base_dir[PATH_MAX];
    char plugin_dir[PATH_MAX];
    char merged[PATH_MAX * 2];
    const char *old_path;
    ssize_t n;

    n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (n <= 0) return;
    exe_path[n] = '\0';
    strncpy(base_dir, exe_path, sizeof(base_dir) - 1);
    base_dir[sizeof(base_dir) - 1] = '\0';

    {
        char *slash = strrchr(base_dir, '/');
        if (slash == NULL) return;
        *slash = '\0';
    }
    if (strlen(base_dir) >= 7 && strcmp(base_dir + strlen(base_dir) - 7, "/player") == 0) {
        char *slash = strrchr(base_dir, '/');
        if (slash == NULL) return;
        *slash = '\0';
    } else if (strlen(base_dir) >= 10 && strcmp(base_dir + strlen(base_dir) - 10, "/build/bin") == 0) {
        char *slash = strrchr(base_dir, '/');
        if (slash == NULL) return;
        *slash = '\0';
        slash = strrchr(base_dir, '/');
        if (slash == NULL) return;
        *slash = '\0';
    }

    if (snprintf(plugin_dir, sizeof(plugin_dir), "%s%s", base_dir, rel) >= (int)sizeof(plugin_dir)) {
        return;
    }
    if (access(plugin_dir, R_OK | X_OK) != 0) return;

    old_path = getenv("GST_PLUGIN_PATH");
    if (old_path != NULL && old_path[0] != '\0') {
        if (snprintf(merged, sizeof(merged), "%s:%s", plugin_dir, old_path) >= (int)sizeof(merged)) {
            return;
        }
        setenv("GST_PLUGIN_PATH", merged, 1);
    } else {
        setenv("GST_PLUGIN_PATH", plugin_dir, 1);
    }
}

static const char *gst_alsa_device_string(void)
{
    const char *e = getenv(GST_ALSA_DEVICE_ENV);
    if (e != NULL && e[0] != '\0') {
        return e;
    }
    return GST_ALSA_DEVICE;
}

static void disable_proxy_for_playback(void)
{
    unsetenv("http_proxy");
    unsetenv("HTTP_PROXY");
    unsetenv("https_proxy");
    unsetenv("HTTPS_PROXY");
    unsetenv("all_proxy");
    unsetenv("ALL_PROXY");
    setenv("no_proxy", "127.0.0.1,localhost", 1);
    setenv("NO_PROXY", "127.0.0.1,localhost", 1);
}

#define GST_HTTP_USER_AGENT \
    "Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36"

static void try_apply_http_source_headers(GstElement *element, const char *uri)
{
    GObjectClass *klass;

    if (element == NULL) {
        return;
    }
    klass = G_OBJECT_GET_CLASS(element);
    if (g_object_class_find_property(klass, "user-agent")) {
        g_object_set(element, "user-agent", GST_HTTP_USER_AGENT, NULL);
        LOGI(TAG, "HTTP 源已设 User-Agent (%s)", GST_OBJECT_NAME(element));
    }
    if (uri != NULL && uri[0] != '\0' &&
        (strstr(uri, "126.net") != NULL || strstr(uri, "music.163.com") != NULL || strstr(uri, "netease") != NULL)) {
        if (g_object_class_find_property(klass, "referer")) {
            g_object_set(element, "referer", "https://music.163.com/", NULL);
            LOGI(TAG, "HTTP 源已设 Referer (%s)", GST_OBJECT_NAME(element));
        }
    }
}

static void apply_http_headers_recursive(GstElement *element, const char *uri)
{
    GValue item = G_VALUE_INIT;
    GstIterator *it;

    if (element == NULL) {
        return;
    }
    try_apply_http_source_headers(element, uri);
    if (!GST_IS_BIN(element)) {
        return;
    }
    it = gst_bin_iterate_recurse(GST_BIN(element));
    if (it == NULL) {
        return;
    }
    while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
        GstElement *child = GST_ELEMENT(g_value_get_object(&item));
        try_apply_http_source_headers(child, uri);
        g_value_unset(&item);
    }
    gst_iterator_free(it);
}

static void on_playbin_source_setup(GstElement *playbin, GstElement *source, gpointer user_data)
{
    gchar *uri = NULL;

    (void)user_data;
    if (playbin == NULL || source == NULL) {
        return;
    }
    g_object_get(playbin, "uri", &uri, NULL);
    apply_http_headers_recursive(source, uri != NULL ? uri : "");
    g_free(uri);
}

static GstBusSyncReply bus_sync(GstBus *bus, GstMessage *msg, gpointer data)
{
    GstPlayerData *d = (GstPlayerData *)data;
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_main_loop_quit(d->loop);
            break;
        case GST_MESSAGE_ERROR:
        {
            GError *err = NULL;
            gchar *dbg = NULL;
            gst_message_parse_error(msg, &err, &dbg);
            if (err != NULL) {
                LOGE(TAG, "GStreamer error: %s", err->message);
                g_error_free(err);
            } else {
                LOGE(TAG, "GStreamer error (no message)");
            }
            if (dbg != NULL) {
                if (dbg[0] != '\0') {
                    LOGE(TAG, "GStreamer debug: %s", dbg);
                }
                g_free(dbg);
            }
            g_main_loop_quit(d->loop);
            break;
        }
        default:
            break;
    }
    return GST_BUS_PASS;
}

static gboolean process_line(GstPlayerData *d, char *line)
{
    while (*line == ' ' || *line == '\n') line++;
    if (strncmp(line, "quit", 4) == 0) {
        g_main_loop_quit(d->loop);
        return FALSE;
    }
    if (strncmp(line, "cycle pause", 11) == 0) {
        d->paused = !d->paused;
        gst_element_set_state(d->playbin, d->paused ? GST_STATE_PAUSED : GST_STATE_PLAYING);
        return TRUE;
    }
    if (strncmp(line, "loadfile ", 9) == 0) {
        char *path = line + 9;
        while (*path == ' ') path++;
        if (*path == '\'') { path++; char *end = strrchr(path, '\''); if (end) *end = '\0'; }
        g_object_set(d->playbin, "uri", path, NULL);
        gst_element_set_state(d->playbin, GST_STATE_PLAYING);
        d->paused = FALSE;
    }
    return TRUE;
}

static gboolean on_fifo(GIOChannel *ch, GIOCondition cond, gpointer data)
{
    GstPlayerData *d = (GstPlayerData *)data;
    char buf[512];
    ssize_t n;
    (void)ch;
    (void)cond;
    n = read(d->fifo_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return TRUE;
    for (ssize_t i = 0; i < n && d->line_len < FIFO_LINE_MAX - 1; i++) {
        d->line_buf[d->line_len++] = buf[i];
        if (buf[i] == '\n') {
            d->line_buf[d->line_len] = '\0';
            if (!process_line(d, d->line_buf)) return FALSE;
            d->line_len = 0;
        }
    }
    if (d->line_len >= FIFO_LINE_MAX - 1) {
        d->line_len = 0;
    }
    return TRUE;
}

int run_gst_player(const char *initial_uri)
{
    GstPlayerData data = {0};
    data.paused = FALSE;
    setenv("GST_GL", "disable", 1);
    setenv("DISPLAY", "", 1);
    disable_proxy_for_playback();
    setup_local_gst_plugin_path();
    gst_init(NULL, NULL);
    GstElement *asink = gst_element_factory_make("alsasink", "asink");
    if (asink) {
        g_object_set(asink, "device", gst_alsa_device_string(), NULL);
        LOGI(TAG, "使用 alsasink device=%s", gst_alsa_device_string());
    } else {
        LOGW(TAG, "alsasink 不可用，回退到 autoaudiosink");
        asink = gst_element_factory_make("autoaudiosink", "asink");
    }
    GstElement *vsink = gst_element_factory_make("fakesink", "vsink");
    data.playbin = gst_element_factory_make("playbin", "player");
    if (!data.playbin) {
        LOGE(TAG, "playbin create fail");
        return -1;
    }
    if (asink)
        g_object_set(data.playbin, "audio-sink", asink, NULL);
    if (vsink)
        g_object_set(data.playbin, "video-sink", vsink, NULL);
    g_signal_connect(data.playbin, "source-setup", G_CALLBACK(on_playbin_source_setup), NULL);
    g_object_set(data.playbin, "uri", initial_uri, NULL);
    GstBus *bus = gst_element_get_bus(data.playbin);
    gst_bus_set_sync_handler(bus, bus_sync, &data, NULL);
    gst_object_unref(bus);
    data.fifo_fd = open(GST_CMD_FIFO, O_RDONLY | O_NONBLOCK);
    if (data.fifo_fd >= 0) {
        GIOChannel *io = g_io_channel_unix_new(data.fifo_fd);
        g_io_channel_set_encoding(io, NULL, NULL);
        g_io_add_watch(io, G_IO_IN, (GIOFunc)on_fifo, &data);
        g_io_channel_unref(io);
    }
    data.loop = g_main_loop_new(NULL, FALSE);
    gst_element_set_state(data.playbin, GST_STATE_PLAYING);
    g_main_loop_run(data.loop);
    gst_element_set_state(data.playbin, GST_STATE_NULL);
    gst_object_unref(data.playbin);
    g_main_loop_unref(data.loop);
    if (data.fifo_fd >= 0) close(data.fifo_fd);
    return 0;
}

#else

typedef struct {
    int fifo_fd;
    int paused;
    char line_buf[FIFO_LINE_MAX];
    size_t line_len;
    char current_uri[2048];
} FallbackPlayerData;

static int fallback_process_line(FallbackPlayerData *d, char *line)
{
    while (*line == ' ' || *line == '\n') line++;
    if (strncmp(line, "quit", 4) == 0) {
        return 0;
    }
    if (strncmp(line, "cycle pause", 11) == 0) {
        d->paused = !d->paused;
        return 1;
    }
    if (strncmp(line, "loadfile ", 9) == 0) {
        char *path = line + 9;
        while (*path == ' ') path++;
        if (*path == '\'') {
            char *end;
            path++;
            end = strrchr(path, '\'');
            if (end) *end = '\0';
        }
        snprintf(d->current_uri, sizeof(d->current_uri), "%s", path);
        d->paused = 0;
        return 1;
    }
    return 1;
}

int run_gst_player(const char *initial_uri)
{
    FallbackPlayerData data;
    fd_set readfds;
    memset(&data, 0, sizeof(data));
    if (initial_uri != NULL) {
        snprintf(data.current_uri, sizeof(data.current_uri), "%s", initial_uri);
    }
    data.fifo_fd = open(GST_CMD_FIFO, O_RDONLY | O_NONBLOCK);
    if (data.fifo_fd < 0) {
        LOGE(TAG, "打开播放控制管道失败: %s", strerror(errno));
        return -1;
    }
    LOGW(TAG, "GStreamer 头文件缺失，当前使用静默降级播放后端");
    while (1) {
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(data.fifo_fd, &readfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        if (select(data.fifo_fd + 1, &readfds, NULL, NULL, &tv) <= 0) {
            continue;
        }
        if (FD_ISSET(data.fifo_fd, &readfds)) {
            char buf[512];
            ssize_t n = read(data.fifo_fd, buf, sizeof(buf) - 1);
            if (n <= 0) {
                continue;
            }
            for (ssize_t i = 0; i < n && data.line_len < FIFO_LINE_MAX - 1; ++i) {
                data.line_buf[data.line_len++] = buf[i];
                if (buf[i] == '\n') {
                    data.line_buf[data.line_len] = '\0';
                    if (!fallback_process_line(&data, data.line_buf)) {
                        close(data.fifo_fd);
                        return 0;
                    }
                    data.line_len = 0;
                }
            }
            if (data.line_len >= FIFO_LINE_MAX - 1) {
                data.line_len = 0;
            }
        }
    }
}

#endif
