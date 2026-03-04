#include "music_lib_bridge.h"
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "player.h"
#include "link.h"
#include "../debug_log.h"
#include "../music-lib/examples/music.h"

#define TAG "MUSIC_LIB_BRIDGE"

typedef music_result_t (*fn_music_search_t)(const char *, const char *, music_search_result_t *);
typedef music_result_t (*fn_music_search_page_t)(const char *, const char *, uint32_t, uint32_t, music_search_result_t *);
typedef char *(*fn_music_get_url_t)(const char *, const char *, const char *);
typedef void (*fn_music_free_search_result_t)(music_search_result_t *);
typedef void (*fn_music_free_string_t)(char *);

typedef struct {
    void *handle;
    fn_music_search_t music_search;
    fn_music_search_page_t music_search_page;
    fn_music_get_url_t music_get_url;
    fn_music_free_search_result_t music_free_search_result;
    fn_music_free_string_t music_free_string;
} music_lib_api_t;

static int load_music_lib(music_lib_api_t *api)
{
    const char *candidates[] = {
        "../music-lib/target/release/libmusic_downloader.so",
        "./music-lib/target/release/libmusic_downloader.so",
        "../music-lib/target/debug/libmusic_downloader.so",
        "./music-lib/target/debug/libmusic_downloader.so",
        NULL
    };
    int i = 0;
    memset(api, 0, sizeof(*api));
    while (candidates[i] != NULL) {
        api->handle = dlopen(candidates[i], RTLD_NOW);
        if (api->handle != NULL) {
            break;
        }
        ++i;
    }
    if (api->handle == NULL) {
        LOGE(TAG, "加载music-lib动态库失败: %s", dlerror());
        return -1;
    }

    api->music_search = (fn_music_search_t)dlsym(api->handle, "music_search");
    api->music_search_page = (fn_music_search_page_t)dlsym(api->handle, "music_search_page");
    api->music_get_url = (fn_music_get_url_t)dlsym(api->handle, "music_get_url");
    api->music_free_search_result = (fn_music_free_search_result_t)dlsym(api->handle, "music_free_search_result");
    api->music_free_string = (fn_music_free_string_t)dlsym(api->handle, "music_free_string");
    if (api->music_search == NULL || api->music_search_page == NULL || api->music_get_url == NULL ||
        api->music_free_search_result == NULL || api->music_free_string == NULL) {
        LOGE(TAG, "解析music-lib符号失败");
        dlclose(api->handle);
        memset(api, 0, sizeof(*api));
        return -1;
    }
    return 0;
}

static void unload_music_lib(music_lib_api_t *api)
{
    if (api->handle != NULL) {
        dlclose(api->handle);
    }
    memset(api, 0, sizeof(*api));
}

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
    music_lib_api_t api;
    music_search_result_t result;
    size_t i;
    int count = 0;
    memset(&result, 0, sizeof(result));
    if (keyword == NULL || keyword[0] == '\0' || page <= 0 || page_size <= 0) {
        return -1;
    }
    if (total_pages != NULL) *total_pages = 0;
    if (filled_count != NULL) *filled_count = 0;
    if (load_music_lib(&api) != 0) {
        return -1;
    }
    if (api.music_search_page(keyword, "auto", (uint32_t)page, (uint32_t)page_size, &result) != Ok) {
        unload_music_lib(&api);
        return -1;
    }
    link_clear_list();
    for (i = 0; i < result.count; i++) {
        const char *src = result.results[i].source;
        const char *id = result.results[i].id;
        const char *artist = result.results[i].artist;
        const char *name = result.results[i].name;
        if (src != NULL && id != NULL && name != NULL &&
            link_add_music_lib(src, id, artist ? artist : "", name) == 0) {
            count++;
        }
    }
    if (total_pages != NULL) {
        *total_pages = (int)result.total_pages;
    }
    if (filled_count != NULL) {
        *filled_count = count;
    }
    api.music_free_search_result(&result);
    unload_music_lib(&api);
    return 0;
}

int music_lib_get_url_by_source_id(const char *source, const char *song_id, char *url_buf, size_t url_size)
{
    music_lib_api_t api;
    char *url = NULL;
    if (source == NULL || song_id == NULL || source[0] == '\0' || song_id[0] == '\0' || url_buf == NULL || url_size == 0) {
        return -1;
    }
    url_buf[0] = '\0';
    if (load_music_lib(&api) != 0) return -1;
    url = api.music_get_url(source, song_id, "320k");
    if (url == NULL || url[0] == '\0') {
        unload_music_lib(&api);
        return -1;
    }
    strncpy(url_buf, url, url_size - 1);
    url_buf[url_size - 1] = '\0';
    api.music_free_string(url);
    unload_music_lib(&api);
    return 0;
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
    if (link_get_source_id(song_name, singer, source, sizeof(source), id, sizeof(id)) != 0) {
        return -1;
    }
    return music_lib_get_url_by_source_id(source, id, url_buf, url_size);
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
