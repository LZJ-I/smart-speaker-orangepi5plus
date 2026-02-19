#define LOG_LEVEL 4
#include "../../debug_log.h"
#include "sherpa_tts.h"

#include <stdlib.h>
#include <string.h>

const SherpaOnnxOfflineTts *g_tts = NULL;
unsigned int g_tts_sample_rate = 0;

#define TAG "TTS"

#ifdef KWS_TEST_MODE
#define MODEL_PREFIX "../../../3rdparty"
#else
#define MODEL_PREFIX "../../3rdparty"
#endif

typedef struct {
    TTSCallback user_callback;
    TTSProgressCallback user_progress_callback;
    void *user_arg;
} TTSCallbackContext;

static TTSCallbackContext g_callback_context = {0};

static int32_t internal_tts_callback(const float *samples, int32_t n, void *arg)
{
    if (g_callback_context.user_callback) {
        return g_callback_context.user_callback(samples, n, g_callback_context.user_arg);
    }
    return 1;
}

static int32_t internal_tts_progress_callback(const float *samples, int32_t n, float progress, void *arg)
{
    if (g_callback_context.user_progress_callback) {
        return g_callback_context.user_progress_callback(samples, n, progress, g_callback_context.user_arg);
    }
    return 1;
}

static int init_sherpa_tts_internal(int max_num_sentences)
{
    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));

    config.rule_fsts = MODEL_PREFIX "/model/tts/matcha-icefall-zh-en/phone-zh.fst,"
                        MODEL_PREFIX "/model/tts/matcha-icefall-zh-en/date-zh.fst,"
                        MODEL_PREFIX "/model/tts/matcha-icefall-zh-en/number-zh.fst";

    config.model.matcha.acoustic_model = MODEL_PREFIX "/model/tts/matcha-icefall-zh-en/model-steps-3.onnx";
    config.model.matcha.vocoder = MODEL_PREFIX "/model/tts/matcha-icefall-zh-en/vocos-16khz-univ.onnx";
    config.model.matcha.lexicon = MODEL_PREFIX "/model/tts/matcha-icefall-zh-en/lexicon.txt";
    config.model.matcha.tokens = MODEL_PREFIX "/model/tts/matcha-icefall-zh-en/tokens.txt";
    config.model.matcha.dict_dir = MODEL_PREFIX "/model/tts/matcha-icefall-zh-en/espeak-ng-data";
    config.model.matcha.data_dir = MODEL_PREFIX "/model/tts/matcha-icefall-zh-en/espeak-ng-data";

    config.model.num_threads = 8;
    config.model.provider = "cpu";
    config.model.debug = 0;
    config.max_num_sentences = max_num_sentences;

    g_tts = SherpaOnnxCreateOfflineTts(&config);
    if (g_tts == NULL) {
        LOGE(TAG, "创建TTS实例失败!");
        return -1;
    }

    g_tts_sample_rate = SherpaOnnxOfflineTtsSampleRate(g_tts);
    LOGI(TAG, "TTS模型加载完成，采样率: %d Hz, max_num_sentences: %d", g_tts_sample_rate, max_num_sentences);

    return 0;
}

int init_sherpa_tts(void)
{
    return init_sherpa_tts_internal(0);
}

int init_sherpa_tts_with_chunk_size(int max_num_sentences)
{
    return init_sherpa_tts_internal(max_num_sentences);
}

int generate_tts_audio(const char *text, const char *output_filename)
{
    if (g_tts == NULL) {
        LOGE(TAG, "TTS实例未初始化!");
        return -1;
    }

    const float speed = 1.0f;
    const SherpaOnnxGeneratedAudio *audio = SherpaOnnxOfflineTtsGenerate(g_tts, text, 0, speed);
    if (audio == NULL) {
        LOGE(TAG, "生成音频失败!");
        return -1;
    }

    SherpaOnnxWriteWave(audio->samples, audio->n, audio->sample_rate, output_filename);
    LOGI(TAG, "音频生成成功，保存到: %s", output_filename);
    LOGI(TAG, "输入文本: %s", text);
    LOGI(TAG, "采样率: %d Hz, 样本数: %d",
            audio->sample_rate, audio->n);

    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);

    return 0;
}

int generate_tts_audio_full(const char *text, float **samples, int32_t *n, uint32_t *sample_rate)
{
    if (g_tts == NULL) {
        LOGE(TAG, "TTS实例未初始化!");
        return -1;
    }

    const float speed = 1.0f;
    const SherpaOnnxGeneratedAudio *audio = SherpaOnnxOfflineTtsGenerate(g_tts, text, 0, speed);
    if (audio == NULL) {
        LOGE(TAG, "生成音频失败!");
        return -1;
    }

    *samples = (float*)malloc(audio->n * sizeof(float));
    if (*samples == NULL) {
        LOGE(TAG, "内存分配失败!");
        SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        return -1;
    }

    memcpy(*samples, audio->samples, audio->n * sizeof(float));
    *n = audio->n;
    *sample_rate = audio->sample_rate;

    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);

    return 0;
}

void destroy_generated_audio(float *samples)
{
    if (samples != NULL) {
        free(samples);
    }
}

int generate_tts_audio_with_callback(const char *text, TTSCallback callback, void *arg)
{
    if (g_tts == NULL) {
        LOGE(TAG, "TTS实例未初始化!");
        return -1;
    }

    const float speed = 1.0f;
    g_callback_context.user_callback = callback;
    g_callback_context.user_arg = arg;

    const SherpaOnnxGeneratedAudio *audio = SherpaOnnxOfflineTtsGenerateWithCallbackWithArg(
        g_tts, text, 0, speed, internal_tts_callback, NULL);

    if (audio == NULL) {
        LOGE(TAG, "生成音频失败!");
        return -1;
    }

    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    g_callback_context.user_callback = NULL;
    g_callback_context.user_arg = NULL;

    return 0;
}

int generate_tts_audio_with_progress_callback(const char *text, TTSProgressCallback callback, void *arg)
{
    if (g_tts == NULL) {
        LOGE(TAG, "TTS实例未初始化!");
        return -1;
    }

    const float speed = 1.0f;
    g_callback_context.user_progress_callback = callback;
    g_callback_context.user_arg = arg;

    const SherpaOnnxGeneratedAudio *audio = SherpaOnnxOfflineTtsGenerateWithProgressCallbackWithArg(
        g_tts, text, 0, speed, internal_tts_progress_callback, NULL);

    if (audio == NULL) {
        LOGE(TAG, "生成音频失败!");
        return -1;
    }

    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    g_callback_context.user_progress_callback = NULL;
    g_callback_context.user_arg = NULL;

    return 0;
}

void cleanup_sherpa_tts(void)
{
    if (g_tts != NULL) {
        SherpaOnnxDestroyOfflineTts(g_tts);
        g_tts = NULL;
    }
    LOGI(TAG, "TTS模型资源清理完成");
}
