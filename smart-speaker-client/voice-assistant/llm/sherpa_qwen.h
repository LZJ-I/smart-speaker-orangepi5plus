#ifndef __SHERPA_QWEN_H__
#define __SHERPA_QWEN_H__

#include <stdio.h>

int init_sherpa_qwen(void);
int generate_qwen_response(const char *question, char *response, size_t response_len);
void cleanup_sherpa_qwen(void);

#endif
