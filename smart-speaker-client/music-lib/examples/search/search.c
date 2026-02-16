#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void print_search_results(const music_search_result_t* result) {
    if (!result) {
        printf("Result is NULL\n");
        return;
    }
    
    printf("Found %zu songs:\n", result->count);
    printf("====================\n");
    
    for (size_t i = 0; i < result->count; i++) {
        const music_info_t* song = &result->results[i];
        
        const char* source_name;
        if (strcmp(song->source, "tx") == 0) {
            source_name = "QQ 音乐";
        } else if (strcmp(song->source, "wy") == 0) {
            source_name = "网易云音乐";
        } else {
            source_name = song->source;
        }
        
        printf("[%zu] %s - %s\n", i + 1, song->name, song->artist);
        printf("     ID: %s\n", song->id);
        printf("     Album: %s\n", song->album);
        printf("     Source: %s (%s)\n", song->source, source_name);
        printf("--------------------\n");
    }
}

int main(int argc, char* argv[]) {
    const char* platform = "tx";
    const char* keyword = "你从未离去";
    
    if (argc >= 2) {
        platform = argv[1];
    }
    if (argc >= 3) {
        keyword = argv[2];
    }
    
    printf("搜索音乐\n");
    printf("====================\n");
    printf("平台: %s (%s)\n", platform, get_platform_name(platform));
    printf("搜索关键词: %s\n", keyword);
    printf("====================\n");
    
    music_search_result_t result = {0};
    music_result_t r = music_search(keyword, platform, &result);
    
    if (r != Ok) {
        printf("搜索失败，错误码: %d\n", r);
        return 1;
    }
    
    print_search_results(&result);
    music_free_search_result(&result);
    
    return 0;
}
