#ifndef __SHERPA_TTS_H__
#define __SHERPA_TTS_H__

#include  <stdio.h>
#include "sherpa-onnx/c-api/c-api.h"

extern const SherpaOnnxOfflineTts *g_tts;
extern unsigned int g_tts_sample_rate;

typedef int32_t (*TTSCallback)(const float *samples, int32_t n, void *arg);
typedef int32_t (*TTSProgressCallback)(const float *samples, int32_t n, float progress, void *arg);

int init_sherpa_tts(void);
int generate_tts_audio(const char *text, int64_t sid, float speed, const char *output_filename);
int generate_tts_audio_with_callback(const char *text, int64_t sid, float speed, TTSCallback callback, void *arg);
int generate_tts_audio_with_progress_callback(const char *text, int64_t sid, float speed, TTSProgressCallback callback, void *arg);
void cleanup_sherpa_tts(void);

#endif
