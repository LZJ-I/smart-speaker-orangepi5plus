#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <glib.h>
#include <gst/gst.h>
#include "player.h"
#include "../debug_log.h"

#define TAG "GST"

#define FIFO_LINE_MAX 4096

typedef struct {
    GMainLoop *loop;
    GstElement *playbin;
    int fifo_fd;
    gboolean paused;
    char line_buf[FIFO_LINE_MAX];
    size_t line_len;
} GstPlayerData;

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
            if (err) g_error_free(err);
            if (dbg) g_free(dbg);
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
    ssize_t n = read(d->fifo_fd, buf, sizeof(buf) - 1);
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
    gst_init(NULL, NULL);
    GstElement *asink = gst_element_factory_make("alsasink", "asink");
    if (asink)
        g_object_set(asink, "device", GST_ALSA_DEVICE, NULL);
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
