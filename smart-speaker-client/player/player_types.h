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

typedef struct {
    char keyword[256];
    int current_page;
    int total_pages;
    int page_size;
} player_playlist_ctx_t;

#endif
