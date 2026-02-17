#ifndef __SHERPA_TTS_H__
#define __SHERPA_TTS_H__

#include  <stdio.h>
#include "sherpa-onnx/c-api/c-api.h"

extern const SherpaOnnxOfflineTts *g_tts;
extern unsigned int g_tts_sample_rate;

typedef int32_t (*TTSCallback)(const float *samples, int32_t n, void *arg);
typedef int32_t (*TTSProgressCallback)(const float *samples, int32_t n, float progress, void *arg);

int init_sherpa_tts(void);
int init_sherpa_tts_with_chunk_size(int max_num_sentences);
int generate_tts_audio(const char *text, const char *output_filename);
int generate_tts_audio_full(const char *text, float **samples, int32_t *n, uint32_t *sample_rate);
int generate_tts_audio_with_callback(const char *text, TTSCallback callback, void *arg);
int generate_tts_audio_with_progress_callback(const char *text, TTSProgressCallback callback, void *arg);
void destroy_generated_audio(float *samples);
void cleanup_sherpa_tts(void);

#endif
