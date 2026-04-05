#ifndef __MUSIC_SOURCE_H__
#define __MUSIC_SOURCE_H__

#include <stddef.h>

#include "link.h"

typedef struct {
    char source[MUSIC_SOURCE_MAX];
    char song_id[MUSIC_ID_MAX];
    char song_name[MUSIC_MAX_NAME];
    char singer[SINGER_MAX_NAME];
    char play_url[MUSIC_PLAY_URL_MAX];
} MusicSourceItem;

typedef struct {
    MusicSourceItem *items;
    int count;
    int total;
    int total_pages;
    int current_page;
} MusicSourceResult;

typedef struct {
    const char *name;
    int (*search)(const char *keyword, int page, int page_size, MusicSourceResult *result);
    int (*get_url)(const char *source, const char *song_id, char *url_buf, size_t url_size);
    void (*free_result)(MusicSourceResult *result);
} MusicSourceBackend;

int music_source_search(const char *keyword, int page, int page_size, MusicSourceResult *result);
int music_source_get_url(const char *source, const char *song_id, char *url_buf, size_t url_size);
void music_source_free_result(MusicSourceResult *result);

#endif
