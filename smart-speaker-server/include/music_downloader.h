#ifndef MUSIC_DOWNLOADER_H
#define MUSIC_DOWNLOADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    Ok = 0,
    InvalidParam = 1,
    ApiError = 2,
    DownloadError = 3
} music_result_t;

typedef void (*progress_callback_t)(uint64_t downloaded, uint64_t total, void *user_data);

typedef struct {
    char id[64];
    char name[128];
    char artist[128];
    char album[128];
    char source[8];
    char *url;
} music_info_t;

typedef struct {
    music_info_t *results;
    size_t count;
    uint32_t page;
    uint32_t page_size;
    uint32_t total;
    uint32_t total_pages;
} music_search_result_t;

char *music_get_url(const char *source, const char *song_id, const char *quality);

void music_configure_online(const char *api_url, const char *api_key, const char *user_agent);

int music_api_configured(void);

typedef struct {
    char *play_url;
    char *source;
    char *song_id;
    char *singer;
    char *song;
} music_resolve_result_t;

music_result_t music_resolve_keyword(const char *keyword, const char *platform, const char *quality,
                                     music_resolve_result_t *out);
void music_free_resolve_result(music_resolve_result_t *out);

char *music_search_first_url(const char *keyword, const char *platform, const char *quality);

music_result_t music_search(const char *keyword, const char *platform, music_search_result_t *result);
music_result_t music_search_page(const char *keyword, const char *platform, uint32_t page, uint32_t page_size,
                                 music_search_result_t *result);

void music_free_search_result(music_search_result_t *result);

music_result_t music_download(const char *source, const char *song_id, const char *quality, const char *output_dir,
                              progress_callback_t callback, void *user_data);

music_result_t music_download_with_path(const char *source, const char *song_id, const char *quality,
                                        const char *output_path, progress_callback_t callback, void *user_data);

char *music_get_extension(const char *quality);
void music_free_string(char *s);

#ifdef __cplusplus
}
#endif

#endif
