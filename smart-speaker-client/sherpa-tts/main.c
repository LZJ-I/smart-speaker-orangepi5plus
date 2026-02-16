#include <stdio.h>
#include "tts.h"
#include <stdlib.h>
#include "alsa_output.h"
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "sherpa-onnx/c-api/c-api.h"

// 运行标志位
int running = 1;    
//用于存储处理后的样本
int16_t *processed_samples = NULL;
// 全局变量：TTS 输出设备文件描述符
int g_tts_fd = -1;
// TTS 输入设备文件描述符地址
#define TTS_FIFO_PATH "../fifo/tts_fifo"
int64_t sid = 84;          // 说话人ID
float speed = 1.0;       // 语速控制
// 是否播放标志位
int g_play_flag = 1;

// 声明回调函数
static int32_t tts_callback(const float *samples, int32_t n);

// 处理ctrl+c信号
void sigint_handler(int signum)
{
    printf("\n[INFO] 收到ctrl+c信号，程序退出\n");
    if(processed_samples != NULL)
    {
        free(processed_samples);
    }
    snd_pcm_close(g_pcm_handle);    // 关闭设备
    SherpaOnnxDestroyOfflineTts(g_tts);  //关闭模型
    printf("清理TTS实例成功\n");
    running = 0;
    exit(0);
}

// 更换说话人音色
static void tts_change_voice_handler(int signum)
{
    printf("\n[INFO] 收到SIGUSR2信号，更换说话人音色\n");
    // 更换说话人音色
    sid = (sid + 17) % 174;
    printf("当前说话人ID: %ld\n", sid);
}

// 终止当前正在合成的语音
static void tts_end_play_handler(int signum)
{
    printf("\n[INFO] 收到SIGUSR1信号，终止当前正在合成的语音\n");
    // 设置遍历，使其在回调函数中 结束合成
    g_play_flag = 0;
    // 清空 缓冲区
    snd_pcm_drop(g_pcm_handle);
}

int main(int argc, char const *argv[])
{
    // 注册ctrl+c
    signal(SIGINT, sigint_handler);

    // 注册SIGUSR1信号处理函数(终止当前正在合成的语音)
    signal(SIGUSR1, tts_end_play_handler);

    // 注册SIGUSR2信号处理函数(更换说话人音色)
    signal(SIGUSR2, tts_change_voice_handler);

    // 初始化大模型
    if(init_sherpa_tts() != 0){
        fprintf(stderr, "[ERROR] 初始化语音合成模型失败!\n");
        return 0;
    }
    printf("初始化语音合成模型成功\n");

    // 获取默认播放设备名称
    char playback_device[256];
    if(get_audio_playback_device(RECORD_DEVICE, playback_device) != 0){
        fprintf(stderr, "[ERROR] 获取播放设备失败!\n");
        return 0;
    }
    printf("找到播放设备: %s\n", playback_device);

    // 初始化 输出设备（大模型采样率为8000）
    if(init_alsa_output(g_tts_sample_rate, playback_device) != 0){
        fprintf(stderr, "[ERROR] 初始化输出设备失败!\n");
        return 0;
    }
    printf("初始化输出设备成功\n");

    // 打开管道(只读)
    g_tts_fd = open(TTS_FIFO_PATH, O_RDONLY);
    if(g_tts_fd == -1){
        fprintf(stderr, "[ERROR] 打开TTS管道失败!\n");
        return 0;
    }
    printf("打开TTS管道成功\n");


    // const char * text = "今天是2025年10月1日，天气有点干。记得多喝热水。";

    // sid = 30;          // 说话人ID（1-174）在spekers里看
    // speed = 0.9;       // 语速控制
    
    char text[512];
    while(running)
    {
        // 从管道读取数据
        int ret = read(g_tts_fd, text, sizeof(text));
        if(ret < 0){
            fprintf(stderr, "[ERROR] 读取TTS管道失败!\n");
            continue;
        }
        else if(ret == 0){
            fprintf(stderr, "[WARNING]对端关闭！！\n");
            running = 0;
            continue;
        }
        text[ret] = '\0';
        /*
            我们只需要将需要合成的文本传入大模型即可，大模型每合成一段，就会调用回调函数tts_callback。（也就是说可以写很多数据）
        */
        // 准备PCM设备（将其状态设置为READY）
        if(snd_pcm_prepare(g_pcm_handle) < 0)
        {
            fprintf(stderr, "[ERROR] 准备设备失败\n");
            running = 0;
        }

        g_play_flag = 1;    // 重置播放标志位

        // 调用大模型进行合成
        SherpaOnnxOfflineTtsGenerateWithCallback(g_tts, text, sid, speed, tts_callback);

        // 等待缓冲区数据播放完毕
        if(snd_pcm_drain(g_pcm_handle) < 0)  // 状态会变为SETUP 
        {
            fprintf(stderr, "[WARNING] 等待缓冲区数据播放完毕失败\n");
            continue;
        }

        // 清空管道缓冲区
        memset(text, 0, sizeof(text));
    }

    // 清理 TTS 实例
    snd_pcm_close(g_pcm_handle);
    SherpaOnnxDestroyOfflineTts(g_tts);
    printf("清理TTS实例成功\n");
    return 0;
}

// 回调函数
static int32_t tts_callback(const float *samples, int32_t n)
{
    /*
        大模型输出的数据为float32类型
        而输出设备需要int16_t类型。（也就是*32767）
    */
    if(!g_play_flag)
    {
        return 0;   // 终止播放
    }
    // 开辟堆空间，用于存储 处理后的样本
    processed_samples = malloc(n * sizeof(int16_t));
    if(processed_samples == NULL){
        fprintf(stderr, "[ERROR] 分配内存失败!\n");
        return 0;
    }
    float t;
    // 遍历所有样本，将float32转换为int16_t
    for(int32_t i = 0; i < n; i++)
    {
        t = samples[i];
        // 限制 样本值在[-1, 1]之间
        if(t < -1.0) t = -1.0;
        else if(t > 1.0) t = 1.0;
        // 转换为int16_t类型
        processed_samples[i] = (int16_t)(t * 32767);
    }
    // 写入音频数据到输出设备
    int ret = snd_pcm_writei(g_pcm_handle, processed_samples, n);
    if(ret == -EPIPE){  // 处理缓冲区溢出(欠载)
        snd_pcm_prepare(g_pcm_handle);  // 重新准备PCM设备
        // 重新写入
        int ret = snd_pcm_writei(g_pcm_handle, processed_samples, n);
        if(ret < 0){
            fprintf(stderr, "[ERROR] 缓冲区欠载，重写失败!\n");
            return 0;
        }
    }
    else if(ret < 0)
    {
        fprintf(stderr, "[ERROR] 缓冲区欠载，重写失败!\n");
        return 0;
    }

    // 清理 处理后的样本内存
    free(processed_samples);


// If the callback returns 0, then it stops generating
// If the callback returns 1, then it keeps generating

    return g_play_flag;
}

