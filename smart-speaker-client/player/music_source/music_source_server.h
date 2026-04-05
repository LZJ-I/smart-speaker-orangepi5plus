#ifndef MUSIC_SOURCE_SERVER_H
#define MUSIC_SOURCE_SERVER_H

#include <json-c/json.h>

#include "music_source.h"

int music_source_server_parse_music_item(struct json_object *item_obj, MusicSourceItem *item);

#endif
