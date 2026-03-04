#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../ipc/ipc_message.h"
#include "../voice-assistant/common/ipc_protocol.h"

static uint16_t parse_type(const char *s) {
    if (strcmp(s, "text") == 0) return IPC_CMD_PLAY_TEXT;
    if (strcmp(s, "file") == 0) return IPC_CMD_PLAY_AUDIO_FILE;
    if (strcmp(s, "stop") == 0) return IPC_CMD_STOP_PLAYING;
    if (strcmp(s, "wake") == 0) return IPC_CMD_PLAY_WAKE_RESPONSE;
    return 0xFFFF;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <fifo_path> <text|file|stop|wake> [payload]\n", argv[0]);
        return 1;
    }

    uint16_t type = parse_type(argv[2]);
    if (type == 0xFFFF) {
        fprintf(stderr, "invalid type\n");
        return 1;
    }

    const char *payload = (argc >= 4) ? argv[3] : NULL;
    uint32_t body_len = payload ? (uint32_t)(strlen(payload) + 1) : 0;

    int fd = open(argv[1], O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "open fifo failed: %s\n", strerror(errno));
        return 1;
    }

    uint32_t seq = 0;
    if (ipc_send_message(fd, type, payload, body_len, &seq) != 0) {
        fprintf(stderr, "send failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}
