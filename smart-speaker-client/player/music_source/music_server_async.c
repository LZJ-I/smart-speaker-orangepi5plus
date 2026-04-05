#include "music_server_async.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "debug_log.h"
#include "music_source_manager.h"
#include "player.h"
#include "select.h"

#define TAG "MUSIC-ASYNC"

static int g_mas_pipe[2] = {-1, -1};
static pthread_mutex_t g_mas_mu = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t g_mas_busy;

static char g_query[256];
static music_async_out_t g_pending_out;
static MusicSourceItem g_pending_item;
static MusicSourceResult g_pending_search;

static void mas_notify_write(void)
{
    char b = 1;
    ssize_t w;
    do {
        w = write(g_mas_pipe[1], &b, 1);
    } while (w < 0 && errno == EINTR);
}

static void *mas_play_query_thread(void *arg)
{
    char q[256];
    MusicSourceItem it;
    MusicSourceResult sr;
    player_playlist_ctx_t pl;
    (void)arg;

    memset(&it, 0, sizeof(it));
    memset(&sr, 0, sizeof(sr));

    pthread_mutex_lock(&g_mas_mu);
    snprintf(q, sizeof(q), "%s", g_query);
    pthread_mutex_unlock(&g_mas_mu);

    if (g_current_online_mode == ONLINE_MODE_YES && music_source_server_resolve_keyword(q, &it) == 0) {
        pthread_mutex_lock(&g_mas_mu);
        g_pending_out = MUSIC_ASYNC_OK_RESOLVE;
        g_pending_item = it;
        memset(&g_pending_search, 0, sizeof(g_pending_search));
        pthread_mutex_unlock(&g_mas_mu);
        mas_notify_write();
        return NULL;
    }

    player_get_playlist_ctx(&pl);
    if (music_source_search(q, 1, pl.page_size > 0 ? pl.page_size : 10, &sr) != 0 || sr.count <= 0) {
        if (sr.count <= 0 && sr.online_search_disabled) {
            music_source_set_online_search_blocked(1);
        }
        music_source_free_result(&sr);
        pthread_mutex_lock(&g_mas_mu);
        g_pending_out = MUSIC_ASYNC_FAIL;
        memset(&g_pending_item, 0, sizeof(g_pending_item));
        memset(&g_pending_search, 0, sizeof(g_pending_search));
        pthread_mutex_unlock(&g_mas_mu);
        mas_notify_write();
        return NULL;
    }

    pthread_mutex_lock(&g_mas_mu);
    g_pending_out = MUSIC_ASYNC_OK_SEARCH;
    memset(&g_pending_item, 0, sizeof(g_pending_item));
    g_pending_search = sr;
    memset(&sr, 0, sizeof(sr));
    pthread_mutex_unlock(&g_mas_mu);
    mas_notify_write();
    return NULL;
}

int music_server_async_init(void)
{
    int fl;
    if (g_mas_pipe[0] >= 0) {
        return 0;
    }
    if (pipe(g_mas_pipe) != 0) {
        LOGE(TAG, "pipe: %s", strerror(errno));
        return -1;
    }
    fl = fcntl(g_mas_pipe[0], F_GETFL, 0);
    if (fl < 0 || fcntl(g_mas_pipe[0], F_SETFL, fl | O_NONBLOCK) != 0) {
        close(g_mas_pipe[0]);
        close(g_mas_pipe[1]);
        g_mas_pipe[0] = g_mas_pipe[1] = -1;
        return -1;
    }
    FD_SET(g_mas_pipe[0], &READSET);
    update_max_fd();
    return 0;
}

int music_server_async_fd(void)
{
    return g_mas_pipe[0];
}

void music_server_async_on_readable(void)
{
    char drain[64];
    ssize_t n;
    music_async_out_t out;
    MusicSourceItem it;
    MusicSourceResult sr;
    char q[256];

    while ((n = read(g_mas_pipe[0], drain, sizeof(drain))) > 0) {
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        return;
    }

    pthread_mutex_lock(&g_mas_mu);
    out = g_pending_out;
    it = g_pending_item;
    sr = g_pending_search;
    snprintf(q, sizeof(q), "%s", g_query);
    g_pending_out = MUSIC_ASYNC_FAIL;
    memset(&g_pending_item, 0, sizeof(g_pending_item));
    memset(&g_pending_search, 0, sizeof(g_pending_search));
    pthread_mutex_unlock(&g_mas_mu);

    g_mas_busy = 0;

    if (out == MUSIC_ASYNC_OK_RESOLVE) {
        select_music_async_play_query_done(out, &it, NULL, q);
    } else if (out == MUSIC_ASYNC_OK_SEARCH) {
        select_music_async_play_query_done(out, NULL, &sr, q);
    } else {
        select_music_async_play_query_done(MUSIC_ASYNC_FAIL, NULL, NULL, q);
    }
}

int music_server_async_start_play_query(const char *query)
{
    pthread_t th;
    pthread_attr_t attr;
    int r;
    if (query == NULL || query[0] == '\0') {
        return -1;
    }
    if (g_mas_pipe[0] < 0) {
        return -1;
    }
    if (g_mas_busy) {
        LOGW(TAG, "上一笔搜歌请求未完成，忽略");
        return -1;
    }
    g_mas_busy = 1;
    pthread_mutex_lock(&g_mas_mu);
    snprintf(g_query, sizeof(g_query), "%s", query);
    pthread_mutex_unlock(&g_mas_mu);

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    r = pthread_create(&th, &attr, mas_play_query_thread, NULL);
    pthread_attr_destroy(&attr);
    if (r != 0) {
        g_mas_busy = 0;
        LOGE(TAG, "pthread_create: %s", strerror(r));
        return -1;
    }
    return 0;
}
