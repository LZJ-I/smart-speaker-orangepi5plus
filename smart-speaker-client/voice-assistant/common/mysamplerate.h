#ifndef __MYSAMPLERATE_H__
#define __MYSAMPLERATE_H__

#include <samplerate.h>
#include <stdint.h>
#include "alsa.h"

// 1. 初始化重采样器（需在ALSA初始化后调用，因依赖g_actual_rate）
int init_resampler(void);

// 2. 核心重采样函数：将ALSA的int16_t数据转换为模型所需的float数据
// input：ALSA读取的int16_t原始数据
// input_frames：输入数据帧数
// output：输出float格式数据的指针（模块内分配，外部无需释放）
// output_frames：输出数据帧数（返回值）
int resample_audio(const int16_t *input, int input_frames, float **output, int *output_frames);

// 3. 清理重采样资源（释放缓冲区和重采样器句柄）
void cleanup_resampler(void);

#endif
