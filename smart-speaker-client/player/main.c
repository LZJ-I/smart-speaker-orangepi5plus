#define LOG_LEVEL 4
#include "../debug_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "select.h"
#include "link.h"
#include "shm.h"
#include "socket.h"
#include "device.h"
#include "player.h"

#define TAG "PLAYER-MAIN"

int main(int argc, char const *argv[])
{
    system("./init.sh");    // 删除可能存在的共享内存

    // 注册 更新音乐列表信号函数（孙进程发出）
    signal(SIGUSR1, socket_update_music);
    // 注册信号处理函数， 用于处理定时器到期信号
	signal(SIGALRM, button_handler);

    if(select_init() != 0)
    {
        LOGE(TAG, "select初始化失败");
        return -1;
    }
    LOGI(TAG, "select初始化成功！");

    // 打开并监听asr模型的管道文件
    if(init_asr_fifo() != 0)
    {
        LOGE(TAG, "打开asr语音识别管道失败");
        return -1;
    }
    LOGI(TAG, "打开asr语音识别管道成功！");

    // 打开并监听kws模型的管道文件
    if(init_kws_fifo() != 0)
    {
        LOGE(TAG, "打开kws关键词识别管道失败");
        return -1;
    }
    LOGI(TAG, "打开kws关键词识别管道成功！");

    // // 打开tts模型的管道文件
    // if(init_tts_fifo() != 0)
    // {
    //     LOGE(TAG, "打开tts语音合成管道失败");
    //     return -1;
    // }
    // LOGI(TAG, "打开tts语音合成管道成功！");


    if(link_init() != 0)
    {
        LOGE(TAG, "链表初始化失败");
        return -1;
    }
    LOGI(TAG, "链表初始化成功！");

    if(shm_sem_init() != 0)
    {
        LOGE(TAG, "信号量初始化失败");
        return -1;
    }
    LOGI(TAG, "信号量初始化成功！");

    if(shm_init() != 0)
    {
        LOGE(TAG, "共享内存初始化失败");
        return -1;
    }
    LOGI(TAG, "共享内存初始化成功！");

   // device_set_volume(DEFAULT_VOLUME);  // 默认音量为60%

    // // 初始化网络
    // if(socket_init() == -1)
    // {
    //     LOGW(TAG, "网络初始化失败，进入离线模式");
    //     if(0 != player_switch_offline_mode())
    //     {
    //         LOGE(TAG, "离线模式进入失败");
    //     }
    // }
    // else{
    //     LOGI(TAG, "网络初始化成功！");
    //     // 获取初始音乐列表
    //     socket_get_music("其他");   
    //     // 打印音乐列表(调试)
    //     link_traverse_list(NULL);    
    // }

    // // 初始化按键
    // if(key_init() != 0)
    // {
    //     LOGE(TAG, "按键初始化失败");
    // }
    // LOGI(TAG, "按键初始化成功！");
   
    select_run();   // 启动事件监听

    return 0;
}
