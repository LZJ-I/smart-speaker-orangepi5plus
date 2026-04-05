#ifndef MUSIC_REMOTE_LIST_H
#define MUSIC_REMOTE_LIST_H

#include <json/json.h>
#include <string>

bool music_remote_keyword_is_vague(const std::string &keyword);

void music_remote_apply_source_hints(std::string &keyword, std::string &platform,
                                     const std::string &fallback_platform);

bool music_remote_list_music_page(const std::string &keyword, int page, int page_size, Json::Value &music,
                                  int &out_total, int &out_total_pages);

#endif
