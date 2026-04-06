#ifndef SMART_SPEAKER_PLAYER_RUNTIME_CONFIG_H
#define SMART_SPEAKER_PLAYER_RUNTIME_CONFIG_H

const char *player_runtime_server_ip(void);
int player_runtime_server_port(void);
const char *player_runtime_local_music_root(void);
int player_runtime_startup_volume(void);
const char *player_runtime_player_mode(void);
const char *player_runtime_gst_alsa_device(void);
const char *player_runtime_music_search_source(void);
const char *player_runtime_device_id(void);

#endif
