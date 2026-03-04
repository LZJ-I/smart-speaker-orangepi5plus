#ifndef __TTS_IPC_HANDLER_H__
#define __TTS_IPC_HANDLER_H__

#include <stdint.h>

void handle_ipc_message(uint16_t type, const uint8_t *body, uint32_t body_len);

#endif
