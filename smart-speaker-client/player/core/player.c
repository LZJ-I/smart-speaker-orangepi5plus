#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
#include <limits.h>

#include "shm.h"
#include "player.h"
#include "link.h"
#include "select.h"
#include "music_lib_bridge.h"
#include "music_source.h"
#include "socket.h"
#include "debug_log.h"

#define TAG "PLAYER"

volatile sig_atomic_t g_current_state = PLAY_STATE_STOP;
volatile sig_atomic_t g_current_suspend = PLAY_SUSPEND_NO;
volatile sig_atomic_t g_player_shutdown_requested = 0;
volatile sig_atomic_t g_audio_focus_state = AUDIO_FOCUS_IDLE;
int g_current_online_mode = ONLINE_MODE_YES;
static int g_env_forces_offline = 0;

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

static volatile sig_atomic_t g_playlist_eof_flag = 0;
static volatile sig_atomic_t g_child_exit_flag = 0;
static volatile sig_atomic_t g_eof_autostart_suppressed = 0;
static int g_gst_cmd_fifo_fd = -1;
static int player_write_fifo(const char *cmd);
static void asr_kws_switch_offline_mode(void);

void player_set_audio_focus(int focus_state)
{
    g_audio_focus_state = focus_state;
}

int player_get_audio_focus(void)
{
    return (int)g_audio_focus_state;
}

int player_audio_focus_should_resume(void)
{
    return g_audio_focus_state == AUDIO_FOCUS_MUSIC_PAUSED_FOR_TTS ||
           g_audio_focus_state == AUDIO_FOCUS_MUSIC_RESUMING;
}

void player_audio_focus_prepare_resume(void)
{
    if (player_audio_focus_should_resume()) {
        g_audio_focus_state = AUDIO_FOCUS_MUSIC_RESUMING;
    }
}

void player_audio_focus_cancel_resume(void)
{
    if (player_audio_focus_should_resume()) {
        g_audio_focus_state = AUDIO_FOCUS_MUSIC_PAUSED_MANUAL;
    } else if (g_audio_focus_state == AUDIO_FOCUS_TTS_PLAYING) {
        g_audio_focus_state = AUDIO_FOCUS_IDLE;
    }
}

void player_audio_focus_mark_tts_standalone(void)
{
    if (g_audio_focus_state == AUDIO_FOCUS_IDLE) {
        g_audio_focus_state = AUDIO_FOCUS_TTS_PLAYING;
    }
}

static int pid_is_alive(pid_t pid)
{
    return (pid > 0 && kill(pid, 0) == 0);
}

static int wait_pid_exit(pid_t pid, int timeout_ms, int *status)
{
    int elapsed = 0;
    if (pid <= 0) return 0;
    while (1) {
        pid_t ret = waitpid(pid, status, WNOHANG);
        if (ret == pid) return 1;
        if (ret == 0) {
            if (timeout_ms >= 0 && elapsed >= timeout_ms) return 0;
            usleep(50 * 1000);
            elapsed += 50;
            continue;
        }
        if (ret < 0) {
            if (errno == EINTR) {
                if (g_current_state != PLAY_STATE_PLAY) return 0;
                continue;
            }
            return 0;
        }
    }
}

static void stop_active_grandchild(int force_kill)
{
    Shm_Data s;
    shm_get(&s);
    if (pid_is_alive(s.grand_pid)) {
        (void)kill(s.grand_pid, SIGCONT);
    }
    (void)player_write_fifo("quit\n");
    usleep(150 * 1000);
    if (pid_is_alive(s.grand_pid)) {
        kill(s.grand_pid, SIGTERM);
        usleep(150 * 1000);
    }
    if (pid_is_alive(s.grand_pid) && force_kill) {
        kill(s.grand_pid, SIGKILL);
    }
}

// 请求子进程退出，如果子进程没有退出，则发送SIGTERM信号，如果子进程没有退出，则发送SIGKILL信号。
static int request_active_grandchild_quit(int timeout_ms)
{
    Shm_Data s;
    int elapsed = 0;

    shm_get(&s);
    if (!pid_is_alive(s.grand_pid)) {
        return -1;
    }

    (void)kill(s.grand_pid, SIGCONT);
    (void)player_write_fifo("quit\n");
    while (pid_is_alive(s.grand_pid) && elapsed < timeout_ms) {
        usleep(50 * 1000);
        elapsed += 50;
    }
    if (pid_is_alive(s.grand_pid)) {
        kill(s.grand_pid, SIGTERM);
    }
    return 0;
}

static void force_stop_playback_processes(void)
{
    Shm_Data s;
    int retry;

    for (retry = 0; retry < 3; ++retry) {
        pid_t cpid;
        pid_t gpid;

        shm_get(&s);
        cpid = s.child_pid;
        gpid = s.grand_pid;

        if (!pid_is_alive(cpid) && !pid_is_alive(gpid)) {
            break;
        }

        if (pid_is_alive(cpid)) {
            kill(cpid, SIGUSR1);
        }

        stop_active_grandchild(1);

        if (pid_is_alive(cpid)) {
            if (!wait_pid_exit(cpid, 500, NULL)) {
                kill(cpid, SIGTERM);
                if (!wait_pid_exit(cpid, 500, NULL)) {
                    kill(cpid, SIGKILL);
                    wait_pid_exit(cpid, 500, NULL);
                }
            }
        }

        if (pid_is_alive(gpid)) {
            kill(gpid, SIGKILL);
            usleep(100 * 1000);
        }
    }

    shm_get(&s);
    s.child_pid = 0;
    s.grand_pid = 0;
    shm_set(&s);
}

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
    copy_text(s->current_song_id, sizeof(s->current_song_id), node->song_id);
}

static int resolve_play_url(const Music_Node *song, char *url, size_t url_size)
{
    if (song == NULL || url == NULL || url_size == 0) return -1;
    url[0] = '\0';
    if (song->play_url[0] != '\0') {
        copy_text(url, url_size, song->play_url);
        return (url[0] != '\0') ? 0 : -1;
    }
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
    snprintf(url, url_size, "%s%s", MUSIC_PATH, song->song_name);
    url[url_size - 1] = '\0';
    return (url[0] != '\0') ? 0 : -1;
}

static int play_music_node(const Music_Node *song)
{
    char music_path[2048];
    if (song == NULL || song->song_name[0] == '\0') return -1;
    if (resolve_play_url(song, music_path, sizeof(music_path)) != 0 || music_path[0] == '\0') {
        LOGE(TAG, "解析播放地址失败");
        return -1;
    }
    LOGI(TAG, "播放 singer=%s song=%s",
         song->singer, song->song_name);
    return run_gst_player(music_path);
}

static void player_pause_current_output(void)
{
    Shm_Data s;
    shm_get(&s);
    if (pid_is_alive(s.grand_pid)) {
        kill(s.grand_pid, SIGSTOP);
    } else {
        (void)player_write_fifo("cycle pause\n");
    }
    g_current_state = PLAY_STATE_PLAY;
    g_current_suspend = PLAY_SUSPEND_YES;
}

static int player_ensure_gst_cmd_fifo_wr(void)
{
    if (g_gst_cmd_fifo_fd >= 0) {
        return 0;
    }
    g_gst_cmd_fifo_fd = open(GST_CMD_FIFO, O_WRONLY | O_NONBLOCK);
    return (g_gst_cmd_fifo_fd >= 0) ? 0 : -1;
}

static int player_write_fifo(const char *cmd)
{
    size_t len;
    ssize_t w;

    if (cmd == NULL) {
        return -1;
    }
    if (player_ensure_gst_cmd_fifo_wr() != 0) {
        return -1;
    }
    len = strlen(cmd);
    w = write(g_gst_cmd_fifo_fd, cmd, len);
    if (w == (ssize_t)len) {
        return 0;
    }
    if (w < 0 && (errno == ENXIO || errno == EPIPE || errno == EBADF)) {
        if (g_gst_cmd_fifo_fd >= 0) {
            close(g_gst_cmd_fifo_fd);
            g_gst_cmd_fifo_fd = -1;
        }
        if (player_ensure_gst_cmd_fifo_wr() != 0) {
            return -1;
        }
        w = write(g_gst_cmd_fifo_fd, cmd, len);
        return (w == (ssize_t)len) ? 0 : -1;
    }
    return -1;
}

void player_cmd_fifo_close(void)
{
    if (g_gst_cmd_fifo_fd >= 0) {
        close(g_gst_cmd_fifo_fd);
        g_gst_cmd_fifo_fd = -1;
    }
}

static int select_song_from_shm(Music_Node *song)
{
    Shm_Data s;
    if (song == NULL) return -1;
    shm_get(&s);
    if (s.current_song_id[0] != '\0' &&
        link_get_music_by_source_id(NULL, s.current_song_id, song) == 0) {
        return 0;
    }
    return link_get_first_music(song);
}

void child_quit_process(int sig)
{
    (void)sig;
    g_current_state = PLAY_STATE_STOP;
}

void player_handle_sigchld(int sig)
{
    (void)sig;
    g_child_exit_flag = 1;
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
            if (s.current_song_id[0] != '\0') {
                Music_Node shm_song;
                if (link_get_music_by_source_id(NULL, s.current_song_id, &shm_song) == 0 &&
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
            while (!wait_pid_exit(pid, 100, &status)) {
                if (g_current_state != PLAY_STATE_PLAY) {
                    if (pid_is_alive(pid)) kill(pid, SIGTERM);
                    wait_pid_exit(pid, 500, &status);
                    break;
                }
            }
            if (g_current_state != PLAY_STATE_PLAY) {
                break;
            }
            if (!WIFEXITED(status) && !WIFSIGNALED(status)) {
                if (g_current_state != PLAY_STATE_PLAY) {
                    break;
                }
                force_advance = 1;
            }
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                force_advance = 1;
            }
            shm_get(&s);
            if (s.current_song_id[0] != '\0') {
                Music_Node target;
                if (link_get_music_by_source_id(NULL, s.current_song_id, &target) == 0 &&
                    !identity_equal(&target, &current_song)) {
                    current_song = target;
                    continue;
                } else if (strcmp(s.current_song_id, current_song.song_id) != 0) {
                    copy_text(current_song.song_id, sizeof(current_song.song_id), s.current_song_id);
                    copy_text(current_song.song_name, sizeof(current_song.song_name), s.current_music);
                    copy_text(current_song.singer, sizeof(current_song.singer), s.current_singer);
                    current_song.source[0] = '\0';
                    continue;
                }
            }
            memset(&next_song, 0, sizeof(next_song));
            {
                int ret = link_get_next_music(NULL, s.current_song_id, s.current_mode, force_advance, &next_song);
                if (ret == 0) {
                    current_song = next_song;
                    update_shm_current_song(&s, &current_song);
                    shm_set(&s);
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
    Shm_Data s;
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
    shm_get(&s);
    s.child_pid = pid;
    s.grand_pid = 0;
    shm_set(&s);
}

void player_start_play()
{
    Music_Node start_song;
    Shm_Data s;
    const char *keyword;
    g_eof_autostart_suppressed = 0;
    shm_get(&s);
    if (g_current_state == PLAY_STATE_PLAY && g_current_suspend == PLAY_SUSPEND_NO && pid_is_alive(s.child_pid)) {
        return;
    }
    if (g_current_state == PLAY_STATE_PLAY && g_current_suspend == PLAY_SUSPEND_YES) {
        player_continue_play();
        return;
    }
    memset(&start_song, 0, sizeof(start_song));
    if (select_song_from_shm(&start_song) != 0) {
        keyword = (g_playlist_ctx.keyword[0] != '\0') ? g_playlist_ctx.keyword : "热门";
        if (player_prepare_keyword_playlist(keyword, 0) <= 0 || select_song_from_shm(&start_song) != 0) {
            LOGW(TAG, "当前无可播放歌曲");
            return;
        }
    }
    update_shm_current_song(&s, &start_song);
    shm_set(&s);
    g_current_state = PLAY_STATE_PLAY;
    g_current_suspend = PLAY_SUSPEND_NO;
    player_set_audio_focus(AUDIO_FOCUS_MUSIC_PLAYING);
    LOGI(TAG, "开始播放 singer=%s song=%s",
         start_song.singer, start_song.song_name);
    player_play_music(&start_song);
}

void player_stop_play(void)
{
    Shm_Data s;
    pid_t cpid;
    if (g_current_state == PLAY_STATE_STOP) return;
    LOGI(TAG, "结束播放");
    select_on_player_stopped();
    g_eof_autostart_suppressed = 1;
    shm_get(&s);
    cpid = s.child_pid;
    if (pid_is_alive(cpid)) {
        kill(cpid, SIGUSR1);
    }
    stop_active_grandchild(1);
    if (pid_is_alive(cpid)) {
        if (!wait_pid_exit(cpid, 500, NULL)) {
            kill(cpid, SIGTERM);
            if (!wait_pid_exit(cpid, 500, NULL)) {
                kill(cpid, SIGKILL);
                wait_pid_exit(cpid, 500, NULL);
            }
        }
    }
    g_current_state = PLAY_STATE_STOP;
    g_current_suspend = PLAY_SUSPEND_YES;
    player_set_audio_focus(AUDIO_FOCUS_IDLE);
    s.child_pid = 0;
    s.grand_pid = 0;
    shm_set(&s);
}

void player_continue_play()
{
    Shm_Data s;
    if (g_current_state == PLAY_STATE_STOP) {
        player_start_play();
        return;
    }
    shm_get(&s);
    if (g_current_suspend == PLAY_SUSPEND_YES) {
        if (pid_is_alive(s.grand_pid)) {
            kill(s.grand_pid, SIGCONT);
        } else {
            (void)player_write_fifo("cycle pause\n");
        }
        g_current_state = PLAY_STATE_PLAY;
        g_current_suspend = PLAY_SUSPEND_NO;
        player_set_audio_focus(AUDIO_FOCUS_MUSIC_PLAYING);
        if (!pid_is_alive(s.grand_pid) && !pid_is_alive(s.child_pid)) {
            player_start_play();
            return;
        }
        LOGI(TAG, "继续播放");
        return;
    }
    if (!pid_is_alive(s.child_pid)) {
        player_start_play();
    }
}

void player_suspend_play()
{
    if (g_current_state == PLAY_STATE_STOP || g_current_suspend == PLAY_SUSPEND_YES) return;
    player_pause_current_output();
    player_set_audio_focus(AUDIO_FOCUS_MUSIC_PAUSED_MANUAL);
    LOGI(TAG, "暂停播放");
}

void player_suspend_for_tts(void)
{
    if (g_current_state == PLAY_STATE_STOP || g_current_suspend == PLAY_SUSPEND_YES) return;
    player_pause_current_output();
    player_set_audio_focus(AUDIO_FOCUS_MUSIC_PAUSED_FOR_TTS);
    LOGI(TAG, "暂停播放");
}

int player_playlist_load_next_page(void)
{
    int total_pages = 0;
    int filled_count = 0;
    int next_page;
    int tries = 0;
    const char *search_kw;
    if (g_current_online_mode == ONLINE_MODE_YES) {
        search_kw = "";
    } else {
        if (g_playlist_ctx.keyword[0] == '\0') return -1;
        search_kw = g_playlist_ctx.keyword;
    }
    next_page = g_playlist_ctx.current_page + 1;
    if (g_playlist_ctx.total_pages > 0 && next_page > g_playlist_ctx.total_pages) next_page = 1;
    while (tries < (g_playlist_ctx.total_pages > 0 ? g_playlist_ctx.total_pages : 3)) {
        if (music_lib_search_fill_list_page(search_kw, next_page, g_playlist_ctx.page_size,
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
            LOGI(TAG, "翻页成功 page=%d/%d count=%d",
                 g_playlist_ctx.current_page, g_playlist_ctx.total_pages, filled_count);
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

int player_search_insert_keyword_and_play(const char *keyword)
{
    Shm_Data s;
    Music_Node anchor;
    Music_Node next_song;
    int n = 0;

    if (keyword == NULL || keyword[0] == '\0') {
        return -1;
    }
    if (music_lib_insert_search_after_current(keyword, 1, g_playlist_ctx.page_size, &n) != 0 || n <= 0) {
        return -1;
    }
    copy_text(g_playlist_ctx.keyword, sizeof(g_playlist_ctx.keyword), keyword);
    g_playlist_ctx.current_page = 1;

    memset(&next_song, 0, sizeof(next_song));
    shm_get(&s);
    if (s.current_song_id[0] != '\0' && link_get_music_by_source_id(NULL, s.current_song_id, &anchor) == 0) {
        if (link_get_next_music(anchor.source, anchor.song_id, ORDER_PLAY, 1, &next_song) != 0) {
            if (link_get_first_music(&next_song) != 0) {
                return -1;
            }
        }
    } else {
        if (link_get_first_music(&next_song) != 0) {
            return -1;
        }
    }
    update_shm_current_song(&s, &next_song);
    shm_set(&s);

    shm_get(&s);
    if (pid_is_alive(s.child_pid)) {
        g_current_state = PLAY_STATE_PLAY;
        g_current_suspend = PLAY_SUSPEND_NO;
        player_set_audio_focus(AUDIO_FOCUS_MUSIC_PLAYING);
        stop_active_grandchild(0);
    } else {
        player_start_play();
    }
    return 0;
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

int player_simulate_song_finished(void)
{
    Shm_Data s;
    shm_get(&s);
    if (g_current_state != PLAY_STATE_PLAY || g_current_suspend == PLAY_SUSPEND_YES) {
        return -1;
    }
    if (!pid_is_alive(s.child_pid) || !pid_is_alive(s.grand_pid)) {
        return -1;
    }
    LOGI(TAG, "模拟当前歌曲播放完成");
    return request_active_grandchild_quit(800);
}

int player_next_song()
{
    Shm_Data s;
    Music_Node next_song;
    int ret;
    int was_stopped;
    const char *keyword;
    was_stopped = (g_current_state == PLAY_STATE_STOP);
    shm_get(&s);
    memset(&next_song, 0, sizeof(next_song));
    ret = link_get_next_music(NULL, s.current_song_id, s.current_mode, 1, &next_song);
    if (ret == -1) {
        if (g_current_online_mode == ONLINE_MODE_NO) {
            if (link_get_first_music(&next_song) != 0) {
                return -1;
            }
            ret = 0;
        } else {
            keyword = (g_playlist_ctx.keyword[0] != '\0') ? g_playlist_ctx.keyword : "热门";
            if (player_prepare_keyword_playlist(keyword, 0) <= 0) {
                return -1;
            }
            if (s.current_song_id[0] == '\0') {
                if (link_get_first_music(&next_song) != 0) {
                    return -1;
                }
                ret = 0;
            } else {
                ret = link_get_next_music(NULL, s.current_song_id, s.current_mode, 1, &next_song);
                if (ret == -1 && link_get_first_music(&next_song) == 0) {
                    ret = 0;
                }
            }
        }
    }
    if (ret == 1) {
        if (g_current_online_mode == ONLINE_MODE_NO) {
            if (s.current_mode == SINGLE_PLAY) {
                if (link_get_next_music(NULL, s.current_song_id, SINGLE_PLAY, 0, &next_song) != 0) {
                    return 1;
                }
            } else if (link_get_first_music(&next_song) != 0) {
                return 1;
            }
        } else if (player_playlist_load_next_page() <= 0 || link_get_first_music(&next_song) != 0) {
            return 1;
        }
    } else if (ret != 0) {
        return -1;
    }
    update_shm_current_song(&s, &next_song);
    shm_set(&s);
    if (was_stopped) {
        s.child_pid = 0;
        s.grand_pid = 0;
        shm_set(&s);
        player_start_play();
        return 0;
    }
    shm_get(&s);
    if (pid_is_alive(s.child_pid)) {
        g_current_state = PLAY_STATE_PLAY;
        g_current_suspend = PLAY_SUSPEND_NO;
        player_set_audio_focus(AUDIO_FOCUS_MUSIC_PLAYING);
        stop_active_grandchild(0);
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
    int was_stopped;
    was_stopped = (g_current_state == PLAY_STATE_STOP);
    shm_get(&s);
    memset(&prev_song, 0, sizeof(prev_song));
    ret = link_get_prev_music(NULL, s.current_song_id,
                              (g_current_online_mode == ONLINE_MODE_NO), &prev_song);
    if (ret != 0 && ret != 1) return -1;
    update_shm_current_song(&s, &prev_song);
    shm_set(&s);
    if (was_stopped) {
        s.child_pid = 0;
        s.grand_pid = 0;
        shm_set(&s);
        player_start_play();
        return 0;
    }
    shm_get(&s);
    if (pid_is_alive(s.child_pid)) {
        g_current_state = PLAY_STATE_PLAY;
        g_current_suspend = PLAY_SUSPEND_NO;
        player_set_audio_focus(AUDIO_FOCUS_MUSIC_PLAYING);
        stop_active_grandchild(0);
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

static void player_eof_wrap_or_restart(void)
{
    Shm_Data s;
    Music_Node next_song;
    int adv;
    shm_get(&s);
    if (s.current_mode == SINGLE_PLAY) {
        player_start_play();
        return;
    }
    memset(&next_song, 0, sizeof(next_song));
    adv = link_get_next_music(NULL, s.current_song_id, s.current_mode, 1, &next_song);
    if (adv == 1) {
        if (link_get_first_music(&next_song) != 0) {
            return;
        }
    } else if (adv != 0) {
        return;
    }
    update_shm_current_song(&s, &next_song);
    shm_set(&s);
    player_start_play();
}

void player_handle_playlist_eof(int sig)
{
    (void)sig;
    g_current_state = PLAY_STATE_STOP;
    g_current_suspend = PLAY_SUSPEND_YES;
    player_set_audio_focus(AUDIO_FOCUS_IDLE);
    g_playlist_eof_flag = 1;
}

void player_process_async_events(void)
{
    if (g_child_exit_flag) {
        while (waitpid(-1, NULL, WNOHANG) > 0) {
        }
        g_child_exit_flag = 0;
    }
    if (g_playlist_eof_flag) {
        g_playlist_eof_flag = 0;
        if (g_eof_autostart_suppressed) {
            g_eof_autostart_suppressed = 0;
            return;
        }
        if (g_current_online_mode == ONLINE_MODE_NO) {
            player_eof_wrap_or_restart();
        } else if (player_playlist_load_next_page() > 0) {
            player_start_play();
        } else {
            player_eof_wrap_or_restart();
        }
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
    player_set_audio_focus(AUDIO_FOCUS_MUSIC_PLAYING);
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

static int path_is_mounted_at(const char *path)
{
    FILE *f;
    char line[1024];
    char norm[PATH_MAX];
    char mntpt[PATH_MAX];
    const char *cmp = path;
    size_t n;
    size_t ml;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    if (realpath(path, norm) != NULL) {
        cmp = norm;
    }
    n = strlen(cmp);
    while (n > 1 && cmp[n - 1] == '/') {
        n--;
    }
    f = fopen("/proc/mounts", "r");
    if (f == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%*s %1023s", mntpt) != 1) {
            continue;
        }
        ml = strlen(mntpt);
        while (ml > 1 && mntpt[ml - 1] == '/') {
            ml--;
        }
        if (ml == n && strncmp(mntpt, cmp, n) == 0) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static int player_ensure_sdcard_mounted(void)
{
    char udisk_name[128] = {0};
    if (path_is_mounted_at(SDCARD_MOUNT_PATH)) {
        return 0;
    }
    if (detect_storage_device(udisk_name, sizeof(udisk_name)) != 0) {
        return -1;
    }
    if (access(SDCARD_MOUNT_PATH, F_OK) != 0 && mkdir(SDCARD_MOUNT_PATH, 0777) != 0) {
        return -1;
    }
    if (mount(udisk_name, SDCARD_MOUNT_PATH, "exfat", 0, NULL) != 0 &&
        mount(udisk_name, SDCARD_MOUNT_PATH, NULL, 0, NULL) != 0) {
        return -1;
    }
    return 0;
}

static void player_unmount_sdcard_if_mounted(void)
{
    if (!path_is_mounted_at(SDCARD_MOUNT_PATH)) {
        return;
    }
    sync();
    if (umount(SDCARD_MOUNT_PATH) != 0) {
        LOGW(TAG, "卸载存储设备失败: %s", strerror(errno));
        return;
    }
    LOGI(TAG, "已卸载存储设备");
}

int player_offline_init_storage_and_library(int reset_player)
{
    Music_Node first_song;
    Shm_Data s;

    if (reset_player) {
        player_stop_play();
        player_write_fifo("quit\n");
        usleep(500000);
    }
    if (player_ensure_sdcard_mounted() != 0) {
        LOGW(TAG, "离线初始化：未挂载存储或无可挂载设备");
        link_clear_list();
        asr_kws_switch_offline_mode();
        g_current_online_mode = ONLINE_MODE_NO;
        g_current_state = PLAY_STATE_STOP;
        g_current_suspend = PLAY_SUSPEND_YES;
        player_set_audio_focus(AUDIO_FOCUS_IDLE);
        return -1;
    }
    if (music_lib_load_all_local_to_link() != 0) {
        LOGW(TAG, "离线初始化：载入本地曲库失败");
        asr_kws_switch_offline_mode();
        g_current_online_mode = ONLINE_MODE_NO;
        g_current_state = PLAY_STATE_STOP;
        g_current_suspend = PLAY_SUSPEND_YES;
        player_set_audio_focus(AUDIO_FOCUS_IDLE);
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
    player_set_audio_focus(AUDIO_FOCUS_IDLE);
    LOGI(TAG, "离线模式初始化完成（存储与曲库）");
    return 0;
}

void player_apply_env_mode(void)
{
    const char *v = getenv(PLAYER_MODE_ENV);
    g_env_forces_offline = 0;
    if (v != NULL && strcasecmp(v, "offline") == 0) {
        g_env_forces_offline = 1;
        g_current_online_mode = ONLINE_MODE_NO;
        asr_kws_switch_offline_mode();
    }
}

int player_env_forces_offline(void)
{
    return g_env_forces_offline;
}

int player_switch_offline_mode(void)
{
    Music_Node first_song;
    Shm_Data s;
    if (g_current_online_mode == ONLINE_MODE_NO) {
        tts_play_text("当前为离线模式，无需切换");
        return 0;
    }
    g_eof_autostart_suppressed = 1;
    player_stop_play();
    force_stop_playback_processes();
    socket_close_connection();
    sync();
    if (player_ensure_sdcard_mounted() != 0) {
        tts_play_text("请先插入存储设备");
        return 1;
    }
    if (music_lib_load_all_local_to_link() != 0) {
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
    player_set_audio_focus(AUDIO_FOCUS_IDLE);
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
    g_eof_autostart_suppressed = 1;
    player_stop_play();
    force_stop_playback_processes();
    socket_close_connection();
    if (socket_connect() != 0) {
        player_offline_init_storage_and_library(0);
        return -1;
    }
    player_unmount_sdcard_if_mounted();
    asr_kws_switch_online_mode();
    g_current_online_mode = ONLINE_MODE_YES;
    g_current_state = PLAY_STATE_STOP;
    g_current_suspend = PLAY_SUSPEND_YES;
    player_set_audio_focus(AUDIO_FOCUS_IDLE);
    tts_play_text("已切换到在线模式");
    return 0;
}
