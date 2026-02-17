// 参考的example:
// - sense-voice-c-api.c: SenseVoice离线识别配置
// - vad-sense-voice-c-api.c: VAD + SenseVoice集成
#define LOG_LEVEL 4
#include "../../debug_log.h"
#include "sherpa_asr.h"
#include "../common/mysamplerate.h"

#include <unistd.h>

#define TAG "ASR"

#ifdef KWS_TEST_MODE
#define MODEL_PREFIX "../../3rdparty"
#else
#define MODEL_PREFIX "../3rdparty"
#endif

const SherpaOnnxVoiceActivityDetector *g_vad = NULL;
const SherpaOnnxOfflineRecognizer *g_offline_recognizer = NULL;
const SherpaOnnxCircularBuffer *g_audio_buffer = NULL;

#define VAD_WINDOW_SIZE 512
#define BUFFER_CAPACITY 480000

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
    vad_config.silero_vad.threshold = 0.25;
    vad_config.silero_vad.min_silence_duration = 0.5;
    vad_config.silero_vad.min_speech_duration = 0.5;
    vad_config.silero_vad.max_speech_duration = 10;
    vad_config.silero_vad.window_size = VAD_WINDOW_SIZE;
    vad_config.sample_rate = MODEL_SAMPLE_RATE;
    vad_config.num_threads = 1;
    vad_config.debug = 0;

    g_vad = SherpaOnnxCreateVoiceActivityDetector(&vad_config, 30);
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
        }

        i += chunk_size;
    }

    return ret;
}

void cleanup_sherpa_asr(void)
{
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
