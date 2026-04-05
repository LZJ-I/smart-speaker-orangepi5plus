#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void progress(uint64_t down, uint64_t total, void* data) {
    (void)data;
    if (total > 0) {
        printf("\r%.1f%%", (double)down / total * 100);
    } else {
        printf("\r%llu bytes", (unsigned long long)down);
    }
    fflush(stdout);
}

const char* get_platform_name(const char* source) {
    if (strcmp(source, "tx") == 0) {
        return "QQ 音乐";
    } else if (strcmp(source, "wy") == 0) {
        return "网易云音乐";
    } else {
        return source;
    }
}

int main(int argc, char* argv[]) {
    const char* platform = "tx";
    const char* song_id = "001StgLm3NMZB";
    const char* quality = "320k";
    const char* output_path = "music-downloads";
    
    if (argc >= 2) {
        platform = argv[1];
    }
    if (argc >= 3) {
        song_id = argv[2];
    }
    if (argc >= 4) {
        quality = argv[3];
    }
    if (argc >= 5) {
        output_path = argv[4];
    }
    
    printf("下载音乐\n");
    printf("====================\n");
    printf("平台: %s (%s)\n", platform, get_platform_name(platform));
    printf("歌曲 ID: %s\n", song_id);
    printf("音质: %s\n", quality);
    printf("输出目录: %s\n", output_path);
    printf("====================\n");
    
    int use_auto = (strcmp(platform, "auto") == 0);
    music_result_t r;
    
    if (use_auto) {
        printf("尝试平台: tx (%s)\n", get_platform_name("tx"));
        r = music_download("tx", song_id, quality, output_path, progress, NULL);
        
        if (r != Ok) {
            printf("\n平台 tx 失败，尝试平台: wy (%s)\n", get_platform_name("wy"));
            r = music_download("wy", song_id, quality, output_path, progress, NULL);
        }
    } else {
        r = music_download(platform, song_id, quality, output_path, progress, NULL);
    }
    
    if (r == Ok) {
        printf("\n✓ Success\n");
    } else {
        printf("\n✗ Failed with error code: %d\n", r);
    }
    
    return 0;
}
