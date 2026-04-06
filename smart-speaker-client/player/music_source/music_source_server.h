#ifndef MUSIC_SOURCE_SERVER_H
#define MUSIC_SOURCE_SERVER_H

#include <json-c/json.h>

#include "music_source.h"

int music_source_server_parse_music_item(struct json_object *item_obj, MusicSourceItem *item);
int music_source_server_load_playlist_detail(const char *playlist_id, const char *source, MusicSourceResult *out_result);
int music_source_server_load_playlist_detail_page(const char *playlist_id, const char *source,
                                                   int page, int page_size, MusicSourceResult *out_result);
int music_source_server_search_playlist_page(const char *keyword, const char *source,
                                             int page, int page_size, MusicSourceResult *out_result);

#endif
