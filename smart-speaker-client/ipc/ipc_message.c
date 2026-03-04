#include "ipc_message.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int ipc_write_all(int fd, const void *buf, uint32_t len) {
    const uint8_t *ptr = (const uint8_t *)buf;
    uint32_t total = 0;
    while (total < len) {
        ssize_t ret = write(fd, ptr + total, len - total);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 2;
            }
            return -1;
        }
        if (ret == 0) {
            return -1;
        }
        total += (uint32_t)ret;
    }
    return 0;
}

int ipc_read_all(int fd, void *buf, uint32_t len) {
    uint8_t *ptr = (uint8_t *)buf;
    uint32_t total = 0;
    while (total < len) {
        ssize_t ret = read(fd, ptr + total, len - total);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (ret == 0) {
            if (total > 0) {
                errno = EPIPE;
                return -1;
            }
            return 1;
        }
        total += (uint32_t)ret;
    }
    return 0;
}

int ipc_send_message(int fd, uint16_t type, const void *body, uint32_t body_len, uint32_t *seq) {
    IPCHeader h;
    memset(&h, 0, sizeof(h));
    h.magic = IPC_MAGIC;
    h.version = IPC_VERSION;
    h.type = type;
    h.seq = (seq != NULL) ? ++(*seq) : 0;
    h.body_len = body_len;

    if (ipc_write_all(fd, &h, sizeof(h)) != 0) {
        return -1;
    }
    if (body_len > 0 && body != NULL) {
        if (ipc_write_all(fd, body, body_len) != 0) {
            return -1;
        }
    }
    return 0;
}

int ipc_recv_message(int fd, IPCHeader *header, uint8_t **body_out) {
    if (header == NULL || body_out == NULL) {
        return -1;
    }

    *body_out = NULL;

    int ret = ipc_read_all(fd, header, sizeof(*header));
    if (ret != 0) {
        return ret;
    }

    if (header->magic != IPC_MAGIC || header->version != IPC_VERSION || header->body_len > IPC_MAX_BODY) {
        errno = EINVAL;
        return -1;
    }

    if (header->body_len == 0) {
        return 0;
    }

    uint8_t *body = (uint8_t *)calloc(1, header->body_len + 1);
    if (body == NULL) {
        return -1;
    }

    ret = ipc_read_all(fd, body, header->body_len);
    if (ret != 0) {
        free(body);
        if (ret == 1) {
            errno = EPIPE;
            uint32_t left = header->body_len;
            while (left > 0) {
                char discard[256];
                uint32_t n = left > sizeof(discard) ? sizeof(discard) : left;
                ssize_t r = read(fd, discard, n);
                if (r <= 0) break;
                left -= (uint32_t)r;
            }
        }
        return -1;
    }

    *body_out = body;
    return 0;
}
