#include "runtime_config.h"

#include "player_constants.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
    char server_ip[64];
    int server_port;
    char local_music_root[256];
    int startup_volume;
    char player_mode[32];
    char gst_alsa_device[128];
    char music_search_source[16];
    int loaded;
} PlayerRuntimeConfig;

/* main.c 会 chdir 到含 fifo/ 的客户端根；此时配置应在 ./data。若 CWD 仍在 player/（如从 build/bin 启动），则为 ../data */
static const char *client_config_path(void)
{
    if (access("./fifo/asr_fifo", F_OK) == 0) {
        return "./data/config/client.toml";
    }
    return "../data/config/client.toml";
}

static const char *client_data_dir(void)
{
    if (access("./fifo/asr_fifo", F_OK) == 0) {
        return "./data";
    }
    return "../data";
}

static const char *client_config_dir(void)
{
    if (access("./fifo/asr_fifo", F_OK) == 0) {
        return "./data/config";
    }
    return "../data/config";
}

static PlayerRuntimeConfig g_runtime_config = {
    .server_ip = SERVER_IP,
    .server_port = SERVER_PORT,
    .local_music_root = SDCARD_MOUNT_PATH,
    .startup_volume = DEFAULT_VOLUME,
    .player_mode = "auto",
    .gst_alsa_device = GST_ALSA_DEVICE,
    .music_search_source = "auto",
    .loaded = 0,
};

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) return;
    dst[0] = '\0';
    if (src == NULL) return;
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void trim_text(char *text)
{
    char *start = text;
    size_t len;
    if (text == NULL || text[0] == '\0') return;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }
    len = strlen(text);
    while (len > 0 &&
           (text[len - 1] == ' ' || text[len - 1] == '\t' || text[len - 1] == '\r' || text[len - 1] == '\n')) {
        text[--len] = '\0';
    }
}

static void unquote_text(char *text)
{
    size_t len;
    if (text == NULL) return;
    trim_text(text);
    len = strlen(text);
    if (len >= 2 && text[0] == '"' && text[len - 1] == '"') {
        memmove(text, text + 1, len - 2);
        text[len - 2] = '\0';
    }
}

static int parse_int_in_range(const char *text, int min_value, int max_value, int *out_value)
{
    char *end = NULL;
    long parsed;
    if (text == NULL || out_value == NULL) return -1;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text) return -1;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
        end++;
    }
    if (*end != '\0' || parsed < min_value || parsed > max_value) return -1;
    *out_value = (int)parsed;
    return 0;
}

static int player_mode_valid(const char *mode)
{
    if (mode == NULL || mode[0] == '\0') return 0;
    return strcasecmp(mode, "auto") == 0 ||
           strcasecmp(mode, "online") == 0 ||
           strcasecmp(mode, "offline") == 0;
}

/* 与服务端 music-lib search 一致：单源；auto 顺序优先；all 多源并发 */
static int music_search_source_apply(const char *value, char *dst, size_t dst_size)
{
    if (value == NULL || value[0] == '\0' || dst == NULL || dst_size == 0) {
        return -1;
    }
    if (strcasecmp(value, "tx") == 0) {
        copy_text(dst, dst_size, "tx");
        return 0;
    }
    if (strcasecmp(value, "wy") == 0) {
        copy_text(dst, dst_size, "wy");
        return 0;
    }
    if (strcasecmp(value, "kw") == 0) {
        copy_text(dst, dst_size, "kw");
        return 0;
    }
    if (strcasecmp(value, "kg") == 0) {
        copy_text(dst, dst_size, "kg");
        return 0;
    }
    if (strcasecmp(value, "mg") == 0) {
        copy_text(dst, dst_size, "mg");
        return 0;
    }
    if (strcasecmp(value, "auto") == 0) {
        copy_text(dst, dst_size, "auto");
        return 0;
    }
    if (strcasecmp(value, "all") == 0) {
        copy_text(dst, dst_size, "all");
        return 0;
    }
    return -1;
}

static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static void write_default_client_config_if_missing(void)
{
    FILE *fp;
    struct stat st;
    if (stat(client_config_path(), &st) == 0) {
        return;
    }
    if (ensure_dir(client_data_dir()) != 0) return;
    if (ensure_dir(client_config_dir()) != 0) return;

    fp = fopen(client_config_path(), "w");
    if (fp == NULL) {
        return;
    }
    fprintf(fp,
            "# 客户端连接服务端地址\n"
            "server_ip = \"%s\"\n"
            "server_port = %d\n"
            "\n"
            "# 本地曲库根目录\n"
            "local_music_root = \"%s\"\n"
            "\n"
            "# 启动音量 0～100\n"
            "startup_volume = %d\n"
            "# offline 启动即离线不连服务端；auto / online 先尝试 TCP，失败再降级本地（二者当前等价）\n"
            "player_mode = \"%s\"\n"
            "# GStreamer alsasink 的 device，与 gst-inspect-1.0 alsasink 一致，例 dmix: / plughw:\n"
            "gst_alsa_device = \"%s\"\n"
            "\n"
            "# 在线搜歌/歌单默认 source（语音未指定平台时）；可选：\n"
            "# tx/wy/kw/kg/mg 单源；auto 顺序（单次 HTTP 3s、全程≤10s）；all 并发（单次 HTTP 3s）\n"
            "music_search_source = \"auto\"\n",
            SERVER_IP, SERVER_PORT, SDCARD_MOUNT_PATH,
            DEFAULT_VOLUME, "auto", GST_ALSA_DEVICE);
    fclose(fp);
}

static void load_client_runtime_config(void)
{
    FILE *fp;
    char line[512];
    write_default_client_config_if_missing();
    fp = fopen(client_config_path(), "r");
    if (fp == NULL) {
        g_runtime_config.loaded = 1;
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *eq;
        char key[128];
        char value[384];
        line[sizeof(line) - 1] = '\0';
        trim_text(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        strncpy(key, line, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';
        strncpy(value, eq + 1, sizeof(value) - 1);
        value[sizeof(value) - 1] = '\0';
        trim_text(key);
        trim_text(value);

        if (strcmp(key, "server_ip") == 0) {
            unquote_text(value);
            if (value[0] != '\0') {
                copy_text(g_runtime_config.server_ip, sizeof(g_runtime_config.server_ip), value);
            }
        } else if (strcmp(key, "server_port") == 0) {
            int port;
            if (parse_int_in_range(value, 1, 65535, &port) == 0) {
                g_runtime_config.server_port = port;
            }
        } else if (strcmp(key, "local_music_root") == 0) {
            unquote_text(value);
            if (value[0] != '\0') {
                copy_text(g_runtime_config.local_music_root, sizeof(g_runtime_config.local_music_root), value);
            }
        } else if (strcmp(key, "startup_volume") == 0) {
            int startup_volume;
            if (parse_int_in_range(value, 0, 100, &startup_volume) == 0) {
                g_runtime_config.startup_volume = startup_volume;
            }
        } else if (strcmp(key, "player_mode") == 0) {
            unquote_text(value);
            if (player_mode_valid(value)) {
                copy_text(g_runtime_config.player_mode, sizeof(g_runtime_config.player_mode), value);
            }
        } else if (strcmp(key, "gst_alsa_device") == 0) {
            unquote_text(value);
            if (value[0] != '\0') {
                copy_text(g_runtime_config.gst_alsa_device, sizeof(g_runtime_config.gst_alsa_device), value);
            }
        } else if (strcmp(key, "music_search_source") == 0) {
            unquote_text(value);
            if (music_search_source_apply(value, g_runtime_config.music_search_source,
                                          sizeof(g_runtime_config.music_search_source)) != 0) {
                copy_text(g_runtime_config.music_search_source, sizeof(g_runtime_config.music_search_source), "auto");
            }
        }
    }
    fclose(fp);
    g_runtime_config.loaded = 1;
}

static void ensure_loaded(void)
{
    if (!g_runtime_config.loaded) {
        load_client_runtime_config();
    }
}

const char *player_runtime_server_ip(void)
{
    ensure_loaded();
    return g_runtime_config.server_ip;
}

int player_runtime_server_port(void)
{
    ensure_loaded();
    return g_runtime_config.server_port;
}

const char *player_runtime_local_music_root(void)
{
    ensure_loaded();
    return g_runtime_config.local_music_root;
}

int player_runtime_startup_volume(void)
{
    ensure_loaded();
    return g_runtime_config.startup_volume;
}

const char *player_runtime_player_mode(void)
{
    ensure_loaded();
    return g_runtime_config.player_mode;
}

const char *player_runtime_gst_alsa_device(void)
{
    ensure_loaded();
    return g_runtime_config.gst_alsa_device;
}

const char *player_runtime_music_search_source(void)
{
    ensure_loaded();
    return g_runtime_config.music_search_source;
}
