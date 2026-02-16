#ifndef MUSIC_DOWNLOADER_H
#define MUSIC_DOWNLOADER_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    Ok = 0,
    InvalidParam = 1,
    ApiError = 2,
    DownloadError = 3
} music_result_t;

typedef void (*progress_callback_t)(uint64_t downloaded, uint64_t total, void* user_data);

typedef struct {
    char id[64];
    char name[128];
    char artist[128];
    char album[128];
    char source[8];
    char* url;
} music_info_t;

typedef struct {
    music_info_t* results;
    size_t count;
} music_search_result_t;

char* music_get_url(
    const char* source,
    const char* song_id,
    const char* quality
);

music_result_t music_search(
    const char* keyword,
    const char* platform,
    music_search_result_t* result
);

void music_free_search_result(music_search_result_t* result);

music_result_t music_download(
    const char* source,
    const char* song_id,
    const char* quality,
    const char* output_dir,
    progress_callback_t callback,
    void* user_data
);

music_result_t music_download_with_path(
    const char* source,
    const char* song_id,
    const char* quality,
    const char* output_path,
    progress_callback_t callback,
    void* user_data
);

char* music_get_extension(const char* quality);
void music_free_string(char* s);

#endif