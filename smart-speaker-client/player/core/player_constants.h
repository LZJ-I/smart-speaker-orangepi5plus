#ifndef __PLAYER_CONSTANTS_H__
#define __PLAYER_CONSTANTS_H__

#define SDCARD_MOUNT_PATH "/mnt/sdcard/"
#define UDISK_MOUNT_PATH SDCARD_MOUNT_PATH

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8888
#define MUSIC_PATH  UDISK_MOUNT_PATH

#define GST_CMD_FIFO       "./fifo/cmd_fifo"
#define GST_ALSA_DEVICE   "dmix:CARD=rockchipes8388,DEV=0"
#define DEFAULT_VOLUME 60

#define ASR_PIPE_PATH "./fifo/asr_fifo"
#define KWS_PIPE_PATH "./fifo/kws_fifo"
#define TTS_PIPE_PATH "/tmp/tts_fifo"
#define ASR_CTRL_PIPE_PATH "./fifo/asr_ctrl_fifo"
#define PLAYER_CTRL_PIPE_PATH "./fifo/player_ctrl_fifo"

#define UDISK_PATH_YES1 "/dev/sdb1"
#define UDISK_PATH_YES2 "/dev/sdc1"
#define UDISK_PATH_YES3 "/dev/sda1"

#define SERVER_DISCONNECT_OFFLINE_WAV "./assets/tts/server_disconnect_offline.wav"
#define SERVER_CONNECT_FAILED_WAV "./assets/tts/server_connect_failed.wav"
#define ASK_WHAT_TO_LISTEN_WAV "./assets/tts/ask_what_to_listen.wav"
#define INSERT_STORAGE_DEVICE_WAV "./assets/tts/insert_storage_device.wav"
#define MODE_ONLINE_WAV "./assets/tts/mode_online.wav"
#define MODE_ONLINE_ALREADY_WAV "./assets/tts/mode_online_already.wav"
#define MODE_OFFLINE_WAV "./assets/tts/mode_offline.wav"
#define MODE_OFFLINE_ALREADY_WAV "./assets/tts/mode_offline_already.wav"
#define MODE_OFFLINE_SWITCH_FAILED_WAV "./assets/tts/mode_offline_switch_failed.wav"
#define NOOP_REPLY_RECALL_WAV "./assets/tts/noop_reply_recall.wav"
#define NOOP_REPLY_LEAVE_WAV "./assets/tts/noop_reply_leave.wav"
#define NOOP_REPLY_OK_WAV "./assets/tts/noop_reply_ok.wav"
#define PLAYLIST_NOT_FOUND_WAV "./assets/tts/playlist_not_found.wav"

#endif
