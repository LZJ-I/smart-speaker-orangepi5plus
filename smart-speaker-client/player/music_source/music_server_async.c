#include "music_server_async.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug_log.h"
#include "music_source_manager.h"
#include "player.h"
#include "player_constants.h"
#include "select.h"
#include "select_text.h"

#define TAG "MUSIC-ASYNC"

typedef struct MasJob {
    char q[256];
    char source[16];
    uint32_t token;
} MasJob;

static int g_mas_pipe[2] = {-1, -1};
static pthread_mutex_t g_mas_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_latest_token;

static char g_query[256];
static char g_source[16];
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

static void mas_publish_or_drop(uint32_t job_tok, music_async_out_t out, const MusicSourceItem *it, MusicSourceResult *sr,
                                const char *q_for_display, const char *source_for_display)
{
    pthread_mutex_lock(&g_mas_mu);
    if (job_tok != g_latest_token) {
        if ((out == MUSIC_ASYNC_OK_SEARCH || out == MUSIC_ASYNC_OK_PLAYLIST) &&
            sr != NULL && sr->items != NULL) {
            music_source_free_result(sr);
            memset(sr, 0, sizeof(*sr));
        }
        pthread_mutex_unlock(&g_mas_mu);
        return;
    }
    g_pending_out = out;
    memset(&g_pending_item, 0, sizeof(g_pending_item));
    memset(&g_pending_search, 0, sizeof(g_pending_search));
    if (q_for_display != NULL) {
        snprintf(g_query, sizeof(g_query), "%s", q_for_display);
    } else {
        g_query[0] = '\0';
    }
    if (source_for_display != NULL) {
        snprintf(g_source, sizeof(g_source), "%s", source_for_display);
    } else {
        g_source[0] = '\0';
    }
    if (out == MUSIC_ASYNC_OK_RESOLVE && it != NULL) {
        g_pending_item = *it;
    } else if ((out == MUSIC_ASYNC_OK_SEARCH || out == MUSIC_ASYNC_OK_PLAYLIST) && sr != NULL) {
        g_pending_search = *sr;
        memset(sr, 0, sizeof(*sr));
    }
    pthread_mutex_unlock(&g_mas_mu);
    mas_notify_write();
}

static void *mas_play_query_thread(void *arg)
{
    MasJob *job = (MasJob *)arg;
    char q[256];
    char source[16];
    uint32_t tok;
    MusicSourceItem it;
    MusicSourceResult sr;
    player_playlist_ctx_t pl;

    if (job == NULL) {
        return NULL;
    }
    snprintf(q, sizeof(q), "%s", job->q);
    snprintf(source, sizeof(source), "%s", job->source);
    tok = job->token;
    free(job);
    job = NULL;

    memset(&it, 0, sizeof(it));
    memset(&sr, 0, sizeof(sr));

    if (g_current_online_mode == ONLINE_MODE_YES) {
        if (select_text_is_playlist_query(q)) {
            char playlist_kw[256];
            select_text_normalize_playlist_keyword(q, playlist_kw, sizeof(playlist_kw));
            LOGI(TAG, "歌单检索 source=%s raw=%s norm=%s", source, q, playlist_kw);
            if (music_source_server_resolve_playlist_keyword(playlist_kw, source, &sr) == 0 && sr.count > 0) {
                mas_publish_or_drop(tok, MUSIC_ASYNC_OK_PLAYLIST, NULL, &sr, q, source);
                return NULL;
            }
            music_source_free_result(&sr);
            memset(&sr, 0, sizeof(sr));
            mas_publish_or_drop(tok, MUSIC_ASYNC_FAIL, NULL, NULL, q, source);
            return NULL;
        } else if (music_source_server_resolve_keyword(q, source, &it) == 0) {
            mas_publish_or_drop(tok, MUSIC_ASYNC_OK_RESOLVE, &it, NULL, q, source);
            return NULL;
        }
    }

    player_get_playlist_ctx(&pl);
    if (music_source_search(q, 1, pl.page_size > 0 ? pl.page_size : PLAYER_ONLINE_PLAYLIST_PAGE_SIZE, &sr) != 0 ||
        sr.count <= 0) {
        if (sr.count <= 0 && sr.online_search_disabled) {
            music_source_set_online_search_blocked(1);
        }
        music_source_free_result(&sr);
        memset(&sr, 0, sizeof(sr));
        mas_publish_or_drop(tok, MUSIC_ASYNC_FAIL, NULL, NULL, q, source);
        return NULL;
    }

    mas_publish_or_drop(tok, MUSIC_ASYNC_OK_SEARCH, NULL, &sr, q, source);
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
    g_query[0] = '\0';
    g_source[0] = '\0';
    pthread_mutex_unlock(&g_mas_mu);

    if (out == MUSIC_ASYNC_OK_RESOLVE) {
        select_music_async_play_query_done(out, &it, NULL, q);
    } else if (out == MUSIC_ASYNC_OK_SEARCH || out == MUSIC_ASYNC_OK_PLAYLIST) {
        select_music_async_play_query_done(out, NULL, &sr, q);
    } else {
        select_music_async_play_query_done(MUSIC_ASYNC_FAIL, NULL, NULL, q);
    }
}

int music_server_async_start_play_query(const char *query, const char *source)
{
    pthread_t th;
    pthread_attr_t attr;
    int r;
    MasJob *job;
    if (query == NULL || query[0] == '\0') {
        return -1;
    }
    if (g_mas_pipe[0] < 0) {
        return -1;
    }
    job = (MasJob *)malloc(sizeof(*job));
    if (job == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_mas_mu);
    g_latest_token++;
    job->token = g_latest_token;
    snprintf(job->q, sizeof(job->q), "%s", query);
    snprintf(job->source, sizeof(job->source), "%s", source != NULL ? source : "");
    pthread_mutex_unlock(&g_mas_mu);

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    r = pthread_create(&th, &attr, mas_play_query_thread, job);
    pthread_attr_destroy(&attr);
    if (r != 0) {
        free(job);
        LOGE(TAG, "pthread_create: %s", strerror(r));
        return -1;
    }
    return 0;
}
