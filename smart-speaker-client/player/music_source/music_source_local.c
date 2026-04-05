#include "music_source_manager.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "player_constants.h"

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

static void local_trim_spaces(char *text)
{
    char *start;
    size_t len;

    if (text == NULL || text[0] == '\0') return;
    start = text;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }
    len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t')) {
        text[--len] = '\0';
    }
}

static void local_parse_song_meta(const char *filename, const char *fallback_singer,
                                  char *song_name, size_t song_name_size,
                                  char *singer, size_t singer_size)
{
    char base[MUSIC_MAX_NAME];
    char parsed_singer[SINGER_MAX_NAME];
    char *ext;
    char *sep;

    if (song_name == NULL || song_name_size == 0 || singer == NULL || singer_size == 0) return;
    song_name[0] = '\0';
    singer[0] = '\0';

    local_copy_text(base, sizeof(base), filename);
    ext = strrchr(base, '.');
    if (ext != NULL) {
        *ext = '\0';
    }

    sep = strstr(base, " - ");
    if (sep != NULL && sep != base && sep[3] != '\0') {
        *sep = '\0';
        local_copy_text(song_name, song_name_size, base);
        local_copy_text(parsed_singer, sizeof(parsed_singer), sep + 3);
        local_trim_spaces(song_name);
        local_trim_spaces(parsed_singer);
        if (parsed_singer[0] != '\0') {
            local_copy_text(singer, singer_size, parsed_singer);
        }
    } else {
        local_copy_text(song_name, song_name_size, base);
        local_trim_spaces(song_name);
    }

    if (singer[0] == '\0') {
        local_copy_text(singer, singer_size, fallback_singer);
        local_trim_spaces(singer);
    }
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

static int utf8_is_de_zh(const char *p)
{
    if (p == NULL || p[0] == '\0' || p[1] == '\0' || p[2] == '\0') {
        return 0;
    }
    return (unsigned char)p[0] == 0xe7 && (unsigned char)p[1] == 0x9a && (unsigned char)p[2] == 0x84;
}

static int local_dual_match_parts(const char *aaa, const char *bbb, const MusicSourceItem *item)
{
    if (aaa == NULL || bbb == NULL || item == NULL) return 0;
    if (aaa[0] == '\0' || bbb[0] == '\0') return 0;
    return (strstr(item->singer, aaa) != NULL && strstr(item->song_name, bbb) != NULL) ||
           (strstr(item->singer, bbb) != NULL && strstr(item->song_name, aaa) != NULL);
}

static int local_bbb_only_match(const char *bbb, const MusicSourceItem *item)
{
    if (bbb == NULL || bbb[0] == '\0' || item == NULL) return 0;
    return strstr(item->song_name, bbb) != NULL || strstr(item->song_id, bbb) != NULL;
}

static int local_try_split_singer_song_keyword(const char *keyword, char *aaa, size_t aaa_sz, char *bbb, size_t bbb_sz)
{
    const char *sep;
    size_t left_len;
    if (keyword == NULL || aaa == NULL || bbb == NULL || aaa_sz == 0 || bbb_sz == 0) {
        return 0;
    }
    aaa[0] = '\0';
    bbb[0] = '\0';
    for (sep = strstr(keyword, "的"); sep != NULL; sep = strstr(sep + 3, "的")) {
        if (sep == keyword || !utf8_is_de_zh(sep) || sep[3] == '\0') {
            continue;
        }
        left_len = (size_t)(sep - keyword);
        if (left_len == 0 || left_len >= aaa_sz) {
            continue;
        }
        memcpy(aaa, keyword, left_len);
        aaa[left_len] = '\0';
        local_copy_text(bbb, bbb_sz, sep + 3);
        local_trim_spaces(aaa);
        local_trim_spaces(bbb);
        if (strlen(aaa) >= 2 && strlen(bbb) >= 2) {
            return 1;
        }
        continue;
    }
    return 0;
}

static int local_match_keyword(const char *keyword, const MusicSourceItem *item)
{
    const char *sep;
    if (keyword == NULL || keyword[0] == '\0') return 1;
    if (strcmp(keyword, "热门") == 0) return 1;
    if (strstr(item->song_name, keyword) != NULL) return 1;
    if (strstr(item->singer, keyword) != NULL) return 1;
    if (strstr(item->song_id, keyword) != NULL) return 1;
    for (sep = strstr(keyword, "的"); sep != NULL; sep = strstr(sep + 3, "的")) {
        if (sep != keyword && utf8_is_de_zh(sep) && sep[3] != '\0') {
            char left[256];
            char right[256];
            size_t left_len = (size_t)(sep - keyword);
            if (left_len < sizeof(left)) {
                memcpy(left, keyword, left_len);
                left[left_len] = '\0';
                local_copy_text(right, sizeof(right), sep + 3);
                local_trim_spaces(left);
                local_trim_spaces(right);
                if (strlen(left) >= 2 && strlen(right) >= 2) {
                    if ((strstr(item->singer, left) != NULL && strstr(item->song_name, right) != NULL) ||
                        (strstr(item->singer, right) != NULL && strstr(item->song_name, left) != NULL)) {
                        return 1;
                    }
                }
            }
        }
    }
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
        local_pick_singer(child_rel, item.singer, sizeof(item.singer));
        local_parse_song_meta(entry->d_name, item.singer,
                              item.song_name, sizeof(item.song_name),
                              item.singer, sizeof(item.singer));
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

static int local_scan_dir_tiered(const char *root_path, const char *relative_path, const char *aaa, const char *bbb,
                                 LocalCollectContext *dual_ctx, LocalCollectContext *bbb_ctx)
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
            if (local_scan_dir_tiered(child_abs, child_rel, aaa, bbb, dual_ctx, bbb_ctx) != 0 && errno != ENOENT) {
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
        local_pick_singer(child_rel, item.singer, sizeof(item.singer));
        local_parse_song_meta(entry->d_name, item.singer,
                              item.song_name, sizeof(item.song_name),
                              item.singer, sizeof(item.singer));
        if (local_dual_match_parts(aaa, bbb, &item)) {
            if (local_push_item(dual_ctx, &item) != 0) {
                closedir(dir);
                free(dual_ctx->items);
                dual_ctx->items = NULL;
                dual_ctx->count = 0;
                dual_ctx->capacity = 0;
                return -1;
            }
        } else if (local_bbb_only_match(bbb, &item)) {
            if (local_push_item(bbb_ctx, &item) != 0) {
                closedir(dir);
                free(bbb_ctx->items);
                bbb_ctx->items = NULL;
                bbb_ctx->count = 0;
                bbb_ctx->capacity = 0;
                return -1;
            }
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
    LocalCollectContext dual_ctx;
    LocalCollectContext bbb_ctx;
    char aaa[256];
    char bbb[256];
    const char *root = local_music_root();
    int ret;
    memset(&ctx, 0, sizeof(ctx));
    memset(&dual_ctx, 0, sizeof(dual_ctx));
    memset(&bbb_ctx, 0, sizeof(bbb_ctx));
    if (page <= 0 || page_size <= 0 || result == NULL) return -1;
    local_result_reset(result);
    if (keyword != NULL && local_try_split_singer_song_keyword(keyword, aaa, sizeof(aaa), bbb, sizeof(bbb))) {
        ret = local_scan_dir_tiered(root, "", aaa, bbb, &dual_ctx, &bbb_ctx);
        if (ret != 0 && errno != ENOENT) {
            free(dual_ctx.items);
            free(bbb_ctx.items);
            return -1;
        }
        if (dual_ctx.count > 0) {
            free(bbb_ctx.items);
            ctx = dual_ctx;
        } else {
            free(dual_ctx.items);
            ctx = bbb_ctx;
        }
    } else {
        ret = local_scan_dir(root, "", keyword, &ctx);
        if (ret != 0 && errno != ENOENT) {
            free(ctx.items);
            return -1;
        }
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
