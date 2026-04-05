#include "select_music_llm.h"
#include <stdio.h>
#include "select_text.h"
#include "player.h"
#include "select.h"
#include "debug_log.h"
#include "music_source.h"
#include "voice-assistant/llm/llm.h"

#define TAG "SELECT"

static const char *ONLINE_MUSIC_UNSUPPORTED_WAV = "./assets/tts/online_music_unsupported.wav";

int try_music_lib_play(const char *text)
{
    char query[256] = {0};
    music_source_clear_online_search_blocked();
    if (!select_text_extract_music_query(text, query, sizeof(query))) {
        return 0;
    }
    if (select_text_is_hot_generic_query(query)) {
        LOGI(TAG, "搜歌关键词: 热门");
        return (player_search_and_play_hot_random() == 0) ? 1 : 0;
    }
    LOGI(TAG, "搜歌关键词: %s", query);
    if (player_search_insert_keyword_and_play(query) == 0) {
        return 1;
    }
    if (music_source_take_online_search_blocked()) {
        tts_play_audio_file(ONLINE_MUSIC_UNSUPPORTED_WAV);
        return 1;
    }
    return 0;
}

int run_llm_and_tts(const char *raw_text)
{
    char question[256] = {0};
    char response[4096] = {0};
    if (raw_text == NULL || raw_text[0] == '\0') return -1;
    snprintf(question, sizeof(question), "%s", raw_text);
    select_text_trim(question);
    if (question[0] == '\0') return -1;
    if (query_llm(question, response, sizeof(response)) != 0 || response[0] == '\0') {
        return -1;
    }
    tts_play_text(response);
    return 0;
}
