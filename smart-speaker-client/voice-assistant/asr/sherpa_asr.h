#ifndef __SHERPA_ASR_H__
#define __SHERPA_ASR_H__

#include <stdio.h>
#include "sherpa-onnx/c-api/c-api.h"
#include "../common/alsa.h"

extern char g_current_asr_text_buffer[1024];

extern const SherpaOnnxVoiceActivityDetector *g_vad;
extern const SherpaOnnxOfflineRecognizer *g_offline_recognizer;
extern const SherpaOnnxCircularBuffer *g_audio_buffer;

int init_sherpa_asr(void);
int process_asr_result(float *model_audio, int model_frame);
void cleanup_sherpa_asr(void);


#endif
