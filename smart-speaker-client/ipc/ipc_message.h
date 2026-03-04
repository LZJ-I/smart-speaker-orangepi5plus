#ifndef IPC_MESSAGE_H
#define IPC_MESSAGE_H

#include <stdint.h>

#define IPC_MAGIC 0x53534D54u
#define IPC_VERSION 1u
#define IPC_MAX_BODY (64 * 1024u)

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t seq;
    uint32_t body_len;
} IPCHeader;

int ipc_write_all(int fd, const void *buf, uint32_t len);
int ipc_read_all(int fd, void *buf, uint32_t len);
int ipc_send_message(int fd, uint16_t type, const void *body, uint32_t body_len, uint32_t *seq);
int ipc_recv_message(int fd, IPCHeader *header, uint8_t **body_out);

#endif
