#include "music_source_manager.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "../player_constants.h"

typedef struct {
    MusicSourceItem *items;
    int count;
    int capacity;
} LocalCollectContext;

static const char *local_music_root(void)
{
    const char *env_path = getenv(LOCAL_MUSIC_ROOT_ENV);
    if (env_path != NULL && env_path[0] != '\0') {
        return env_path;
    }
    return SDCARD_MOUNT_PATH;
}

static void local_result_reset(MusicSourceResult *result)
{
    if (result == NULL) return;
    memset(result, 0, sizeof(*result));
}

static void local_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) return;
    dst[0] = '\0';
    if (src == NULL) return;
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static int local_has_audio_ext(const char *filename)
{
    const char *ext;
    if (filename == NULL) return 0;
    ext = strrchr(filename, '.');
    if (ext == NULL) return 0;
    return strcasecmp(ext, ".mp3") == 0 ||
           strcasecmp(ext, ".wav") == 0 ||
           strcasecmp(ext, ".flac") == 0;
}

static void local_pick_singer(const char *relative_path, char *singer, size_t singer_size)
{
    const char *sep;
    if (singer == NULL || singer_size == 0) return;
    singer[0] = '\0';
    if (relative_path == NULL) return;
    sep = strchr(relative_path, '/');
    if (sep == NULL) return;
    {
        size_t len = (size_t)(sep - relative_path);
        if (len >= singer_size) len = singer_size - 1;
        memcpy(singer, relative_path, len);
        singer[len] = '\0';
    }
}

static int local_match_keyword(const char *keyword, const MusicSourceItem *item)
{
    if (keyword == NULL || keyword[0] == '\0') return 1;
    if (strcmp(keyword, "热门") == 0) return 1;
    if (strstr(item->song_name, keyword) != NULL) return 1;
    if (strstr(item->singer, keyword) != NULL) return 1;
    if (strstr(item->song_id, keyword) != NULL) return 1;
    return 0;
}

static int local_push_item(LocalCollectContext *ctx, const MusicSourceItem *item)
{
    MusicSourceItem *new_items;
    int new_capacity;
    if (ctx == NULL || item == NULL) return -1;
    if (ctx->count >= ctx->capacity) {
        new_capacity = (ctx->capacity == 0) ? 32 : ctx->capacity * 2;
        new_items = (MusicSourceItem *)realloc(ctx->items, sizeof(MusicSourceItem) * new_capacity);
        if (new_items == NULL) return -1;
        ctx->items = new_items;
        ctx->capacity = new_capacity;
    }
    ctx->items[ctx->count++] = *item;
    return 0;
}

static int local_scan_dir(const char *root_path, const char *relative_path, const char *keyword, LocalCollectContext *ctx)
{
    DIR *dir;
    struct dirent *entry;
    dir = opendir(root_path);
    if (dir == NULL) {
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        struct stat st;
        char child_abs[1024];
        char child_rel[MUSIC_ID_MAX];
        MusicSourceItem item;
        if (entry->d_name[0] == '.') continue;
        if (snprintf(child_abs, sizeof(child_abs), "%s/%s", root_path, entry->d_name) >= (int)sizeof(child_abs)) {
            continue;
        }
        if (relative_path != NULL && relative_path[0] != '\0') {
            if (snprintf(child_rel, sizeof(child_rel), "%s/%s", relative_path, entry->d_name) >= (int)sizeof(child_rel)) {
                continue;
            }
        } else {
            if (snprintf(child_rel, sizeof(child_rel), "%s", entry->d_name) >= (int)sizeof(child_rel)) {
                continue;
            }
        }
        if (stat(child_abs, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (local_scan_dir(child_abs, child_rel, keyword, ctx) != 0 && errno != ENOENT) {
                closedir(dir);
                return -1;
            }
            continue;
        }
        if (!S_ISREG(st.st_mode) || !local_has_audio_ext(entry->d_name)) {
            continue;
        }
        memset(&item, 0, sizeof(item));
        local_copy_text(item.source, sizeof(item.source), "local");
        local_copy_text(item.song_id, sizeof(item.song_id), child_rel);
        local_copy_text(item.song_name, sizeof(item.song_name), entry->d_name);
        local_pick_singer(child_rel, item.singer, sizeof(item.singer));
        if (!local_match_keyword(keyword, &item)) {
            continue;
        }
        if (local_push_item(ctx, &item) != 0) {
            closedir(dir);
            free(ctx->items);
            ctx->items = NULL;
            ctx->count = 0;
            ctx->capacity = 0;
            return -1;
        }
    }
    closedir(dir);
    return 0;
}

static void local_shuffle_items(MusicSourceItem *items, int count)
{
    int i;
    if (items == NULL || count <= 1) return;
    for (i = count - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        MusicSourceItem tmp = items[i];
        items[i] = items[j];
        items[j] = tmp;
    }
}

static int local_fill_page(const LocalCollectContext *ctx, int page, int page_size, MusicSourceResult *result)
{
    int start;
    int end;
    int out_count;
    if (ctx == NULL || result == NULL || page <= 0 || page_size <= 0) return -1;
    local_result_reset(result);
    result->total = ctx->count;
    result->current_page = page;
    result->total_pages = (ctx->count == 0) ? 0 : ((ctx->count + page_size - 1) / page_size);
    if (ctx->count == 0) return 0;
    start = (page - 1) * page_size;
    if (start >= ctx->count) return 0;
    end = start + page_size;
    if (end > ctx->count) end = ctx->count;
    out_count = end - start;
    result->items = (MusicSourceItem *)calloc((size_t)out_count, sizeof(MusicSourceItem));
    if (result->items == NULL) return -1;
    memcpy(result->items, ctx->items + start, sizeof(MusicSourceItem) * (size_t)out_count);
    result->count = out_count;
    return 0;
}

static int music_source_local_search(const char *keyword, int page, int page_size, MusicSourceResult *result)
{
    LocalCollectContext ctx;
    const char *root = local_music_root();
    int ret;
    memset(&ctx, 0, sizeof(ctx));
    if (page <= 0 || page_size <= 0 || result == NULL) return -1;
    local_result_reset(result);
    ret = local_scan_dir(root, "", keyword, &ctx);
    if (ret != 0 && errno != ENOENT) {
        free(ctx.items);
        return -1;
    }
    if (keyword != NULL && strcmp(keyword, "热门") == 0) {
        local_shuffle_items(ctx.items, ctx.count);
    }
    ret = local_fill_page(&ctx, page, page_size, result);
    free(ctx.items);
    return ret;
}

static int music_source_local_get_url(const char *source, const char *song_id, char *url_buf, size_t url_size)
{
    char abs_path[1024];
    if (source == NULL || song_id == NULL || url_buf == NULL || url_size == 0) return -1;
    if (strcmp(source, "local") != 0) return -1;
    if (snprintf(abs_path, sizeof(abs_path), "%s/%s", local_music_root(), song_id) >= (int)sizeof(abs_path)) {
        return -1;
    }
    if (snprintf(url_buf, url_size, "file://%s", abs_path) >= (int)url_size) {
        return -1;
    }
    return 0;
}

static void music_source_local_free_result(MusicSourceResult *result)
{
    if (result == NULL) return;
    free(result->items);
    local_result_reset(result);
}

const MusicSourceBackend *music_source_local_backend(void)
{
    static const MusicSourceBackend backend = {
        .name = "local",
        .search = music_source_local_search,
        .get_url = music_source_local_get_url,
        .free_result = music_source_local_free_result
    };
    return &backend;
}
