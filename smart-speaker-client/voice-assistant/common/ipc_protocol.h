#ifndef IPC_PROTOCOL_H
#define IPC_PROTOCOL_H

#include <stdint.h>

#define TAG "COMMON"

#define FIFO_DIR_PATH "./fifo"
#define TTS_FIFO_PATH "./fifo/tts_fifo"
#define KWS_FIFO_PATH "./fifo/kws_fifo"
#define ASR_FIFO_PATH "./fifo/asr_fifo"

#define MAX_TEXT_LEN 1024
#define MAX_FILENAME_LEN 256

#define WAKE_AUDIO_DIR "./voice-assistant/wake_audio"

#define TTS_TEMP_FILE "./tts_temp.wav"

typedef enum {
    IPC_CMD_PLAY_TEXT,
    IPC_CMD_PLAY_AUDIO_FILE,
    IPC_CMD_STOP_PLAYING,
    IPC_CMD_PLAY_WAKE_RESPONSE
} IPCCommandType;

typedef struct {
    IPCCommandType type;
    char text[MAX_TEXT_LEN];
    char filename[MAX_FILENAME_LEN];
} IPCMessage;

#endif
