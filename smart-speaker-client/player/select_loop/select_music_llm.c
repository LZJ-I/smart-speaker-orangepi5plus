#include "select_music_llm.h"
#include <stdio.h>
#include <string.h>
#include "select_text.h"
#include "player.h"
#include "select.h"
#include "debug_log.h"
#include "music_source.h"
#include "music_server_async.h"
#include "voice-assistant/llm/llm.h"

#define TAG "SELECT"

static const char *ONLINE_MUSIC_UNSUPPORTED_WAV = "./assets/tts/online_music_unsupported.wav";
static const char *FALLBACK_UNMATCHED_WAV = "./assets/tts/fallback_unmatched.wav";

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

void select_music_async_play_query_done(music_async_out_t out, MusicSourceItem *item, MusicSourceResult *search_res,
                                        const char *query)
{
    Music_Node track;
    char intro[512];
    char offline_msg[384];

    memset(&track, 0, sizeof(track));
    if (out == MUSIC_ASYNC_OK_RESOLVE) {
        if (player_play_query_resolve_done(item, query, &track) != 0) {
            out = MUSIC_ASYNC_FAIL;
        }
    } else if (out == MUSIC_ASYNC_OK_SEARCH) {
        if (player_play_query_search_done(search_res, query, &track) != 0) {
            out = MUSIC_ASYNC_FAIL;
        }
    } else if (out == MUSIC_ASYNC_OK_PLAYLIST) {
        if (player_play_query_playlist_done(search_res, query, &track) != 0) {
            out = MUSIC_ASYNC_FAIL;
        }
    }

    if (out == MUSIC_ASYNC_FAIL) {
        player_voice_cmd_clear_followup();
        if (music_source_take_online_search_blocked()) {
            tts_play_audio_file(ONLINE_MUSIC_UNSUPPORTED_WAV);
            return;
        }
        if (g_current_online_mode == ONLINE_MODE_NO) {
            snprintf(offline_msg, sizeof(offline_msg),
                     "当前为离线模式，本地没有%s，请尝试切换在线模式", query != NULL ? query : "");
            tts_play_text(offline_msg);
            return;
        }
        if (query != NULL && select_text_is_playlist_query(query)) {
            tts_play_audio_file(PLAYLIST_NOT_FOUND_WAV);
            return;
        }
        tts_play_audio_file(FALLBACK_UNMATCHED_WAV);
        return;
    }

    format_voice_track_intro(&track, intro, sizeof(intro));
    player_voice_intro_arm_deferred_play(PLAYER_VOICE_DEFER_INSERT_COMMIT);
    tts_play_text(intro);
}

int try_music_lib_play_playlist(const char *text)
{
    char query[256] = {0};
    char source[16] = {0};
    char intro[512];
    char offline_msg[384];
    Music_Node track;
    music_source_clear_online_search_blocked();
    if (select_text_is_incomplete_music_query(text)) {
        player_voice_cmd_clear_followup();
        tts_play_audio_file(ASK_WHAT_TO_LISTEN_WAV);
        return 1;
    }
    if (!select_text_extract_music_query_source(text, query, sizeof(query), source, sizeof(source))) {
        return 0;
    }
    if (!select_text_is_playlist_query(query)) {
        return 0;
    }
    LOGI(TAG, "歌单 source=%s query=%s", source[0] != '\0' ? source : "default", query);
    if (music_server_async_start_play_query(query, source) == 0) {
        return 1;
    }
    {
        char pl_kw[256];
        const char *insert_kw = query;
        select_text_normalize_playlist_keyword(query, pl_kw, sizeof(pl_kw));
        if (pl_kw[0] != '\0') {
            insert_kw = pl_kw;
        }
        if (player_search_insert_keyword_prepare_voice_intro(insert_kw, &track) != 0) {
            if (music_source_take_online_search_blocked()) {
                player_voice_cmd_clear_followup();
                tts_play_audio_file(ONLINE_MUSIC_UNSUPPORTED_WAV);
                return 1;
            }
            if (g_current_online_mode == ONLINE_MODE_NO) {
                player_voice_cmd_clear_followup();
                snprintf(offline_msg, sizeof(offline_msg),
                         "当前为离线模式，本地没有%s，请尝试切换在线模式", query);
                tts_play_text(offline_msg);
                return 1;
            }
            return 0;
        }
    }
    format_voice_track_intro(&track, intro, sizeof(intro));
    player_voice_intro_arm_deferred_play(PLAYER_VOICE_DEFER_INSERT_COMMIT);
    tts_play_text(intro);
    return 1;
}

int try_music_lib_play(const char *text)
{
    char query[256] = {0};
    char source[16] = {0};
    char intro[512];
    char offline_msg[384];
    Music_Node track;
    music_source_clear_online_search_blocked();
    if (!select_text_extract_music_query_source(text, query, sizeof(query), source, sizeof(source))) {
        return 0;
    }
    if (select_text_is_playlist_query(query)) {
        return 0;
    }
    if (select_text_is_hot_generic_query(query)) {
        LOGI(TAG, "搜歌关键词 source=%s query=热门", source[0] != '\0' ? source : "default");
        if (player_search_hot_random_prepare_for_tts(&track) != 0) {
            if (g_current_online_mode == ONLINE_MODE_NO) {
                player_voice_cmd_clear_followup();
                snprintf(offline_msg, sizeof(offline_msg),
                         "当前为离线模式，本地没有合适的歌曲，请尝试切换在线模式");
                tts_play_text(offline_msg);
                return 1;
            }
            return 0;
        }
        format_voice_track_intro(&track, intro, sizeof(intro));
        player_voice_intro_arm_deferred_play(PLAYER_VOICE_DEFER_HOT_RANDOM);
        tts_play_text(intro);
        return 1;
    }
    LOGI(TAG, "搜歌关键词 source=%s query=%s", source[0] != '\0' ? source : "default", query);
    if (music_server_async_start_play_query(query, source) == 0) {
        return 1;
    }
    if (player_search_insert_keyword_prepare_voice_intro(query, &track) != 0) {
        if (music_source_take_online_search_blocked()) {
            player_voice_cmd_clear_followup();
            tts_play_audio_file(ONLINE_MUSIC_UNSUPPORTED_WAV);
            return 1;
        }
        if (g_current_online_mode == ONLINE_MODE_NO) {
            player_voice_cmd_clear_followup();
            snprintf(offline_msg, sizeof(offline_msg),
                     "当前为离线模式，本地没有%s，请尝试切换在线模式", query);
            tts_play_text(offline_msg);
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
    player_voice_cmd_clear_followup();
    if (query_llm(question, response, sizeof(response)) != 0 || response[0] == '\0') {
        return -1;
    }
    tts_play_text(response);
    return 0;
}
