#ifndef __SELECT_TEXT_H__
#define __SELECT_TEXT_H__

#include <stddef.h>

void select_text_trim(char *s);
int select_text_has_control_intent(const char *text);
int select_text_is_hot_generic_query(const char *query);
int select_text_extract_music_query(const char *text, char *out, size_t out_size);

#endif
