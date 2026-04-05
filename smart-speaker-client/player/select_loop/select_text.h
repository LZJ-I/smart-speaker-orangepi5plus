#ifndef __SELECT_TEXT_H__
#define __SELECT_TEXT_H__

#include <stddef.h>

void select_text_trim(char *s);
int select_text_has_control_intent(const char *text);
int select_text_is_hot_generic_query(const char *query);
int select_text_is_incomplete_music_query(const char *text);
int select_text_extract_music_query_source(const char *text, char *query, size_t query_size,
                                           char *source, size_t source_size);
int select_text_extract_music_query(const char *text, char *out, size_t out_size);
int select_text_is_playlist_query(const char *query);
void select_text_normalize_playlist_keyword(const char *query, char *out, size_t out_size);

#endif
