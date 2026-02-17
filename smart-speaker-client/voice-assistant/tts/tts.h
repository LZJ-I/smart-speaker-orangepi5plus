#ifndef __TTL_H
#define __TTL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sherpa-onnx/c-api/c-api.h"

// 全局tts实例指针
extern const SherpaOnnxOfflineTts *g_tts;

// 全局tts模型输出数据的采样频率
extern unsigned int g_tts_sample_rate;

// 初始化tts模型
int init_sherpa_tts(void);

#endif
