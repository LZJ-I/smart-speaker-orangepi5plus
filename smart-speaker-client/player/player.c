#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <wait.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/mount.h>
#include <glob.h>

#include "shm.h"
#include "player.h"
#include "link.h"
#include "select.h"
#include "music_lib_bridge.h"
#include "../debug_log.h"

#define TAG "PLAYER"

int g_current_state = PLAY_STATE_STOP;
int g_current_suspend = PLAY_SUSPEND_NO;
int g_current_online_mode = ONLINE_MODE_YES;

int g_asr_fd = -1;
int g_kws_fd = -1;
int g_tts_fd = -1;
int g_player_ctrl_fd = -1;

static player_playlist_ctx_t g_playlist_ctx = {
    .keyword = "热门",
    .current_page = 1,
    .total_pages = 1,
    .page_size = 10
};

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) return;
    dst[0] = '\0';
    if (src == NULL) return;
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static int identity_equal(const Music_Node *a, const Music_Node *b)
{
    if (a == NULL || b == NULL) return 0;
    return (strcmp(a->source, b->source) == 0 && strcmp(a->song_id, b->song_id) == 0);
}

static void update_shm_current_song(Shm_Data *s, const Music_Node *node)
{
    if (s == NULL || node == NULL) return;
    copy_text(s->current_music, sizeof(s->current_music), node->song_name);
    copy_text(s->current_singer, sizeof(s->current_singer), node->singer);
    copy_text(s->current_source, sizeof(s->current_source), node->source);
    copy_text(s->current_song_id, sizeof(s->current_song_id), node->song_id);
}

static int resolve_play_url(const Music_Node *song, char *url, size_t url_size)
{
    if (song == NULL || url == NULL || url_size == 0) return -1;
    url[0] = '\0';
    if (g_current_online_mode == ONLINE_MODE_YES) {
        if (music_lib_get_url_by_source_id(song->source, song->song_id, url, url_size) == 0 && url[0] != '\0') {
            return 0;
        }
        if (song->singer[0] != '\0') {
            char full_name[MUSIC_MAX_NAME + SINGER_MAX_NAME + 2];
            snprintf(full_name, sizeof(full_name), "%s/%s", song->singer, song->song_name);
            if (music_lib_get_url_for_music(full_name, url, url_size) == 0 && url[0] != '\0') {
                return 0;
            }
        }
        if (music_lib_get_url_for_music(song->song_name, url, url_size) == 0 && url[0] != '\0') {
            return 0;
        }
        return -1;
    }
    snprintf(url, url_size, "%s%s", MUSIC_PATH, song->song_name);
    url[url_size - 1] = '\0';
    return 0;
}

static int play_music_node(const Music_Node *song)
{
    char music_path[2048];
    if (song == NULL || song->song_name[0] == '\0') return -1;
    if (resolve_play_url(song, music_path, sizeof(music_path)) != 0 || music_path[0] == '\0') {
        LOGE(TAG, "解析播放地址失败");
        return -1;
    }
    LOGI(TAG, "播放 song_id=%s singer=%s song=%s source=%s",
         song->song_id, song->singer, song->song_name, song->source);
    return run_gst_player(music_path);
}

static int player_write_fifo(const char *cmd)
{
    int fd = open(GST_CMD_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd == -1) {
        return -1;
    }
    size_t len = strlen(cmd);
    int ret = (write(fd, cmd, len) == (ssize_t)len) ? 0 : -1;
    close(fd);
    return ret;
}

static int select_song_from_shm(Music_Node *song)
{
    Shm_Data s;
    if (song == NULL) return -1;
    shm_get(&s);
    if (s.current_source[0] != '\0' && s.current_song_id[0] != '\0' &&
        link_get_music_by_source_id(s.current_source, s.current_song_id, song) == 0) {
        return 0;
    }
    return link_get_first_music(song);
}

void child_quit_process(int sig)
{
    (void)sig;
    g_current_state = PLAY_STATE_STOP;
}

static void child_process(Music_Node *start_song)
{
    Music_Node current_song;
    memset(&current_song, 0, sizeof(current_song));
    if (start_song != NULL) {
        current_song = *start_song;
    }
    signal(SIGUSR1, child_quit_process);
    while (g_current_state == PLAY_STATE_PLAY) {
        pid_t pid = fork();
        if (pid < 0) {
            LOGE(TAG, "创建孙进程失败");
            break;
        } else if (pid == 0) {
            Shm_Data s;
            shm_get(&s);
            if (s.current_source[0] != '\0' && s.current_song_id[0] != '\0') {
                Music_Node shm_song;
                if (link_get_music_by_source_id(s.current_source, s.current_song_id, &shm_song) == 0 &&
                    !identity_equal(&shm_song, &current_song)) {
                    current_song = shm_song;
                }
            }
            s.child_pid = getppid();
            s.grand_pid = getpid();
            update_shm_current_song(&s, &current_song);
            shm_set(&s);
            if (play_music_node(&current_song) != 0) {
                exit(1);
            }
            exit(0);
        } else {
            int status;
            Shm_Data s;
            Music_Node next_song;
            int force_advance = 0;
            waitpid(pid, &status, 0);
            if (g_current_state != PLAY_STATE_PLAY) {
                break;
            }
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                force_advance = 1;
            }
            shm_get(&s);
            if (s.current_source[0] != '\0' && s.current_song_id[0] != '\0') {
                Music_Node target;
                if (link_get_music_by_source_id(s.current_source, s.current_song_id, &target) == 0 &&
                    !identity_equal(&target, &current_song)) {
                    current_song = target;
                    continue;
                } else if (strcmp(s.current_song_id, current_song.song_id) != 0 ||
                           strcmp(s.current_source, current_song.source) != 0) {
                    copy_text(current_song.song_id, sizeof(current_song.song_id), s.current_song_id);
                    copy_text(current_song.source, sizeof(current_song.source), s.current_source);
                    copy_text(current_song.song_name, sizeof(current_song.song_name), s.current_music);
                    copy_text(current_song.singer, sizeof(current_song.singer), s.current_singer);
                    continue;
                }
            }
            memset(&next_song, 0, sizeof(next_song));
            {
                int ret = link_get_next_music(s.current_source, s.current_song_id, s.current_mode, force_advance, &next_song);
                if (ret == 0) {
                    current_song = next_song;
                    continue;
                }
                if (ret == 1) {
                    kill(s.parent_pid, SIGUSR1);
                }
            }
            break;
        }
    }
}

static void player_play_music(const Music_Node *song)
{
    pid_t pid;
    if (song == NULL) return;
    pid = fork();
    if (pid < 0) {
        LOGE(TAG, "创建子进程失败");
        return;
    }
    if (pid == 0) {
        Music_Node local_song = *song;
        child_process(&local_song);
        exit(0);
    }
}

void player_start_play()
{
    Music_Node start_song;
    Shm_Data s;
    shm_get(&s);
    if (g_current_state == PLAY_STATE_PLAY && g_current_suspend == PLAY_SUSPEND_NO) return;
    if (g_current_state == PLAY_STATE_PLAY && g_current_suspend == PLAY_SUSPEND_YES) {
        player_continue_play();
        return;
    }
    memset(&start_song, 0, sizeof(start_song));
    if (select_song_from_shm(&start_song) != 0) {
        LOGW(TAG, "当前无可播放歌曲");
        return;
    }
    update_shm_current_song(&s, &start_song);
    shm_set(&s);
    g_current_state = PLAY_STATE_PLAY;
    g_current_suspend = PLAY_SUSPEND_NO;
    LOGI(TAG, "开始播放 song_id=%s singer=%s song=%s source=%s",
         start_song.song_id, start_song.singer, start_song.song_name, start_song.source);
    player_play_music(&start_song);
}

void player_stop_play(void)
{
    Shm_Data s;
    if (g_current_state == PLAY_STATE_STOP) return;
    LOGI(TAG, "结束播放");
    shm_get(&s);
    if (s.child_pid > 0) kill(s.child_pid, SIGUSR1);
    if (s.grand_pid > 0) kill(s.grand_pid, SIGTERM);
    player_write_fifo("quit\n");
    if (s.child_pid > 0) waitpid(s.child_pid, NULL, 0);
    g_current_state = PLAY_STATE_STOP;
    g_current_suspend = PLAY_SUSPEND_YES;
    s.child_pid = 0;
    s.grand_pid = 0;
    shm_set(&s);
}

void player_continue_play()
{
    if (g_current_state == PLAY_STATE_STOP) {
        player_start_play();
        return;
    }
    if (g_current_suspend == PLAY_SUSPEND_NO) return;
    player_write_fifo("cycle pause\n");
    g_current_state = PLAY_STATE_PLAY;
    g_current_suspend = PLAY_SUSPEND_NO;
    LOGI(TAG, "继续播放");
}

void player_suspend_play()
{
    if (g_current_state == PLAY_STATE_STOP || g_current_suspend == PLAY_SUSPEND_YES) return;
    player_write_fifo("cycle pause\n");
    g_current_state = PLAY_STATE_PLAY;
    g_current_suspend = PLAY_SUSPEND_YES;
    LOGI(TAG, "暂停播放");
}

int player_playlist_load_next_page(void)
{
    int total_pages = 0;
    int filled_count = 0;
    int next_page;
    int tries = 0;
    if (g_playlist_ctx.keyword[0] == '\0') return -1;
    next_page = g_playlist_ctx.current_page + 1;
    if (g_playlist_ctx.total_pages > 0 && next_page > g_playlist_ctx.total_pages) next_page = 1;
    while (tries < (g_playlist_ctx.total_pages > 0 ? g_playlist_ctx.total_pages : 3)) {
        if (music_lib_search_fill_list_page(g_playlist_ctx.keyword, next_page, g_playlist_ctx.page_size,
                                            &total_pages, &filled_count) == 0 &&
            filled_count > 0) {
            Music_Node first_song;
            Shm_Data s;
            g_playlist_ctx.current_page = next_page;
            g_playlist_ctx.total_pages = (total_pages > 0) ? total_pages : 1;
            memset(&first_song, 0, sizeof(first_song));
            if (link_get_first_music(&first_song) == 0) {
                shm_get(&s);
                update_shm_current_song(&s, &first_song);
                shm_set(&s);
            }
            LOGI(TAG, "翻页成功 keyword=%s page=%d/%d count=%d",
                 g_playlist_ctx.keyword, g_playlist_ctx.current_page, g_playlist_ctx.total_pages, filled_count);
            return filled_count;
        }
        next_page++;
        if (total_pages > 0 && next_page > total_pages) next_page = 1;
        tries++;
    }
    return -1;
}

int player_prepare_keyword_playlist(const char *keyword, int auto_start)
{
    int total_pages = 0;
    int filled_count = 0;
    char normalized[sizeof(g_playlist_ctx.keyword)];
    if (keyword == NULL || keyword[0] == '\0') return -1;
    copy_text(normalized, sizeof(normalized), keyword);
    if (strcmp(normalized, g_playlist_ctx.keyword) != 0) {
        copy_text(g_playlist_ctx.keyword, sizeof(g_playlist_ctx.keyword), normalized);
        g_playlist_ctx.current_page = 1;
    } else if (g_playlist_ctx.current_page <= 0) {
        g_playlist_ctx.current_page = 1;
    }
    if (music_lib_search_fill_list_page(g_playlist_ctx.keyword, g_playlist_ctx.current_page, g_playlist_ctx.page_size,
                                        &total_pages, &filled_count) != 0 || filled_count <= 0) {
        return -1;
    }
    g_playlist_ctx.total_pages = (total_pages > 0) ? total_pages : 1;
    if (auto_start) {
        player_stop_play();
        player_start_play();
    }
    return filled_count;
}

int player_search_and_play_keyword(const char *keyword)
{
    return (player_prepare_keyword_playlist(keyword, 1) > 0) ? 0 : -1;
}

int player_search_and_play_hot_random(void)
{
    const char *keyword = "热门";
    int total_pages = 0;
    int filled_count = 0;
    int target_page = 1;
    int pick_index = 0;
    int i = 0;
    Music_Node picked_song;
    Shm_Data s;

    if (music_lib_search_fill_list_page(keyword, 1, g_playlist_ctx.page_size, &total_pages, &filled_count) != 0 || filled_count <= 0) {
        return -1;
    }

    g_playlist_ctx.total_pages = (total_pages > 0) ? total_pages : 1;
    copy_text(g_playlist_ctx.keyword, sizeof(g_playlist_ctx.keyword), keyword);

    if (g_playlist_ctx.total_pages > 1) {
        target_page = (rand() % g_playlist_ctx.total_pages) + 1;
        if (music_lib_search_fill_list_page(keyword, target_page, g_playlist_ctx.page_size, &total_pages, &filled_count) != 0 || filled_count <= 0) {
            return -1;
        }
        g_playlist_ctx.total_pages = (total_pages > 0) ? total_pages : g_playlist_ctx.total_pages;
    }
    g_playlist_ctx.current_page = target_page;

    memset(&picked_song, 0, sizeof(picked_song));
    if (link_get_first_music(&picked_song) != 0) {
        return -1;
    }
    pick_index = rand() % filled_count;
    for (i = 0; i < pick_index; ++i) {
        Music_Node next_song;
        memset(&next_song, 0, sizeof(next_song));
        if (link_get_next_music(picked_song.source, picked_song.song_id, ORDER_PLAY, 1, &next_song) != 0) {
            break;
        }
        picked_song = next_song;
    }

    shm_get(&s);
    if (filled_count > 1 &&
        strcmp(s.current_source, picked_song.source) == 0 &&
        strcmp(s.current_song_id, picked_song.song_id) == 0) {
        Music_Node next_song;
        memset(&next_song, 0, sizeof(next_song));
        if (link_get_next_music(picked_song.source, picked_song.song_id, ORDER_PLAY, 1, &next_song) == 0) {
            picked_song = next_song;
        }
    }
    update_shm_current_song(&s, &picked_song);
    shm_set(&s);

    player_stop_play();
    player_start_play();
    return 0;
}

int player_next_song()
{
    Shm_Data s;
    Music_Node next_song;
    int ret;
    int was_stopped;
    was_stopped = (g_current_state == PLAY_STATE_STOP);
    shm_get(&s);
    memset(&next_song, 0, sizeof(next_song));
    ret = link_get_next_music(s.current_source, s.current_song_id, s.current_mode, 1, &next_song);
    if (ret == 1) {
        if (player_playlist_load_next_page() <= 0 || link_get_first_music(&next_song) != 0) {
            return 1;
        }
    } else if (ret != 0) {
        return -1;
    }
    update_shm_current_song(&s, &next_song);
    shm_set(&s);
    g_current_state = PLAY_STATE_PLAY;
    g_current_suspend = PLAY_SUSPEND_NO;
    if (was_stopped) {
        s.child_pid = 0;
        s.grand_pid = 0;
        shm_set(&s);
        player_start_play();
        return 0;
    }
    if (s.child_pid > 0 && kill(s.child_pid, 0) == 0) {
        if (player_write_fifo("quit\n") != 0) return -1;
        return 0;
    }
    player_start_play();
    return 0;
}

int player_prev_song()
{
    Shm_Data s;
    Music_Node prev_song;
    int ret;
    shm_get(&s);
    memset(&prev_song, 0, sizeof(prev_song));
    ret = link_get_prev_music(s.current_source, s.current_song_id, &prev_song);
    if (ret != 0 && ret != 1) return -1;
    update_shm_current_song(&s, &prev_song);
    shm_set(&s);
    g_current_state = PLAY_STATE_PLAY;
    g_current_suspend = PLAY_SUSPEND_NO;
    if (s.child_pid > 0 && kill(s.child_pid, 0) == 0) {
        if (player_write_fifo("quit\n") != 0) return -1;
        return 0;
    }
    player_start_play();
    return 0;
}

void player_set_mode(int mode)
{
    Shm_Data s;
    if (mode != SINGLE_PLAY && mode != ORDER_PLAY) return;
    shm_get(&s);
    s.current_mode = mode;
    shm_set(&s);
    if (mode == SINGLE_PLAY) LOGI(TAG, "单曲循环");
    if (mode == ORDER_PLAY) LOGI(TAG, "顺序播放");
}

void player_get_playlist_ctx(player_playlist_ctx_t *out_ctx)
{
    if (out_ctx == NULL) return;
    *out_ctx = g_playlist_ctx;
}

void player_handle_playlist_eof(int sig)
{
    (void)sig;
    g_current_state = PLAY_STATE_STOP;
    g_current_suspend = PLAY_SUSPEND_YES;
    if (player_playlist_load_next_page() > 0) {
        player_start_play();
    }
}

void player_singer_play(const char *singer)
{
    (void)singer;
    player_stop_play();
    player_start_play();
}

int player_play_url(const char *url)
{
    char cmd[4096];
    if (url == NULL || url[0] == '\0') return -1;
    if (g_current_state == PLAY_STATE_STOP) {
        player_start_play();
        usleep(150000);
    }
    snprintf(cmd, sizeof(cmd), "loadfile '%s'\n", url);
    if (player_write_fifo(cmd) != 0) return -1;
    g_current_state = PLAY_STATE_PLAY;
    g_current_suspend = PLAY_SUSPEND_NO;
    return 0;
}

static void asr_kws_switch_offline_mode(void)
{
    int fd = open(ASR_CTRL_PIPE_PATH, O_WRONLY | O_NONBLOCK);
    if (fd == -1) return;
    write(fd, "mode:offline\n", 13);
    close(fd);
}

static int detect_storage_device(char *out, size_t out_size)
{
    glob_t g;
    memset(&g, 0, sizeof(g));
    if (glob("/dev/mmcblk*p*", 0, NULL, &g) == 0 && g.gl_pathc > 0) {
        snprintf(out, out_size, "%s", g.gl_pathv[0]);
        globfree(&g);
        return 0;
    }
    globfree(&g);
    if (access(UDISK_PATH_YES1, F_OK) == 0) { snprintf(out, out_size, "%s", UDISK_PATH_YES1); return 0; }
    if (access(UDISK_PATH_YES2, F_OK) == 0) { snprintf(out, out_size, "%s", UDISK_PATH_YES2); return 0; }
    if (access(UDISK_PATH_YES3, F_OK) == 0) { snprintf(out, out_size, "%s", UDISK_PATH_YES3); return 0; }
    return -1;
}

static int unmount_storage_path(const char *mount_path)
{
    if (umount(mount_path) == 0) return 0;
    if (errno == EINVAL || errno == ENOENT) return 0;
    return -1;
}

int player_switch_offline_mode(void)
{
    char udisk_name[128] = {0};
    Music_Node first_song;
    Shm_Data s;
    if (g_current_online_mode == ONLINE_MODE_NO) {
        tts_play_text("当前为离线模式，无需切换");
        return 0;
    }
    player_stop_play();
    if (detect_storage_device(udisk_name, sizeof(udisk_name)) != 0) {
        tts_play_text("请先插入存储设备");
        return 1;
    }
    player_write_fifo("quit\n");
    sleep(1);
    if (unmount_storage_path(SDCARD_MOUNT_PATH) != 0) {
        tts_play_text("卸载存储设备失败");
        return -1;
    }
    if (access(SDCARD_MOUNT_PATH, F_OK) != 0 && mkdir(SDCARD_MOUNT_PATH, 0777) != 0) {
        tts_play_text("创建挂载存储设备目录失败");
        return -1;
    }
    if (mount(udisk_name, SDCARD_MOUNT_PATH, "exfat", 0, NULL) != 0 &&
        mount(udisk_name, SDCARD_MOUNT_PATH, NULL, 0, NULL) != 0) {
        tts_play_text("创建挂载存储设备失败");
        return -1;
    }
    if (link_read_udisk_music() != 0) {
        tts_play_text("切换离线模式失败，无法读取存储设备歌曲");
        return -1;
    }
    if (link_get_first_music(&first_song) == 0) {
        shm_get(&s);
        update_shm_current_song(&s, &first_song);
        shm_set(&s);
    }
    asr_kws_switch_offline_mode();
    g_current_online_mode = ONLINE_MODE_NO;
    g_current_state = PLAY_STATE_STOP;
    g_current_suspend = PLAY_SUSPEND_YES;
    tts_play_text("已切换到离线模式");
    return 0;
}

static void asr_kws_switch_online_mode(void)
{
    int fd = open(ASR_CTRL_PIPE_PATH, O_WRONLY | O_NONBLOCK);
    if (fd == -1) return;
    write(fd, "mode:online\n", 12);
    close(fd);
}

int player_switch_online_mode(void)
{
    if (g_current_online_mode == ONLINE_MODE_YES) {
        tts_play_text("当前为在线模式，无需切换");
        return 0;
    }
    player_stop_play();
    player_write_fifo("quit\n");
    sleep(1);
    unmount_storage_path(SDCARD_MOUNT_PATH);
    asr_kws_switch_online_mode();
    g_current_online_mode = ONLINE_MODE_YES;
    g_current_state = PLAY_STATE_STOP;
    g_current_suspend = PLAY_SUSPEND_YES;
    tts_play_text("已切换到在线模式");
    return 0;
}
