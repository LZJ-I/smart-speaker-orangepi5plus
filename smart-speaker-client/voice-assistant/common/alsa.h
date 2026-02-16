#ifndef __ALSA_H__
#define __ALSA_H__

#include <stdint.h>
#include <stdio.h>
#include <alsa/asoundlib.h>

// 录音设备配置
#define RECORD_DEVICE   "MICCM379"    // 录音设备（根据实际情况调整）
#define RATE            16000       // ALSA请求采样率（硬件可能不支持，实际以actual_rate为准）
#define PERIOD_SIZE     1024        // 录音缓冲区周期大小（单次读取样本数）
#define CHANNELS        1           // 单声道（与模型一致）


// 全局PCM句柄（供ALSA初始化和重采样使用）
extern snd_pcm_t *g_pcm_handle;
// 实际采样率（由ALSA初始化后设置）
extern unsigned int g_actual_rate;

// 查找指定录音设备的ALSA设备名称
int get_audio_device(const char* target_device, char* result);

// 语音识别模型配置（模型的目标采样率，需根据sherpa模型实际参数调整！）
#define MODEL_SAMPLE_RATE 16000     // sherpa-asr模型采样率为16000Hz

// ALSA相关函数声明
int init_alsa(void);
void cleanup_alsa(void);


#endif
