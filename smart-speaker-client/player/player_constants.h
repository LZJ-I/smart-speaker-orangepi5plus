#ifndef __PLAYER_CONSTANTS_H__
#define __PLAYER_CONSTANTS_H__

#define SDCARD_MOUNT_PATH "/mnt/sdcard/"
#define UDISK_MOUNT_PATH SDCARD_MOUNT_PATH

#define ONNINE_URL "http://10.102.178.47/music/"
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

#endif
