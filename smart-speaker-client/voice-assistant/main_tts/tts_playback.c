#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include "../../debug_log.h"
#include "../common/ipc_protocol.h"
#include "../tts/alsa_output.h"
#include "../tts/sherpa_tts.h"

#define TAG "TTS_MAIN"
#define PLAYBACK_BUFFER_SIZE (16 * 1024)

typedef struct {
    char riff[4];
    uint32_t file_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} WavHeader;

static pthread_t playback_thread = 0;
static pthread_mutex_t playback_mutex = PTHREAD_MUTEX_INITIALIZER;
static int playback_should_stop = 0;
static float *playback_samples = NULL;
static int32_t playback_n = 0;
static char *playback_wav_filename = NULL;
static int s_wav_play_is_wake = 0;
static volatile int s_tts_content_session = 0;

static void notify_player_tts_event(const char *event) {
    int fd = open(PLAYER_CTRL_FIFO_PATH, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        LOGW(TAG, "上报TTS事件失败: %s", event);
        return;
    }
    write(fd, event, strlen(event));
    write(fd, "\n", 1);
    close(fd);
    LOGI(TAG, "上报TTS事件: %s", event);
}

static void* playback_text_thread(void *arg) {
    LOGI(TAG, "播放线程启动");
    float *samples = NULL;
    int32_t n = 0;
    pthread_mutex_lock(&playback_mutex);
    samples = playback_samples;
    n = playback_n;
    playback_samples = NULL;
    playback_n = 0;
    pthread_mutex_unlock(&playback_mutex);
    if (samples == NULL || n == 0) {
        LOGE(TAG, "没有可播放的音频数据");
        return NULL;
    }
    if (g_pcm_handle != NULL) {
        snd_pcm_drop(g_pcm_handle);
        if (snd_pcm_prepare(g_pcm_handle) < 0) {
            LOGE(TAG, "准备PCM设备失败");
            destroy_generated_audio(samples);
            return NULL;
        }
        pcm_write_silence(g_pcm_handle);
    }
    unsigned int sample_rate = g_tts_sample_rate;
    unsigned int playback_rate = g_alsa_playback_rate;
    int need_resample = (playback_rate != 0 && playback_rate != sample_rate);
    size_t max_out_frames = need_resample ? (size_t)(PERIOD_SIZE * playback_rate / sample_rate) + 1 : (size_t)PERIOD_SIZE;
    if (max_out_frames > 1024) max_out_frames = 1024;
    int16_t *processed_samples = malloc(max_out_frames * sizeof(int16_t));
    int16_t *stereo_buf = malloc(max_out_frames * 2 * sizeof(int16_t));
    float *resampled_buf = need_resample ? (float *)malloc(max_out_frames * sizeof(float)) : NULL;
    if (processed_samples == NULL || stereo_buf == NULL || (need_resample && resampled_buf == NULL)) {
        free(processed_samples);
        free(stereo_buf);
        free(resampled_buf);
        destroy_generated_audio(samples);
        return NULL;
    }
    int32_t offset = 0;
    int should_stop_local = 0;
    while (offset < n && !should_stop_local) {
        pthread_mutex_lock(&playback_mutex);
        should_stop_local = playback_should_stop;
        pthread_mutex_unlock(&playback_mutex);
        if (should_stop_local) break;
        int32_t samples_to_process = (n - offset) > PERIOD_SIZE ? PERIOD_SIZE : (n - offset);
        int out_frames;
        const int16_t *write_buf;
        snd_pcm_uframes_t write_frames;
        if (need_resample && resampled_buf != NULL) {
            out_frames = (int)((long)samples_to_process * (long)playback_rate / (long)sample_rate);
            if (out_frames > (int)max_out_frames) out_frames = (int)max_out_frames;
            for (int i = 0; i < out_frames; i++) {
                float pos = (float)i * (float)sample_rate / (float)playback_rate;
                int idx = (int)pos;
                float frac = pos - idx;
                if (idx >= samples_to_process - 1)
                    resampled_buf[i] = samples[offset + samples_to_process - 1];
                else
                    resampled_buf[i] = samples[offset + idx] * (1.0f - frac) + samples[offset + idx + 1] * frac;
            }
            for (int i = 0; i < out_frames; i++) {
                float t = resampled_buf[i];
                if (t < -1.0f) t = -1.0f; else if (t > 1.0f) t = 1.0f;
                processed_samples[i] = (int16_t)(t * 32767);
            }
            for (int i = 0; i < out_frames; i++) {
                stereo_buf[i * 2] = processed_samples[i];
                stereo_buf[i * 2 + 1] = processed_samples[i];
            }
            write_buf = stereo_buf;
            write_frames = (snd_pcm_uframes_t)out_frames;
        } else {
            float t;
            for (int32_t i = 0; i < samples_to_process; i++) {
                t = samples[offset + i];
                if (t < -1.0f) t = -1.0f; else if (t > 1.0f) t = 1.0f;
                processed_samples[i] = (int16_t)(t * 32767);
            }
            for (int32_t i = 0; i < samples_to_process; i++) {
                stereo_buf[i * 2] = processed_samples[i];
                stereo_buf[i * 2 + 1] = processed_samples[i];
            }
            write_buf = stereo_buf;
            write_frames = (snd_pcm_uframes_t)samples_to_process;
        }
        pthread_mutex_lock(&playback_mutex);
        if (playback_should_stop) { pthread_mutex_unlock(&playback_mutex); break; }
        pthread_mutex_unlock(&playback_mutex);
        if (pcm_write_all(g_pcm_handle, write_buf, write_frames) < 0) {
            LOGE(TAG, "写入PCM失败");
            break;
        }
        offset += samples_to_process;
    }
    pthread_mutex_lock(&playback_mutex);
    should_stop_local = playback_should_stop;
    pthread_mutex_unlock(&playback_mutex);
    if (should_stop_local && g_pcm_handle != NULL) {
        snd_pcm_drop(g_pcm_handle);
    } else if (!should_stop_local && g_pcm_handle != NULL) {
        snd_pcm_drain(g_pcm_handle);
    }
    free(processed_samples);
    free(stereo_buf);
    free(resampled_buf);
    destroy_generated_audio(samples);
    if (should_stop_local) {
        LOGI(TAG, "文本播放已被打断");
    } else {
        LOGI(TAG, "文本播放完成");
    }
    s_tts_content_session = 0;
    notify_player_tts_event("tts:done");
    pthread_mutex_lock(&playback_mutex);
    playback_thread = 0;
    pthread_mutex_unlock(&playback_mutex);
    return NULL;
}

static void* playback_wav_thread(void *arg) {
    LOGI(TAG, "WAV播放线程启动");
    char *filename = NULL;
    pthread_mutex_lock(&playback_mutex);
    filename = playback_wav_filename;
    playback_wav_filename = NULL;
    pthread_mutex_unlock(&playback_mutex);
    if (filename == NULL) {
        LOGE(TAG, "没有WAV文件名");
        return NULL;
    }
    LOGI(TAG, "开始播放WAV文件: %s", filename);
    FILE *wav_file = fopen(filename, "rb");
    if (wav_file == NULL) {
        LOGE(TAG, "无法打开文件: %s", filename);
        free(filename);
        return NULL;
    }
    WavHeader header;
    size_t read_size = fread(&header, 1, sizeof(WavHeader), wav_file);
    if (read_size != sizeof(WavHeader)) {
        LOGE(TAG, "读取WAV头失败");
        fclose(wav_file);
        free(filename);
        return NULL;
    }
    if (memcmp(header.riff, "RIFF", 4) != 0 || memcmp(header.wave, "WAVE", 4) != 0) {
        LOGE(TAG, "不是有效的WAV文件: %s", filename);
        fclose(wav_file);
        free(filename);
        return NULL;
    }
    if (header.audio_format != 1) {
        LOGE(TAG, "不支持的音频格式: %d", header.audio_format);
        fclose(wav_file);
        free(filename);
        return NULL;
    }
    LOGI(TAG, "WAV文件信息: 采样率=%d Hz, 声道数=%d, 位深=%d bit",
           header.sample_rate, header.num_channels, header.bits_per_sample);
    char chunk_id[5] = {0};
    uint32_t chunk_size;
    while (1) {
        if (fread(chunk_id, 1, 4, wav_file) != 4) break;
        if (fread(&chunk_size, 1, 4, wav_file) != 4) break;
        if (memcmp(chunk_id, "data", 4) == 0) break;
        fseek(wav_file, chunk_size, SEEK_CUR);
    }
    if (memcmp(chunk_id, "data", 4) != 0) {
        LOGE(TAG, "未找到data块");
        fclose(wav_file);
        free(filename);
        return NULL;
    }
    if (g_pcm_handle != NULL) {
        snd_pcm_drop(g_pcm_handle);
        if (snd_pcm_prepare(g_pcm_handle) < 0) {
            LOGE(TAG, "准备PCM设备失败");
            fclose(wav_file);
            free(filename);
            return NULL;
        }
        pcm_write_silence(g_pcm_handle);
    }
    uint32_t bytes_per_sample = header.bits_per_sample / 8;
    uint32_t buffer_size_bytes = PLAYBACK_BUFFER_SIZE * bytes_per_sample;
    uint8_t *buffer = (uint8_t *)malloc(buffer_size_bytes);
    uint8_t *stereo_buf = (uint8_t *)malloc(buffer_size_bytes * 2);
    if (buffer == NULL || stereo_buf == NULL) {
        free(buffer);
        free(stereo_buf);
        fclose(wav_file);
        free(filename);
        return NULL;
    }
    unsigned int wav_rate = header.sample_rate;
    unsigned int playback_rate = g_alsa_playback_rate;
    int wav_need_resample = (playback_rate != 0 && playback_rate != wav_rate);
    size_t max_wav_out = wav_need_resample ? (buffer_size_bytes / bytes_per_sample) * playback_rate / wav_rate + 1 : 0;
    int16_t *wav_resampled = NULL;
    if (wav_need_resample && max_wav_out > 0) {
        wav_resampled = (int16_t *)malloc(max_wav_out * 2 * sizeof(int16_t));
        if (!wav_resampled) wav_need_resample = 0;
    }
    uint32_t total_bytes_read = 0;
    LOGI(TAG, "开始播放 (总数据大小: %u bytes, 块大小: %u bytes)", chunk_size, buffer_size_bytes);
    int should_stop_local = 0;
    while (total_bytes_read < chunk_size && !should_stop_local) {
        pthread_mutex_lock(&playback_mutex);
        should_stop_local = playback_should_stop;
        pthread_mutex_unlock(&playback_mutex);
        if (should_stop_local) break;
        uint32_t bytes_to_read = (chunk_size - total_bytes_read) < buffer_size_bytes ?
                                  (chunk_size - total_bytes_read) : buffer_size_bytes;
        size_t bytes_read = fread(buffer, 1, bytes_to_read, wav_file);
        if (bytes_read != bytes_to_read) {
            LOGE(TAG, "读取音频数据失败 (已读 %zu, 预期 %u)", bytes_read, bytes_to_read);
            break;
        }
        int32_t num_samples = bytes_read / bytes_per_sample;
        const int16_t *mono = (const int16_t *)buffer;
        snd_pcm_uframes_t write_frames;
        const int16_t *write_buf;
        if (wav_need_resample && wav_resampled != NULL) {
            int out_samples = (int)((long)num_samples * (long)playback_rate / (long)wav_rate);
            if (out_samples > (int)max_wav_out) out_samples = (int)max_wav_out;
            for (int i = 0; i < out_samples; i++) {
                float pos = (float)i * (float)wav_rate / (float)playback_rate;
                int idx = (int)pos;
                float frac = pos - idx;
                int16_t v;
                if (idx >= num_samples - 1)
                    v = mono[num_samples - 1];
                else
                    v = (int16_t)(mono[idx] * (1.0f - frac) + mono[idx + 1] * frac);
                wav_resampled[i * 2] = v;
                wav_resampled[i * 2 + 1] = v;
            }
            write_buf = wav_resampled;
            write_frames = (snd_pcm_uframes_t)out_samples;
        } else {
            for (int32_t i = 0; i < num_samples; i++) {
                memcpy(stereo_buf + i * bytes_per_sample * 2, buffer + i * bytes_per_sample, bytes_per_sample);
                memcpy(stereo_buf + i * bytes_per_sample * 2 + bytes_per_sample, buffer + i * bytes_per_sample, bytes_per_sample);
            }
            write_buf = (const int16_t *)stereo_buf;
            write_frames = (snd_pcm_uframes_t)num_samples;
        }
        pthread_mutex_lock(&playback_mutex);
        if (playback_should_stop) { pthread_mutex_unlock(&playback_mutex); break; }
        pthread_mutex_unlock(&playback_mutex);
        if (pcm_write_all(g_pcm_handle, write_buf, write_frames) < 0) {
            LOGE(TAG, "ALSA写入失败");
            break;
        }
        total_bytes_read += bytes_read;
    }
    pthread_mutex_lock(&playback_mutex);
    should_stop_local = playback_should_stop;
    pthread_mutex_unlock(&playback_mutex);
    if (should_stop_local && g_pcm_handle != NULL) {
        snd_pcm_drop(g_pcm_handle);
    } else if (!should_stop_local && g_pcm_handle != NULL) {
        snd_pcm_drain(g_pcm_handle);
    }
    free(buffer);
    free(stereo_buf);
    free(wav_resampled);
    fclose(wav_file);
    free(filename);
    if (should_stop_local) {
        LOGI(TAG, "WAV播放已停止");
    } else {
        LOGI(TAG, "WAV播放完成");
    }
    if (!s_wav_play_is_wake)
        notify_player_tts_event("tts:done");
    s_wav_play_is_wake = 0;
    pthread_mutex_lock(&playback_mutex);
    playback_thread = 0;
    pthread_mutex_unlock(&playback_mutex);
    return NULL;
}

void tts_playback_notify_player(const char *event) {
    notify_player_tts_event(event);
}

void tts_playback_stop(void) {
    pthread_mutex_lock(&playback_mutex);
    playback_should_stop = 1;
    pthread_mutex_unlock(&playback_mutex);
    if (g_pcm_handle != NULL) {
        snd_pcm_drop(g_pcm_handle);
    }
    if (playback_thread != 0) {
        pthread_join(playback_thread, NULL);
        playback_thread = 0;
    }
}

int tts_playback_request_text(const char *text) {
    s_tts_content_session = 1;
    float *samples = NULL;
    int32_t n = 0;
    uint32_t sample_rate = 0;
    if (generate_tts_audio_full(text, &samples, &n, &sample_rate) != 0 || n <= 0) {
        s_tts_content_session = 0;
        return -1;
    }
    tts_playback_notify_player("tts:start");
    pthread_mutex_lock(&playback_mutex);
    playback_should_stop = 0;
    if (playback_samples != NULL) destroy_generated_audio(playback_samples);
    playback_samples = samples;
    playback_n = n;
    pthread_mutex_unlock(&playback_mutex);
    pthread_create(&playback_thread, NULL, playback_text_thread, NULL);
    return 0;
}

void tts_playback_wake_response(void) {
    const char *wake_files[] = {
        "./voice-assistant/wake_audio/nihao.wav",
        "./voice-assistant/wake_audio/wozai.wav",
        "./voice-assistant/wake_audio/zaine.wav"
    };
    int num_files = 3;
    srand((unsigned int)time(NULL));
    int idx = rand() % num_files;
    LOGI(TAG, "播放唤醒响应: %s", wake_files[idx]);
    if (access(wake_files[idx], F_OK) == 0) {
        s_wav_play_is_wake = 1;
        pthread_mutex_lock(&playback_mutex);
        playback_should_stop = 0;
        if (playback_wav_filename != NULL) {
            free(playback_wav_filename);
        }
        playback_wav_filename = strdup(wake_files[idx]);
        pthread_mutex_unlock(&playback_mutex);
        if (playback_thread != 0) {
            pthread_join(playback_thread, NULL);
            playback_thread = 0;
        }
        pthread_create(&playback_thread, NULL, playback_wav_thread, NULL);
    } else {
        LOGW(TAG, "唤醒音频文件不存在: %s", wake_files[idx]);
    }
}

void tts_playback_play_wav_file(const char *path) {
    if (path == NULL || path[0] == '\0') return;
    if (access(path, F_OK) != 0) {
        LOGW(TAG, "音频文件不存在: %s", path);
        return;
    }
    LOGI(TAG, "播放WAV文件(同唤醒路径): %s", path);
    pthread_mutex_lock(&playback_mutex);
    playback_should_stop = 0;
    if (playback_wav_filename != NULL) {
        free(playback_wav_filename);
    }
    playback_wav_filename = strdup(path);
    pthread_mutex_unlock(&playback_mutex);
    if (playback_thread != 0) {
        pthread_join(playback_thread, NULL);
        playback_thread = 0;
    }
    pthread_create(&playback_thread, NULL, playback_wav_thread, NULL);
}

void tts_playback_cleanup(void) {
    pthread_mutex_lock(&playback_mutex);
    playback_should_stop = 1;
    pthread_mutex_unlock(&playback_mutex);
    if (playback_thread != 0) {
        pthread_join(playback_thread, NULL);
        playback_thread = 0;
    }
    pthread_mutex_destroy(&playback_mutex);
    if (playback_samples != NULL) {
        destroy_generated_audio(playback_samples);
        playback_samples = NULL;
    }
    if (playback_wav_filename != NULL) {
        free(playback_wav_filename);
        playback_wav_filename = NULL;
    }
}

int tts_playback_get_content_session(void) {
    return s_tts_content_session;
}

int tts_playback_is_playing(void) {
    return playback_thread != 0;
}

void tts_playback_join(void) {
    if (playback_thread != 0) {
        pthread_join(playback_thread, NULL);
        playback_thread = 0;
    }
}
