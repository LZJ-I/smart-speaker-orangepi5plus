#ifndef __MUSIC_SOURCE_MANAGER_H__
#define __MUSIC_SOURCE_MANAGER_H__

#include "music_source.h"

const MusicSourceBackend *music_source_local_backend(void);
const MusicSourceBackend *music_source_server_backend(void);
int music_source_server_resolve_keyword(const char *keyword, MusicSourceItem *out_item);
int music_source_server_list_music_page(const char *keyword, int page, int page_size, MusicSourceResult *result);

#endif
