#ifndef MUSIC_SERVER_ASYNC_H
#define MUSIC_SERVER_ASYNC_H

#include "music_source.h"

typedef enum {
    MUSIC_ASYNC_OK_RESOLVE,
    MUSIC_ASYNC_OK_SEARCH,
    MUSIC_ASYNC_OK_PLAYLIST,
    MUSIC_ASYNC_FAIL,
} music_async_out_t;

void select_music_async_play_query_done(music_async_out_t out, MusicSourceItem *item, MusicSourceResult *search_res,
                                        const char *query);

int music_server_async_init(void);
int music_server_async_fd(void);
void music_server_async_on_readable(void);
void music_server_async_cancel_pending(void);
int music_server_async_start_play_query(const char *query, const char *source);

#endif
