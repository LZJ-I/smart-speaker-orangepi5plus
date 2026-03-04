#ifndef __PLAYER_H__
#define __PLAYER_H__

#include "player_constants.h"
#include "player_types.h"

int run_gst_player(const char *initial_uri);

extern int g_current_state;
extern int g_current_suspend;
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

int init_asr_fifo();
int init_kws_fifo();
int init_tts_fifo();
int init_player_ctrl_fifo();

int player_switch_offline_mode(void);
int player_switch_online_mode(void);
#endif
