#ifndef IPC_PROTOCOL_H
#define IPC_PROTOCOL_H

#include <stdint.h>

#define FIFO_DIR_PATH "./fifo"
#define TTS_FIFO_PATH "/tmp/tts_fifo"
#define KWS_FIFO_PATH "./fifo/kws_fifo"
#define ASR_FIFO_PATH "./fifo/asr_fifo"
#define ASR_CTRL_FIFO_PATH "./fifo/asr_ctrl_fifo"
#define TTS_WAKE_DONE_FIFO_PATH "/tmp/tts_wake_done_fifo"
#define PLAYER_CTRL_FIFO_PATH "./fifo/player_ctrl_fifo"

#define MAX_TEXT_LEN 1024
#define MAX_FILENAME_LEN 256

#define WAKE_AUDIO_DIR "./voice-assistant/wake_audio"

#define TTS_TEMP_FILE "./tts_temp.wav"

#define ASR_TIMEOUT_SENTINEL "__asr_timeout__"

typedef enum {
    IPC_CMD_PLAY_TEXT,
    IPC_CMD_PLAY_AUDIO_FILE,
    IPC_CMD_STOP_PLAYING,
    IPC_CMD_PLAY_WAKE_RESPONSE,
    IPC_CMD_SWITCH_OFFLINE,
    IPC_CMD_SWITCH_ONLINE
} IPCCommandType;

#endif
