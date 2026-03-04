#ifndef __TTS_PLAYBACK_H__
#define __TTS_PLAYBACK_H__

void tts_playback_notify_player(const char *event);
void tts_playback_stop(void);
int tts_playback_request_text(const char *text);
void tts_playback_wake_response(void);
void tts_playback_play_wav_file(const char *path);
void tts_playback_cleanup(void);
int tts_playback_get_content_session(void);
int tts_playback_is_playing(void);
void tts_playback_join(void);

#endif
