#ifndef __MUSIC_SOURCE_MANAGER_H__
#define __MUSIC_SOURCE_MANAGER_H__

#include "music_source.h"

const MusicSourceBackend *music_source_local_backend(void);
const MusicSourceBackend *music_source_server_backend(void);

#endif
