#ifndef __LLM_H__
#define __LLM_H__

#include <stdio.h>

int init_llm(void);
int generate_llm_response(const char *question, char *response, size_t response_len);
int query_llm(const char *question, char *response, size_t response_len);
void cleanup_llm(void);

#endif
