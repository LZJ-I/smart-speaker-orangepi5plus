#include "../music.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    const char *platform = "auto";
    const char *keyword = "你从未离去";
    const char *quality = "128k";

    if (argc < 2) {
        fprintf(stderr, "用法: %s <music_api_key> [platform] [keyword] [quality]\n", argv[0]);
        fprintf(stderr, "（与 smart-speaker-server 中 data/config/music.toml 的 music_api_key 一致）\n");
        return 2;
    }

    music_configure_online("https://source.shiqianjiang.cn/api/music", argv[1], "lx-music-request/2.12.0");

    if (!music_api_configured()) {
        fprintf(stderr, "music_api_key 为空\n");
        return 2;
    }

    if (argc >= 3) {
        platform = argv[2];
    }
    if (argc >= 4) {
        keyword = argv[3];
    }
    if (argc >= 5) {
        quality = argv[4];
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
