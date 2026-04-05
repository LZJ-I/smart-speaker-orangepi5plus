#include "select_music_llm.h"
#include <stdio.h>
#include <string.h>
#include "select_text.h"
#include "player.h"
#include "select.h"
#include "debug_log.h"
#include "music_source.h"
#include "voice-assistant/llm/llm.h"

#define TAG "SELECT"

static const char *ONLINE_MUSIC_UNSUPPORTED_WAV = "./assets/tts/online_music_unsupported.wav";

static void format_voice_track_intro(const Music_Node *t, char *buf, size_t buf_sz)
{
    const char *sg = (t->singer[0] != '\0') ? t->singer : "";
    const char *sn = (t->song_name[0] != '\0') ? t->song_name : "";

    if (sg[0] != '\0' && sn[0] != '\0') {
        snprintf(buf, buf_sz, "好的，将为你播放%s的%s", sg, sn);
    } else if (sn[0] != '\0') {
        snprintf(buf, buf_sz, "好的，将为你播放%s", sn);
    } else if (sg[0] != '\0') {
        snprintf(buf, buf_sz, "好的，将为你播放%s的作品", sg);
    } else {
        snprintf(buf, buf_sz, "好的，即将为你播放");
    }
}

int try_music_lib_play(const char *text)
{
    char query[256] = {0};
    char intro[512];
    Music_Node track;
    music_source_clear_online_search_blocked();
    if (!select_text_extract_music_query(text, query, sizeof(query))) {
        return 0;
    }
    if (select_text_is_hot_generic_query(query)) {
        LOGI(TAG, "搜歌关键词: 热门");
        if (player_search_hot_random_prepare_for_tts(&track) != 0) {
            return 0;
        }
        format_voice_track_intro(&track, intro, sizeof(intro));
        player_voice_intro_arm_deferred_play(PLAYER_VOICE_DEFER_HOT_RANDOM);
        tts_play_text(intro);
        return 1;
    }
    LOGI(TAG, "搜歌关键词: %s", query);
    if (player_search_insert_keyword_prepare_voice_intro(query, &track) != 0) {
        if (music_source_take_online_search_blocked()) {
            tts_play_audio_file(ONLINE_MUSIC_UNSUPPORTED_WAV);
            return 1;
        }
        return 0;
    }
    format_voice_track_intro(&track, intro, sizeof(intro));
    player_voice_intro_arm_deferred_play(PLAYER_VOICE_DEFER_INSERT_COMMIT);
    tts_play_text(intro);
    return 1;
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
