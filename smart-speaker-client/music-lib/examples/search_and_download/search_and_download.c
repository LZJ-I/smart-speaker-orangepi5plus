#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 音质优先级（从高到低）
static const char* QUALITY_PRIORITY[] = {
    "master",
    "atmos_plus",
    "atmos",
    "hires",
    "flac24bit",
    "flac",
    "320k",
    "128k"
};
static const int QUALITY_COUNT = sizeof(QUALITY_PRIORITY) / sizeof(QUALITY_PRIORITY[0]);

// 获取音质在优先级数组中的索引，-1 表示未找到
int get_quality_index(const char* quality) {
    for (int i = 0; i < QUALITY_COUNT; i++) {
        if (strcmp(QUALITY_PRIORITY[i], quality) == 0) {
            return i;
        }
    }
    return -1;
}

void progress(uint64_t down, uint64_t total, void* data) {
    (void)data;
    if (total > 0) {
        printf("\r下载中: %.1f%%", (double)down / total * 100);
    } else {
        printf("\r下载中: %llu bytes", (unsigned long long)down);
    }
    fflush(stdout);
}

void sanitize_filename(char* filename) {
    char* p = filename;
    while (*p) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' || 
            *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') {
            *p = '_';
        }
        p++;
    }
}

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
    const char* keyword = "你从未离去";
    const char* target_quality = "320k";
    const char* output_dir = "music-downloads";
    
    if (argc >= 2) {
        platform = argv[1];
    }
    if (argc >= 3) {
        keyword = argv[2];
    }
    if (argc >= 4) {
        target_quality = argv[3];
    }
    if (argc >= 5) {
        output_dir = argv[4];
    }
    
    printf("搜索并下载音乐\n");
    printf("====================\n");
    printf("平台: %s (%s)\n", platform, get_platform_name(platform));
    printf("搜索关键词: %s\n", keyword);
    printf("目标音质: %s\n", target_quality);
    printf("输出目录: %s\n", output_dir);
    printf("====================\n\n");
    
    music_search_result_t result = {0};
    music_result_t r = music_search(keyword, platform, &result);
    
    if (r != Ok) {
        printf("搜索失败，错误码: %d\n", r);
        return 1;
    }
    
    if (result.count == 0) {
        printf("未找到任何歌曲\n");
        music_free_search_result(&result);
        return 0;
    }
    
    printf("找到 %zu 首歌曲\n\n", result.count);
    
    // 确定从哪个音质开始尝试
    int start_quality_index = get_quality_index(target_quality);
    if (start_quality_index == -1) {
        printf("目标音质 %s 不在支持列表中，从最低音质开始\n", target_quality);
        start_quality_index = QUALITY_COUNT - 1;
    }
    
    // 最多尝试 5 首歌
    int max_songs = (result.count > 5) ? 5 : (int)result.count;
    int download_success = 0;
    
    for (int song_idx = 0; song_idx < max_songs; song_idx++) {
        const music_info_t* song = &result.results[song_idx];
        
        printf("--- 尝试第 %d 首歌 ---\n", song_idx + 1);
        printf("歌曲信息:\n");
        printf("  名称: %s\n", song->name);
        printf("  艺术家: %s\n", song->artist);
        printf("  专辑: %s\n", song->album);
        printf("  ID: %s\n", song->id);
        printf("  来源: %s (%s)\n\n", song->source, get_platform_name(song->source));
        
        // 对这首歌，从目标音质开始降级尝试
        for (int q_idx = start_quality_index; q_idx < QUALITY_COUNT; q_idx++) {
            const char* current_quality = QUALITY_PRIORITY[q_idx];
            
            printf("  尝试音质: %s... ", current_quality);
            
            char* ext = music_get_extension(current_quality);
            char filename[512];
            char output_path[1024];
            
            snprintf(filename, sizeof(filename), "%s-%s.%s", song->name, song->artist, ext);
            sanitize_filename(filename);
            snprintf(output_path, sizeof(output_path), "%s/%s", output_dir, filename);
            
            music_free_string(ext);
            
            r = music_download_with_path(
                song->source, song->id, current_quality, output_path, progress, NULL
            );
            
            if (r == Ok) {
                printf("\n✓ 下载成功 (音质: %s)!\n", current_quality);
                printf("文件已保存为: %s\n", output_path);
                download_success = 1;
                goto download_done;
            } else {
                printf("失败\n");
            }
        }
        
        printf("  这首歌所有音质都尝试失败，尝试下一首...\n\n");
    }
    
download_done:
    if (!download_success) {
        printf("\n✗ 前 %d 首歌都无法下载\n", max_songs);
    }
    
    music_free_search_result(&result);
    
    return download_success ? 0 : 1;
}
