#include "../music.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    const char *platform = "auto";
    const char *keyword = "你从未离去";
    const char *quality = "128k";

    if (argc >= 2) {
        platform = argv[1];
    }
    if (argc >= 3) {
        keyword = argv[2];
    }
    if (argc >= 4) {
        quality = argv[3];
    }

    if (!music_api_configured()) {
        fprintf(stderr, "未设置 SMART_SPEAKER_MUSIC_API_KEY，跳过取链。\n");
        return 2;
    }

    char *url = music_search_first_url(keyword, platform, quality);
    if (url == NULL || url[0] == '\0') {
        fprintf(stderr, "music_search_first_url 失败\n");
        return 1;
    }
    printf("%s\n", url);
    music_free_string(url);
    return 0;
}
