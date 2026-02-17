
#include "tts.h"

// 全局tts实例指针
const SherpaOnnxOfflineTts *g_tts = NULL;

unsigned int g_tts_sample_rate = 0;

int init_sherpa_tts(void)
{
    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));   

    // 规则文件（保持不变）
    config.rule_fsts = "/root/sherpa/2-文本转语音合成/vits-icefall-zh-aishell3/phone.fst,"
                        "/root/sherpa/2-文本转语音合成/vits-icefall-zh-aishell3/date.fst,"
                        "/root/sherpa/2-文本转语音合成/vits-icefall-zh-aishell3/new_heteronym.fst,"
                        "/root/sherpa/2-文本转语音合成/vits-icefall-zh-aishell3/number.fst";
    
    // 模型路径（保持不变）
    config.model.vits.model = "/root/sherpa/2-文本转语音合成/vits-icefall-zh-aishell3/model.onnx";
    config.model.vits.lexicon = "/root/sherpa/2-文本转语音合成/vits-icefall-zh-aishell3/lexicon.txt";
    config.model.vits.tokens = "/root/sherpa/2-文本转语音合成/vits-icefall-zh-aishell3/tokens.txt";

    
    // 优化参数
    // config.model.vits.noise_scale = 0.667;    // 提升声音生动性和变异性
    // config.model.vits.noise_scale_w = 0.1;    // 增强韵律自然度
    // config.model.vits.length_scale = 1.05;    // 控制语速，稍慢更自然
    
    // 静音控制优化
    // config.silence_scale = 0.4;               // 优化句子间停顿
    // config.max_num_sentences = 5;             // 增加批量处理能力
    
    // 性能优化
    config.model.num_threads = 4;             // 线程数提升性能
    config.model.provider = "cpu";

    // 创建tts实例
    g_tts = SherpaOnnxCreateOfflineTts(&config);
    if(g_tts == NULL){
        fprintf(stderr, "[ERROR] 创建tts实例失败!\n");
        return -1;
    }

    // 获取模型输出数据的采样频率
    g_tts_sample_rate = SherpaOnnxOfflineTtsSampleRate(g_tts);
    printf("tts模型输出数据的采样频率为: %d\n", g_tts_sample_rate);

    return 0;
}



