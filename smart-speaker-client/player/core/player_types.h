#ifndef __PLAYER_TYPES_H__
#define __PLAYER_TYPES_H__

enum {
    ORDER_PLAY,
    SINGLE_PLAY
};

enum {
    PLAY_STATE_STOP,
    PLAY_STATE_PLAY
};

enum {
    PLAY_SUSPEND_NO,
    PLAY_SUSPEND_YES
};

enum {
    ONLINE_MODE_NO,
    ONLINE_MODE_YES
};

enum {
    AUDIO_FOCUS_IDLE,
    AUDIO_FOCUS_MUSIC_PLAYING,
    AUDIO_FOCUS_MUSIC_PAUSED_MANUAL,
    AUDIO_FOCUS_MUSIC_PAUSED_FOR_TTS,
    AUDIO_FOCUS_TTS_PLAYING,
    AUDIO_FOCUS_MUSIC_RESUMING
};

typedef struct {
    char keyword[256];
    char playlist_id[128];
    char playlist_source[32];
    int current_page;
    int total_pages;
    int page_size;
} player_playlist_ctx_t;

#endif
