#include "music_downloader.h"
#include "music_remote_list.h"

#include <cstdlib>
#include <cstring>
#include <string>

bool music_remote_keyword_is_vague(const std::string &keyword)
{
    static const char *generic[] = {
        "热门", "音乐", "歌", "歌曲", "一首歌", "来首歌", "听歌", "热门歌曲", NULL,
    };
    size_t i;

    if (keyword.empty()) {
        return true;
    }
    for (i = 0; generic[i] != NULL; ++i) {
        if (keyword == generic[i]) {
            return true;
        }
    }
    return false;
}

static const char *platform_from_env(void)
{
    const char *p = getenv("SMART_SPEAKER_MUSIC_PLATFORM");
    if (p != NULL && p[0] != '\0') {
        return p;
    }
    return "auto";
}

bool music_remote_list_music_page(const std::string &keyword, int page, int page_size, Json::Value &music,
                                  int &out_total, int &out_total_pages)
{
    music_search_result_t res;
    music_result_t mr;
    size_t i;

    memset(&res, 0, sizeof(res));
    if (page <= 0 || page_size <= 0 || keyword.empty()) {
        return false;
    }

    mr = music_search_page(keyword.c_str(), platform_from_env(), (uint32_t)page, (uint32_t)page_size, &res);
    if (mr != Ok) {
        return false;
    }

    out_total = (int)res.total;
    out_total_pages = (int)res.total_pages;

    for (i = 0; i < res.count; ++i) {
        music_info_t *info = &res.results[i];
        Json::Value item(Json::objectValue);

        item["singer"] = std::string(info->artist);
        item["song"] = std::string(info->name);
        item["path"] = "";
        item["source"] = std::string(info->source);
        item["song_id"] = std::string(info->id);
        music.append(item);
    }

    music_free_search_result(&res);
    return true;
}
