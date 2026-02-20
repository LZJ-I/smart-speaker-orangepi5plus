// 参考: c-api-examples/kws-c-api.c
#define LOG_LEVEL 4
#include "../../debug_log.h"
#include "sherpa_kws.h"
#include <unistd.h>
#include <string.h>

const SherpaOnnxKeywordSpotter *g_kws_spotter = NULL;
const SherpaOnnxOnlineStream *g_kws_stream = NULL;

#define TAG "KWS"

#ifdef KWS_TEST_MODE
#define MODEL_PREFIX "../../../3rdparty"
#define KEYWORDS_FILE "../../keywords.txt"
#elif defined(PROCESS_MODE)
#define MODEL_PREFIX "./3rdparty"
#define KEYWORDS_FILE "./voice-assistant/keywords.txt"
#else
#define MODEL_PREFIX "../3rdparty"
#define KEYWORDS_FILE "./keywords.txt"
#endif

int init_sherpa_kws(void)
{
    SherpaOnnxKeywordSpotterConfig config;

    memset(&config, 0, sizeof(config));
    config.model_config.transducer.encoder =
        MODEL_PREFIX "/model/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/encoder-epoch-13-avg-2-chunk-16-left-64.onnx";

    config.model_config.transducer.decoder =
        MODEL_PREFIX "/model/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/decoder-epoch-13-avg-2-chunk-16-left-64.onnx";

    config.model_config.transducer.joiner =
        MODEL_PREFIX "/model/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/joiner-epoch-13-avg-2-chunk-16-left-64.int8.onnx";

    config.model_config.tokens =
        MODEL_PREFIX "/model/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/tokens.txt";

    config.model_config.provider = "cpu";
    config.model_config.num_threads = 4;
    config.model_config.debug = 0;

    config.keywords_file = KEYWORDS_FILE;

    g_kws_spotter = SherpaOnnxCreateKeywordSpotter(&config);
    if (!g_kws_spotter) {
      LOGE(TAG, "SherpaOnnxCreateKeywordSpotter 创建kws识别器失败");
      return -1;
    }

    g_kws_stream = SherpaOnnxCreateKeywordStream(g_kws_spotter);
    if (!g_kws_stream) {
        LOGE(TAG, "SherpaOnnxCreateKeywordStream 创建kws音频流失败");
        return -1;
    }

    return 0;
}

int process_kws_result(float *model_audio, int model_frames, char *keyword_buf, int buf_size)
{
    int found = 0;
    SherpaOnnxOnlineStreamAcceptWaveform(g_kws_stream, MODEL_SAMPLE_RATE, model_audio,
        model_frames * CHANNELS);

    while (SherpaOnnxIsKeywordStreamReady(g_kws_spotter, g_kws_stream)) {
        SherpaOnnxDecodeKeywordStream(g_kws_spotter, g_kws_stream);
        const SherpaOnnxKeywordResult *r = SherpaOnnxGetKeywordResult(g_kws_spotter, g_kws_stream);
        if (r && r->json && strlen(r->keyword)) {
            if (keyword_buf && buf_size > 0) {
                strncpy(keyword_buf, r->keyword, buf_size - 1);
                keyword_buf[buf_size - 1] = '\0';
            }
            found = 1;
            SherpaOnnxResetKeywordStream(g_kws_spotter, g_kws_stream);
        }
        if(r)
        {
            SherpaOnnxDestroyKeywordResult(r);
        }
    }

    return found;
}

void cleanup_sherpa_kws(void)
{
    if (g_kws_stream) {
        SherpaOnnxDestroyOnlineStream(g_kws_stream);
        g_kws_stream = NULL;
    }
    if (g_kws_spotter) {
        SherpaOnnxDestroyKeywordSpotter(g_kws_spotter);
        g_kws_spotter = NULL;
    }
    LOGI(TAG, "KWS模型资源清理完成");
}
