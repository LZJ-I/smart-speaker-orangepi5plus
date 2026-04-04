#ifndef __PLAYER_CONSTANTS_H__
#define __PLAYER_CONSTANTS_H__

#define SDCARD_MOUNT_PATH "/mnt/sdcard/"
#define UDISK_MOUNT_PATH SDCARD_MOUNT_PATH

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8888
#define SERVER_MUSIC_BASE_URL "http://127.0.0.1/music/"
#define SERVER_IP_ENV "SMART_SPEAKER_SERVER_IP"
#define SERVER_PORT_ENV "SMART_SPEAKER_SERVER_PORT"
#define SERVER_MUSIC_BASE_URL_ENV "SMART_SPEAKER_SERVER_MUSIC_BASE_URL"
#define ONNINE_URL SERVER_MUSIC_BASE_URL
#define LOCAL_MUSIC_ROOT_ENV "SMART_SPEAKER_LOCAL_MUSIC_DIR"
#define PLAYER_MODE_ENV "SMART_SPEAKER_PLAYER_MODE"
#define MUSIC_PATH  UDISK_MOUNT_PATH

#define GST_CMD_FIFO       "./fifo/cmd_fifo"
#define GST_ALSA_DEVICE   "dmix:CARD=rockchipes8388,DEV=0"
#define GST_ALSA_DEVICE_ENV "SMART_SPEAKER_GST_ALSA_DEVICE"
#define DEFAULT_VOLUME 60

#define ASR_PIPE_PATH "./fifo/asr_fifo"
#define KWS_PIPE_PATH "./fifo/kws_fifo"
#define TTS_PIPE_PATH "/tmp/tts_fifo"
#define ASR_CTRL_PIPE_PATH "./fifo/asr_ctrl_fifo"
#define PLAYER_CTRL_PIPE_PATH "./fifo/player_ctrl_fifo"

#define UDISK_PATH_YES1 "/dev/sdb1"
#define UDISK_PATH_YES2 "/dev/sdc1"
#define UDISK_PATH_YES3 "/dev/sda1"

#endif
