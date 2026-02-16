#ifndef __SHERPA_KWS_H__
#define __SHERPA_KWS_H__

#include  <stdio.h>
#include "sherpa-onnx/c-api/c-api.h"
#include "alsa.h"

// 全局识别器指针（供KWS识别使用）
extern const SherpaOnnxKeywordSpotter *g_kws_spotter;
// 全局音频流句柄（供KWS识别使用）
extern const SherpaOnnxOnlineStream *g_kws_stream;

// 离线模式与在线模式
enum OnlineMode{
    ONLINE_MODE_NO,  // 离线模式
    ONLINE_MODE_YES  // 在线模式
};

// 初始化sherpa-onnx-kws资源
int init_sherpa_kws(void);
// KWS 处理并识别结果，返回1表示找到关键词，0表示没有，keyword_buf存储关键词
int process_kws_result(float *model_audio, int model_frames, char *keyword_buf, int buf_size);
// 清理sherpa-onnx-kws资源
void cleanup_sherpa_kws(void);


#endif
