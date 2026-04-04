#include "music_lib_bridge.h"

#include <string.h>

#include "../debug_log.h"
#include "link.h"
#include "player.h"
#include "music_source/music_source.h"

#define TAG "MUSIC-LIB"

#define OFFLINE_LOAD_PAGE 2048

int music_lib_search_fill_list(const char *keyword, int max_count)
{
    int total_pages = 0;
    int count = 0;
    int page_size = (max_count > 0) ? max_count : 10;
    if (music_lib_search_fill_list_page(keyword, 1, page_size, &total_pages, &count) != 0) {
        return -1;
    }
    return count;
}

int music_lib_search_fill_list_page(const char *keyword, int page, int page_size, int *total_pages, int *filled_count)
{
    MusicSourceResult result;
    int i;
    if (keyword == NULL || page <= 0 || page_size <= 0) {
        return -1;
    }
    memset(&result, 0, sizeof(result));
    if (total_pages != NULL) *total_pages = 0;
    if (filled_count != NULL) *filled_count = 0;
    if (music_source_search(keyword, page, page_size, &result) != 0) {
        return -1;
    }
    link_clear_list();
    for (i = 0; i < result.count; ++i) {
        if (link_add_music_lib(result.items[i].source,
                               result.items[i].song_id,
                               result.items[i].singer,
                               result.items[i].song_name) != 0) {
            music_source_free_result(&result);
            return -1;
        }
    }
    if (total_pages != NULL) {
        *total_pages = result.total_pages;
    }
    if (filled_count != NULL) {
        *filled_count = result.count;
    }
    music_source_free_result(&result);
    return 0;
}

int music_lib_load_all_local_to_link(void)
{
    MusicSourceResult result;
    int page = 1;
    int total_pages = 0;
    int total = 0;
    int i;

    link_clear_list();
    for (;;) {
        memset(&result, 0, sizeof(result));
        if (music_source_search("", page, OFFLINE_LOAD_PAGE, &result) != 0) {
            music_source_free_result(&result);
            return -1;
        }
        total_pages = result.total_pages;
        for (i = 0; i < result.count; ++i) {
            if (link_add_music_lib(result.items[i].source,
                                   result.items[i].song_id,
                                   result.items[i].singer,
                                   result.items[i].song_name) != 0) {
                music_source_free_result(&result);
                return -1;
            }
            total++;
            LOGD(TAG, "offline[%d] source=%s singer=%s name=%s id=%s",
                 total,
                 result.items[i].source,
                 result.items[i].singer,
                 result.items[i].song_name,
                 result.items[i].song_id);
        }
        music_source_free_result(&result);
        if (total_pages <= 0 || page >= total_pages) {
            break;
        }
        page++;
    }
    LOGI(TAG, "离线曲库已载入链表: %d 首", total);
    return 0;
}

int music_lib_get_url_by_source_id(const char *source, const char *song_id, char *url_buf, size_t url_size)
{
    if (source == NULL || song_id == NULL || source[0] == '\0' || song_id[0] == '\0' || url_buf == NULL || url_size == 0) {
        return -1;
    }
    url_buf[0] = '\0';
    return music_source_get_url(source, song_id, url_buf, url_size);
}

int music_lib_get_url_for_music(const char *music_name, char *url_buf, size_t url_size)
{
    char source[MUSIC_SOURCE_MAX];
    char id[MUSIC_ID_MAX];
    char singer[SINGER_MAX_NAME];
    char song_name[MUSIC_MAX_NAME];
    const char *sep;
    if (music_name == NULL || url_buf == NULL || url_size == 0) return -1;
    singer[0] = '\0';
    song_name[0] = '\0';
    sep = strchr(music_name, '/');
    if (sep != NULL) {
        size_t singer_len = (size_t)(sep - music_name);
        if (singer_len >= sizeof(singer)) singer_len = sizeof(singer) - 1;
        memcpy(singer, music_name, singer_len);
        singer[singer_len] = '\0';
        strncpy(song_name, sep + 1, sizeof(song_name) - 1);
    } else {
        strncpy(song_name, music_name, sizeof(song_name) - 1);
    }
    song_name[sizeof(song_name) - 1] = '\0';
    if (link_get_source_id(song_name, singer, source, sizeof(source), id, sizeof(id)) == 0) {
        return music_lib_get_url_by_source_id(source, id, url_buf, url_size);
    }
    if (sep != NULL) {
        return music_source_get_url("local", music_name, url_buf, url_size);
    }
    return -1;
}

int music_lib_search_play_first(const char *keyword)
{
    int total_pages = 0;
    int count = 0;
    if (keyword == NULL || keyword[0] == '\0') return -1;
    if (music_lib_search_fill_list_page(keyword, 1, 10, &total_pages, &count) != 0 || count <= 0) return -1;
    player_start_play();
    return 0;
}
