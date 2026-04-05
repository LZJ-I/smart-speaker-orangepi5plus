#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "music.h"

const char* get_platform_name(const char* platform) {
    if (strcmp(platform, "tx") == 0) {
        return "QQ 音乐";
    } else if (strcmp(platform, "wy") == 0) {
        return "网易云音乐";
    } else if (strcmp(platform, "auto") == 0) {
        return "自动选择";
    } else {
        return platform;
    }
}

int main(int argc, char* argv[]) {
    const char* platform = "tx";
    const char* song_id = "001StgLm3NMZBG";
    const char* quality = "320k";
    
    if (argc >= 2) {
        platform = argv[1];
    }
    if (argc >= 3) {
        song_id = argv[2];
    }
    if (argc >= 4) {
        quality = argv[3];
    }
    
    printf("获取音乐下载链接\n");
    printf("========================================\n");
    printf("平台: %s (%s)\n", platform, get_platform_name(platform));
    printf("歌曲 ID: %s\n", song_id);
    printf("音质: %s\n", quality);
    printf("========================================\n\n");
    
    char* url = music_get_url(platform, song_id, quality);
    
    if (url != NULL) {
        printf("获取成功！\n");
        printf("下载链接: %s\n", url);
        music_free_string(url);
    } else {
        printf("获取失败！\n");
    }
    
    return 0;
}
