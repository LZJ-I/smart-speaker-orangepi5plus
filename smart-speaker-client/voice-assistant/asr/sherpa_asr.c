// 参考的example:
// - sense-voice-c-api.c: SenseVoice离线识别配置
// - vad-sense-voice-c-api.c: VAD + SenseVoice集成
#define LOG_LEVEL 4
#include "../../debug_log.h"
#include "sherpa_asr.h"
#include "../common/mysamplerate.h"

#include <unistd.h>
#include <time.h>

#define TAG "ASR"

#ifdef KWS_TEST_MODE
#define MODEL_PREFIX "../../../3rdparty"
#else
#define MODEL_PREFIX "../3rdparty"
#endif

const SherpaOnnxVoiceActivityDetector *g_vad = NULL;
const SherpaOnnxOfflineRecognizer *g_offline_recognizer = NULL;
const SherpaOnnxCircularBuffer *g_audio_buffer = NULL;

#define VAD_WINDOW_SIZE 512
#define BUFFER_CAPACITY 480000

// 新增：用于模拟流式识别的变量
static float *g_current_audio = NULL;
static int32_t g_current_audio_size = 0;
static int32_t g_current_audio_capacity = 0;
static int32_t g_speech_started = 0;
static struct timespec g_last_recognize_time = {0, 0};

static void ensure_audio_capacity(int32_t required_size) {
    if (required_size > g_current_audio_capacity) {
        int32_t new_capacity = (required_size > BUFFER_CAPACITY) ? BUFFER_CAPACITY : required_size * 2;
        float *new_audio = (float *)realloc(g_current_audio, new_capacity * sizeof(float));
        if (new_audio == NULL) {
            LOGE(TAG, "内存分配失败！");
            return;
        }
        g_current_audio = new_audio;
        g_current_audio_capacity = new_capacity;
    }
}

static double get_elapsed_ms(struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = (now.tv_sec - start->tv_sec) * 1000.0;
    elapsed += (now.tv_nsec - start->tv_nsec) / 1000000.0;
    return elapsed;
}

int init_sherpa_asr(void)
{
    SherpaOnnxOfflineSenseVoiceModelConfig sense_voice_config;
    memset(&sense_voice_config, 0, sizeof(sense_voice_config));
    sense_voice_config.model = MODEL_PREFIX "/model/asr/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/model.int8.onnx";
    sense_voice_config.language = "auto";
    sense_voice_config.use_itn = 1;

    SherpaOnnxOfflineModelConfig offline_model_config;
    memset(&offline_model_config, 0, sizeof(offline_model_config));
    offline_model_config.debug = 0;
    offline_model_config.num_threads = 8;
    offline_model_config.provider = "cpu";
    offline_model_config.tokens = MODEL_PREFIX "/model/asr/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/tokens.txt";
    offline_model_config.sense_voice = sense_voice_config;

    SherpaOnnxOfflineRecognizerConfig recognizer_config;
    memset(&recognizer_config, 0, sizeof(recognizer_config));
    recognizer_config.decoding_method = "greedy_search";
    recognizer_config.model_config = offline_model_config;

    g_offline_recognizer = SherpaOnnxCreateOfflineRecognizer(&recognizer_config);
    if (!g_offline_recognizer)
    {
        LOGE(TAG, "创建离线识别器失败!");
        return -1;
    }

    SherpaOnnxVadModelConfig vad_config;
    memset(&vad_config, 0, sizeof(vad_config));
    vad_config.silero_vad.model = MODEL_PREFIX "/model/asr/silero_vad.onnx";
    vad_config.silero_vad.threshold = 0.5;
    vad_config.silero_vad.min_silence_duration = 0.1;
    vad_config.silero_vad.min_speech_duration = 0.25;
    vad_config.silero_vad.max_speech_duration = 8;
    vad_config.silero_vad.window_size = VAD_WINDOW_SIZE;
    vad_config.sample_rate = MODEL_SAMPLE_RATE;
    vad_config.num_threads = 1;
    vad_config.debug = 0;

    g_vad = SherpaOnnxCreateVoiceActivityDetector(&vad_config, 20);
    if (!g_vad)
    {
        LOGE(TAG, "创建VAD失败!");
        SherpaOnnxDestroyOfflineRecognizer(g_offline_recognizer);
        return -1;
    }

    g_audio_buffer = SherpaOnnxCreateCircularBuffer(BUFFER_CAPACITY);
    if (!g_audio_buffer)
    {
        LOGE(TAG, "创建音频缓冲区失败!");
        SherpaOnnxDestroyVoiceActivityDetector(g_vad);
        SherpaOnnxDestroyOfflineRecognizer(g_offline_recognizer);
        return -1;
    }

    // 初始化用于模拟流式识别的变量
    g_current_audio = NULL;
    g_current_audio_size = 0;
    g_current_audio_capacity = 0;
    g_speech_started = 0;

    return 0;
}

int process_asr_result(float *model_audio, int model_frames)
{
    int ret = 1;
    int total_samples = model_frames * CHANNELS;
    int i = 0;

    while (i < total_samples)
    {
        int remaining = total_samples - i;
        int chunk_size = (remaining < VAD_WINDOW_SIZE) ? remaining : VAD_WINDOW_SIZE;

        SherpaOnnxVoiceActivityDetectorAcceptWaveform(g_vad, model_audio + i, chunk_size);
        SherpaOnnxCircularBufferPush(g_audio_buffer, model_audio + i, chunk_size);

        // 保存到用于模拟流式的缓冲区
        ensure_audio_capacity(g_current_audio_size + chunk_size);
        if (g_current_audio) {
            memcpy(g_current_audio + g_current_audio_size, model_audio + i, chunk_size * sizeof(float));
            g_current_audio_size += chunk_size;
        }

        // 检查是否开始检测到语音
        if (!g_speech_started && SherpaOnnxVoiceActivityDetectorDetected(g_vad)) {
            g_speech_started = 1;
            clock_gettime(CLOCK_MONOTONIC, &g_last_recognize_time);
            LOGD(TAG, "检测到语音开始");
        }

        // 如果已经开始语音，并且超过 0.2 秒，就进行一次模拟流式识别
        if (g_speech_started && get_elapsed_ms(&g_last_recognize_time) > 200) {
            const SherpaOnnxOfflineStream *stream = SherpaOnnxCreateOfflineStream(g_offline_recognizer);
            if (stream && g_current_audio && g_current_audio_size > 0) {
                SherpaOnnxAcceptWaveformOffline(stream, MODEL_SAMPLE_RATE, g_current_audio, g_current_audio_size);
                SherpaOnnxDecodeOfflineStream(g_offline_recognizer, stream);

                const SherpaOnnxOfflineRecognizerResult *result = SherpaOnnxGetOfflineStreamResult(stream);
                if (result && strlen(result->text) > 0) {
                    LOGI(TAG, "识别中: %s", result->text);
                }
                SherpaOnnxDestroyOfflineRecognizerResult(result);
            }
            if (stream) {
                SherpaOnnxDestroyOfflineStream(stream);
            }
            clock_gettime(CLOCK_MONOTONIC, &g_last_recognize_time);
        }

        // 如果 buffer 太大，清理旧数据（只保留最近 10*VAD_WINDOW_SIZE）
        if (!g_speech_started && g_current_audio_size > 10 * VAD_WINDOW_SIZE) {
            int32_t keep_samples = 10 * VAD_WINDOW_SIZE;
            if (g_current_audio && keep_samples > 0) {
                memmove(g_current_audio, g_current_audio + (g_current_audio_size - keep_samples), keep_samples * sizeof(float));
            }
            g_current_audio_size = keep_samples;
        }

        while (!SherpaOnnxVoiceActivityDetectorEmpty(g_vad))
        {
            const SherpaOnnxSpeechSegment *segment = SherpaOnnxVoiceActivityDetectorFront(g_vad);

            const float *samples = SherpaOnnxCircularBufferGet(g_audio_buffer, segment->start, segment->n);
            if (samples)
            {
                const SherpaOnnxOfflineStream *stream = SherpaOnnxCreateOfflineStream(g_offline_recognizer);
                SherpaOnnxAcceptWaveformOffline(stream, MODEL_SAMPLE_RATE, samples, segment->n);
                SherpaOnnxDecodeOfflineStream(g_offline_recognizer, stream);

                const SherpaOnnxOfflineRecognizerResult *result = SherpaOnnxGetOfflineStreamResult(stream);
                if (result && strlen(result->text) > 0)
                {
                    LOGI(TAG, "识别结果: %s", result->text);
                    ret = 0;
                }

                SherpaOnnxDestroyOfflineRecognizerResult(result);
                SherpaOnnxDestroyOfflineStream(stream);
                SherpaOnnxCircularBufferFree(samples);
            }

            SherpaOnnxDestroySpeechSegment(segment);
            SherpaOnnxVoiceActivityDetectorPop(g_vad);

            // 语音片段处理完毕，重置
            if (g_current_audio) {
                free(g_current_audio);
                g_current_audio = NULL;
            }
            g_current_audio_size = 0;
            g_current_audio_capacity = 0;
            g_speech_started = 0;
        }

        i += chunk_size;
    }

    return ret;
}

void cleanup_sherpa_asr(void)
{
    if (g_current_audio != NULL)
    {
        free(g_current_audio);
        g_current_audio = NULL;
    }
    g_current_audio_size = 0;
    g_current_audio_capacity = 0;

    if (g_audio_buffer != NULL)
    {
        SherpaOnnxDestroyCircularBuffer(g_audio_buffer);
        g_audio_buffer = NULL;
    }
    if (g_vad != NULL)
    {
        SherpaOnnxDestroyVoiceActivityDetector(g_vad);
        g_vad = NULL;
    }
    if (g_offline_recognizer != NULL)
    {
        SherpaOnnxDestroyOfflineRecognizer(g_offline_recognizer);
        g_offline_recognizer = NULL;
    }
    LOGI(TAG, "ASR模型资源清理完成");
}
