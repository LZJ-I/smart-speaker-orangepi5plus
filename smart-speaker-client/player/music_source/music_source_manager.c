#include "music_source.h"
#include "music_source_manager.h"

#include <stdlib.h>
#include <string.h>

#include "player.h"

static int g_online_search_blocked;

void music_source_clear_online_search_blocked(void)
{
    g_online_search_blocked = 0;
}

void music_source_set_online_search_blocked(int blocked)
{
    g_online_search_blocked = blocked ? 1 : 0;
}

int music_source_take_online_search_blocked(void)
{
    int x = g_online_search_blocked;
    g_online_search_blocked = 0;
    return x;
}

static const MusicSourceBackend *select_backend_by_source(const char *source)
{
    if (source != NULL && strcmp(source, "server") == 0) {
        return music_source_server_backend();
    }
    return music_source_local_backend();
}

int music_source_search(const char *keyword, int page, int page_size, MusicSourceResult *result)
{
    const MusicSourceBackend *primary;
    const MusicSourceBackend *fallback;
    int ret;
    if (keyword == NULL || result == NULL)
        return -1;
    primary = (g_current_online_mode == ONLINE_MODE_YES) ? music_source_server_backend() : music_source_local_backend();
    fallback = (primary == music_source_server_backend()) ? music_source_local_backend() : NULL;

    ret = primary->search(keyword, page, page_size, result);
    if (ret == 0 && result->online_search_disabled) {
        return 0;
    }
    if (ret == 0 && result->count > 0) {
        return 0;
    }
    if (fallback != NULL) {
        music_source_free_result(result);
        ret = fallback->search(keyword, page, page_size, result);
        if (ret == 0) {
            return 0;
        }
    }
    return ret;
}

int music_source_get_url(const char *source, const char *song_id, char *url_buf, size_t url_size)
{
    const MusicSourceBackend *backend;
    const MusicSourceBackend *srv;
    if (song_id == NULL || url_buf == NULL || url_size == 0) return -1;
    backend = select_backend_by_source(source);
    if (backend->get_url(source != NULL ? source : "local", song_id, url_buf, url_size) == 0 && url_buf[0] != '\0') {
        return 0;
    }
    srv = music_source_server_backend();
    if (g_current_online_mode == ONLINE_MODE_YES && source != NULL && strcmp(source, "local") != 0 &&
        strcmp(source, "server") != 0 && srv->get_url(source, song_id, url_buf, url_size) == 0 && url_buf[0] != '\0') {
        return 0;
    }
    if (backend != music_source_local_backend()) {
        return music_source_local_backend()->get_url("local", song_id, url_buf, url_size);
    }
    return -1;
}

void music_source_free_result(MusicSourceResult *result)
{
    if (result == NULL || result->items == NULL) {
        if (result != NULL) memset(result, 0, sizeof(*result));
        return;
    }
    free(result->items);
    memset(result, 0, sizeof(*result));
}
