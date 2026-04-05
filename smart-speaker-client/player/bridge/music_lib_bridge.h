#ifndef __MUSIC_LIB_BRIDGE_H__
#define __MUSIC_LIB_BRIDGE_H__

#include <stddef.h>

#include "music_source.h"

int music_lib_resolve_insert_item_after_current(const MusicSourceItem *item);
int music_lib_insert_search_result_after_current(MusicSourceResult *result, int *out_added);

int music_lib_search_play_first(const char *keyword);
int music_lib_search_fill_list(const char *keyword, int max_count);
int music_lib_get_url_for_music(const char *music_name, char *url_buf, size_t url_size);
int music_lib_search_fill_list_page(const char *keyword, int page, int page_size, int *total_pages, int *filled_count);
int music_lib_get_url_by_source_id(const char *source, const char *song_id, char *url_buf, size_t url_size);
int music_lib_load_all_local_to_link(void);
int music_lib_insert_search_after_current(const char *keyword, int page, int page_size, int *out_added);
int music_lib_resolve_insert_one_after_current(const char *keyword);

#endif
