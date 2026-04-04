#ifndef __PLAYER_H__
#define __PLAYER_H__

#include <signal.h>

#include "player_constants.h"
#include "player_types.h"

int run_gst_player(const char *initial_uri);

extern volatile sig_atomic_t g_current_state;
extern volatile sig_atomic_t g_current_suspend;
extern volatile sig_atomic_t g_player_shutdown_requested;
extern volatile sig_atomic_t g_audio_focus_state;
extern int g_current_online_mode;

extern int g_asr_fd;
extern int g_kws_fd;
extern int g_tts_fd;
extern int g_player_ctrl_fd;

void player_start_play();
void player_stop_play(void);
void player_continue_play();
void player_suspend_play();
int player_next_song();
int player_prev_song();
void player_set_mode(int mode);
void player_singer_play(const char *singer);
int player_play_url(const char *url);
int player_search_and_play_keyword(const char *keyword);
int player_search_and_play_hot_random(void);
int player_prepare_keyword_playlist(const char *keyword, int auto_start);
int player_playlist_load_next_page(void);
void player_get_playlist_ctx(player_playlist_ctx_t *out_ctx);
void player_handle_playlist_eof(int sig);
void player_handle_sigchld(int sig);
void player_process_async_events(void);
void player_set_audio_focus(int focus_state);
int player_get_audio_focus(void);
int player_audio_focus_should_resume(void);
void player_audio_focus_prepare_resume(void);
void player_audio_focus_cancel_resume(void);
void player_audio_focus_mark_tts_standalone(void);
void player_suspend_for_tts(void);

int init_asr_fifo();
int init_kws_fifo();
int init_tts_fifo();
int init_player_ctrl_fifo();

int player_switch_offline_mode(void);
int player_switch_online_mode(void);
int player_offline_init_storage_and_library(int reset_player);
void player_apply_env_mode(void);
int player_env_forces_offline(void);
void player_cmd_fifo_close(void);
#endif
