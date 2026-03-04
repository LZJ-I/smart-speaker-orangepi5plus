#ifndef __ALSA_OUTPUT_H__
#define __ALSA_OUTPUT_H__


#include <stdint.h>
#include <stdio.h>
#include <alsa/asoundlib.h>


// #define RECORD_DEVICE   "audiocodec"    // 播放设备（根据实际情况调整）
#define RECORD_DEVICE   "default"
#define PERIOD_SIZE     256
#define CHANNELS        2

extern snd_pcm_t *g_pcm_handle;
extern unsigned int g_alsa_playback_rate;

// 查找指定播放设备的ALSA设备名称
int get_audio_playback_device(const char* target_device, char* result);

// 初始化alsa播放设备
int init_alsa_output(unsigned int rate, char* playback_device_name);

// 清理alsa播放设备
void cleanup_alsa_output(void);

int pcm_write_all(snd_pcm_t *pcm, const int16_t *buf, snd_pcm_uframes_t frames);
void pcm_write_silence(snd_pcm_t *pcm);

#endif
