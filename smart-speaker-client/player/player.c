#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <wait.h>   // waitpid()
#include <errno.h>
#include <sys/select.h>
#include <sys/mount.h>
#include <errno.h>

#include "shm.h"
#include "player.h"
#include "link.h"
#include "socket.h"
#include "select.h"
#include "../debug_log.h"

#define TAG "PLAYER"

int g_current_state = PLAY_STATE_STOP;            // 当前播放状态
int g_current_suspend = PLAY_SUSPEND_NO;          // 当前是否处于 继续播放(未暂停)
int g_current_online_mode = ONLINE_MODE_NO;         // 当前是否处于在线模式

int g_asr_fd = -1;  // asr语音识别管道文件描述符
int g_kws_fd = -1;  // kws关键词识别管道文件描述符
int g_tts_fd = -1;  // tts语音合成管道文件描述符










/**
 * @brief 分离音乐名中的歌手和歌曲名（格式：歌手/歌曲名 或 歌曲名）
 * @param music_name 输入的音乐名字符串（支持 "歌手/歌曲名" 或直接 "歌曲名"）
 * @param shm_data 共享内存数据指针，用于存储分离后的歌手名和歌曲名
 * @return int 0-分离成功，-1-分离失败（参数无效）
 */
static int split_music_info(const char *music_name, Shm_Data *shm_data) {
    // 参数合法性检查（避免空指针访问）
    if (music_name == NULL || shm_data == NULL) {
        LOGE(TAG, "split_music_info: 输入参数为NULL");
        return -1;
    }
    const char *sep = strchr(music_name, '/');  // 查找分隔符'/'
    if (sep != NULL) {
        // 存在分隔符，分离歌手名和歌曲名
        size_t singer_len = sep - music_name;  // 歌手名字符长度

        // 拷贝歌手名
        if (singer_len > 0 && singer_len < sizeof(shm_data->current_singer)) {
            strncpy(shm_data->current_singer, music_name, singer_len);
            shm_data->current_singer[singer_len] = '\0';  // 手动添加字符串结束符
        } else {
            strncpy(shm_data->current_singer, "未知歌手", sizeof(shm_data->current_singer) - 1);
            shm_data->current_singer[sizeof(shm_data->current_singer) - 1] = '\0';
        }

        // 拷贝歌曲名
        strncpy(shm_data->current_music, sep + 1, sizeof(shm_data->current_music) - 1);
        shm_data->current_music[sizeof(shm_data->current_music) - 1] = '\0';
    } else {
        // 不存在分隔符，歌曲名直接使用原字符串，歌手名设为"未知歌手"
        strncpy(shm_data->current_music, music_name, sizeof(shm_data->current_music) - 1);
        shm_data->current_music[sizeof(shm_data->current_music) - 1] = '\0';
        
        strncpy(shm_data->current_singer, "未知歌手", sizeof(shm_data->current_singer) - 1);
        shm_data->current_singer[sizeof(shm_data->current_singer) - 1] = '\0';
    }

    return 0;  // 分离成功
}


/**
 * @brief 播放指定名称的音乐（支持在线/离线模式）
 * @param name 歌曲名称（含后缀，如"song.mp3"），非NULL
 * @return int 执行失败返回-1（execv成功时不会返回，进程已替换）
 */
int play_music(const char* singer, const char* name) {
    // 1. 参数合法性校验
    if (name == NULL || strlen(name) == 0) {
        LOGE(TAG, "歌曲名不能为空");
        return -1;
    }

    char music_path[128] = {0};
    int ret = 0;

    // 2. 拼接完整音乐路径（用snprintf避免缓冲区溢出）
    if (ONLINE_MODE_YES == g_current_online_mode) {
        // 在线模式：基础URL + xx/歌曲名
        ret = snprintf(music_path, sizeof(music_path) - 1, 
                      "%s%s/%s", ONNINE_URL, singer, name);
    } else {
        // 离线模式：本地目录 + 歌曲名
        ret = snprintf(music_path, sizeof(music_path) - 1, 
                      "%s%s", MUSIC_PATH, name);
    }

    // 检查路径拼接是否溢出
    if (ret < 0 || ret >= sizeof(music_path) - 1) {
        LOGE(TAG, "歌曲路径过长（最大支持%ld字节）", 
                sizeof(music_path) - 1);
        return -1;
    }

    LOGD(TAG, "完整播放路径: '%s'", music_path);

    // 3. 构造mplayer命令参数（execv要求参数数组以NULL结尾）
    char* arg[] = {
        "mplayer",        // 命令名
        music_path,       // 音乐路径/URL
        "-slave",         // 禁止键盘控制
        "-quiet",         // 取消进度条输出
        "-input",         // 输入方式指定
        CMD_FIFO_PATH,    // 命令管道路径
        NULL              // 参数结束标记（必须）
    };

    // 4. 执行mplayer（进程替换，成功则不会返回）
    execv(MPLAYER_PATH, arg);

    // 5. 若执行到此处，说明execv失败
    LOGE(TAG, "执行mplayer失败: %s（检查mplayer路径和权限）", 
            strerror(errno));
    return -1;
}


void child_quit_process(int sig)
{
    // 修改子进程中播放状态为停止，防止重新fork孙进程
    g_current_state = PLAY_STATE_STOP;  

}


// 子进程执行函数
static void child_process(char *music_name)
{
    /*
        子进程用于创建并监控孙进程状态
        孙进程用于播放音乐
        所以只有在开始播放时才能创建孙进程
    */
    while(g_current_state == PLAY_STATE_PLAY)
    {
        pid_t pid = fork();
        if(-1 == pid)
        {
            LOGE(TAG, "创建孙进程失败");
            break;
        }
        else if(0 == pid)   // 孙进程
        { 
            /**
             * 第一次进入时，复制的子进程的music_name。不为空。
             * 执行完一次歌曲播放，孙进程被子进程回收，并重新fork，此时fork的music_name为空
             * 开始根据模式切换下一首歌曲
             */
            if(strlen(music_name) == 0) // 第二次进入后需判断歌曲名是否为空
            {
                // 获取共享内存数据
                Shm_Data shm_data;
                shm_get(&shm_data); 
                
                // 从数据库获取下一首歌
                int ret = link_get_next_music(shm_data.current_music,       // 当前播放
                                            shm_data.current_mode,          // 播放模式
                                            music_name);    // 输出下一首歌名 到 子进程music_name
                // 获取下一首歌成功
                if(ret == 0){
                    // 更新共享内存数据
                    shm_set(&shm_data);
                }
                // 当前为最后一首歌曲，需要申请新的歌曲
                else if(ret == 1){

                    // 如果为离线模式，则重头开始
                    if(g_current_online_mode == ONLINE_MODE_NO)
                    {
                        strcpy(music_name, g_music_head->next->music_name);
                    }
                    else    // 在线模式
                    {
                        // 发送信号通知父子进程
                        // 父进程 收到信号，请求新的歌曲
                        kill(shm_data.parent_pid, SIGUSR1);
                        // 子进程 收到信号，修改标志位
                        kill(shm_data.child_pid, SIGUSR1);
                        // 等待父进程杀死子进程，再退出孙进程。防止孙进程成为僵尸进程
                        usleep(100000);   
                        // 退出孙进程
                        exit(0);
                    }
                }
                else{
                    // 错误
                    LOGE(TAG, "获取下一首歌失败");
                }
            }

            // 更新子进程和孙进程pid、更新音乐与歌手名 到共享内存
            Shm_Data shm_data;
            shm_get(&shm_data); // 获取共享内存数据
            shm_data.child_pid = getppid(); // 设置子进程的父进程的pid(子进程)
            shm_data.grand_pid = getpid(); // 设置子进程的pid(子进程)
            // 分离服务器发送音乐名称中的歌手和歌曲
            split_music_info(music_name, &shm_data);
            // 存入共享内存
            shm_set(&shm_data);

#if 1
            LOGI(TAG, "=============================================");
            LOGI(TAG, "子进程id：%d", shm_data.child_pid);
            LOGI(TAG, "孙子进程id：%d", shm_data.parent_pid);
            LOGI(TAG, "歌曲名：%s", shm_data.current_music);
            LOGI(TAG, "歌手名：%s", shm_data.current_singer);
            LOGI(TAG, "当前播放模式：%d", shm_data.current_mode);
            LOGI(TAG, "=============================================");
#endif
            // 播放音乐（组装播放命令）
            if(play_music(shm_data.current_singer, shm_data.current_music) == -1)
            {
                // 让子进程不要继续fork孙进程（修改标志位）
                kill(shm_data.child_pid, SIGUSR1);
                // 退出此进程
                exit(-1);
            }
        }
        else    // 子进程
        {
            // 如果收到了孙进程发送的退出信号，则退出进程流程
            signal(SIGUSR1, child_quit_process);
            // 清空子进程的name数组
            memset(music_name, 0, MUSIC_MAX_NAME);
            int status;      // 存放子进程退出状态
            wait(&status);   // 等待子进程结束
        }
    }

}


// 用于播放音乐的函数，参数为播放的歌曲
void player_play_music(char *music_name)
{
    // 创建子进程
    pid_t pid = fork();
    if(-1 == pid)
    {
        LOGE(TAG, "创建子进程失败！");
        return;
    }
    else if(0 == pid)
    {
        // 进入子进程
        child_process(music_name);
        exit(0);    // 退出子进程
    }
    else    // 创建成功，父进程
    {
        return;
    }
}

// 播放开始
void player_start_play()
{   
    if(g_current_state == PLAY_STATE_PLAY && g_current_suspend == PLAY_SUSPEND_NO)   // 如果正在播放
        return;
    LOGI(TAG, "================= 开始播放=================");

    // 读取音乐名称
    char music_name[MUSIC_MAX_NAME];
    strcpy(music_name, g_music_head->next->music_name);

    // 更新播放状态
    g_current_state = PLAY_STATE_PLAY;      // 播放状态
    g_current_suspend = PLAY_SUSPEND_NO;    // 未暂停

    player_play_music(music_name);          // 开始播放音乐
}


// 向mplayer管道写入命令
static int player_write_fifo(const char* cmd)
{
    // 打开管道
    int fd = open(FIFO_PATH, O_WRONLY);
    if(-1 == fd)
    {
        LOGE(TAG, "打开mplayer fifo文件失败:%s", strerror(errno));
        return -1;
    }
    if(-1 == write(fd, cmd, strlen(cmd)))
    {
        LOGE(TAG, "写入mplayer fifo文件失败:%s", strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}


// 播放结束
void player_stop_play(void)
{ 
    if(g_current_state == PLAY_STATE_STOP)   // 如果已停止则退出
        return;
    LOGI(TAG, "================= 结束播放=================");

    Shm_Data s;
    shm_get(&s);
    // 通知子进程结束
    kill(s.child_pid, SIGUSR1);
    // 结束mplayer
    player_write_fifo("quit\n");
    // 回收子进程资源
    waitpid(s.child_pid, NULL, 0);
    // 修改播放状态标志位
    g_current_state = PLAY_STATE_STOP;      // 停止播放
    g_current_suspend = PLAY_SUSPEND_YES;    // 暂停 
}


// 播放继续
void player_continue_play()
{
    // 如果没有播放 或 未暂停 则返回
    if(g_current_state == PLAY_STATE_STOP || g_current_suspend == PLAY_SUSPEND_NO )
        return;

    // 结束mplayer
    player_write_fifo("pause\n");

    // 修改播放状态标志位
    g_current_state = PLAY_STATE_PLAY;      // 开始播放
    g_current_suspend = PLAY_SUSPEND_NO;    // 未暂停 

    LOGI(TAG, "================= 继续播放=================");
}
// 播放暂停
void player_suspend_play()
{
    // 如果没有播放 或 已暂停 则返回
    if(g_current_state == PLAY_STATE_STOP || g_current_suspend == PLAY_SUSPEND_YES)
        return;

    // 结束mplayer
    player_write_fifo("pause\n");

    // 修改播放状态标志位
    g_current_state = PLAY_STATE_PLAY;          // 停止播放
    g_current_suspend = PLAY_SUSPEND_YES;       // 已暂停 

    LOGI(TAG, "================= 暂停播放=================");
}


// 下一首
// 成功返回0
// 失败返回-1
// 为最后一首歌，返回1
int player_next_song()
{
    // // 播放状态为停止
    // if(g_current_state == PLAY_STATE_STOP)
    //     return;

    LOGI(TAG, "================= 下一首=================");

    // 找到当前歌曲
    Shm_Data s;
    shm_get(&s);
    // 遍历链表找到下一首歌
    char next_music[MUSIC_MAX_NAME];

    int mode = s.current_mode;  //播放模式不可为单曲播放，如果是则改为循环播放。
    if(mode == SINGLE_PLAY)     mode = ORDER_PLAY;  // 如果为其他（随机和循环）则不变

    int ret = link_get_next_music(s.current_music,      // 当前播放
                                    mode,               // 播放模式(下一首不能为单曲播放)
                                    next_music);        // 输出下一首歌名 到 子进程music_name
    // 获取下一首歌成功
    if(ret == 0){
        // 分离歌曲和歌手名
        split_music_info(next_music, &s);
        // 更新共享内存数据
        shm_set(&s);
        // 拼接完整音乐路径
        char music_path[512] = {0};
        if (ONLINE_MODE_YES == g_current_online_mode) {
            // 在线模式：基础URL + xxx/歌曲名
            snprintf(music_path, sizeof(music_path) - 1, 
                      "%s%s/%s", ONNINE_URL, s.current_singer, s.current_music);
	    
        } else {
            // 离线模式：本地目录 + 歌曲名
            snprintf(music_path, sizeof(music_path) - 1, 
                      "%s%s", MUSIC_PATH, s.current_music);
        }

        // 组装命令
        char cmd[1024] = {0};
        sprintf(cmd, "loadfile '%s'\n", music_path);

        // 如果还没有播放，则先播放
        if(g_current_state == PLAY_STATE_STOP)
            player_start_play();

        // 执行命令
        player_write_fifo(cmd);
        // 修改标志位
        g_current_state = PLAY_STATE_PLAY;      // 播放中
        g_current_suspend = PLAY_SUSPEND_NO;    // 未暂停

        return 0;
    }
    // 当前为最后一首歌曲，需要申请新的歌曲
    else if(ret == 1){
        // 在线模式
        if(g_current_online_mode == ONLINE_MODE_YES)
        {
            // 结束播放
            player_stop_play();
            // 清空链表
            link_clear_list();
            // 请求新的音乐数据
            socket_get_music(s.current_singer);
            // 开始播放
            player_start_play();

            // 通知APP歌曲列表更新
            socket_upload_music_list();

            return 1;
        }
        else{ // 离线模式
            // 开始播放
            player_start_play();
            return 1;
        }
    }
    else{
        // 错误
        LOGE(TAG, "获取下一首歌失败");
        return -1;
    }
}


// 播放上一首歌
int player_prev_song()
{
    // 读取共享内存，找到当前歌曲
    Shm_Data s;
    shm_get(&s);
    // 遍历链表，根据当前歌曲找到上一首，如果是第一首则返回第一首
    char prev_music[MUSIC_MAX_NAME];
    int ret = link_get_prev_music(s.current_music, prev_music);
    // 获取上一首歌成功
    if(ret == 0 || ret == 1){ // 0为找到， 1为返回到第一首了。
        // 分离歌曲和歌手名
        split_music_info(prev_music, &s);
        // 更新共享内存数据
        shm_set(&s);
        // 拼接完整音乐路径
        char music_path[512] = {0};
        if (ONLINE_MODE_YES == g_current_online_mode) {
            // 在线模式：基础URL + xxx/歌曲名
            snprintf(music_path, sizeof(music_path) - 1, 
                "%s%s/%s", ONNINE_URL, s.current_singer, s.current_music);
        } else {
            // 离线模式：本地目录 + 歌曲名
            snprintf(music_path, sizeof(music_path) - 1, 
                      "%s%s", MUSIC_PATH, s.current_music);
        }

        // 组装命令
        char cmd[1024] = {0};
        sprintf(cmd, "loadfile '%s'\n", music_path);

        // 如果还没有开始播放，则先播放
        if(g_current_state == PLAY_STATE_STOP)
            player_start_play();

        // 执行命令
        player_write_fifo(cmd);
        // 修改标志位
        g_current_state = PLAY_STATE_PLAY;      // 播放中
        g_current_suspend = PLAY_SUSPEND_NO;    // 未暂停
        return 0;
    }
    else
    {
        // 错误
        LOGE(TAG, "获取上一首歌失败");
        return -1;
    }
}

// 设置播放模式
void player_set_mode(int mode)
{
    if(mode == SINGLE_PLAY || mode == ORDER_PLAY || mode == RANDOM_PLAY)
    {
        // 获取共享内存
        Shm_Data s;
        shm_get(&s);
        // 修改共享内存
        s.current_mode = mode;
        // 设置共享内存
        shm_set(&s);

        switch(mode)
        {
            case SINGLE_PLAY : LOGI(TAG, "================= 单曲循环================="); break;
            case ORDER_PLAY  : LOGI(TAG, "================= 顺序循环================="); break;
            case RANDOM_PLAY : LOGI(TAG, "================= 随机播放================="); break;
        }
    } 
    else{
        // 错误
        LOGE(TAG, "模式设置错误");
    }
}





// ========================

// 初始化asr的管道（打开并监听）
int init_asr_fifo()
{
    // 打开管道文件（如果不存在则创建）
    g_asr_fd = open(ASR_PIPE_PATH, O_RDONLY);
    if(-1 == g_asr_fd)
    {
        LOGE(TAG, "打开asr语音识别管道失败: %s", strerror(errno));
        return -1;
    }
    // 添加到select监听
    FD_SET(g_asr_fd, &READSET);
    update_max_fd();
    return 0;
}

// 初始化kws的管道（打开并监听）
int init_kws_fifo()
{
    // 打开管道文件
    g_kws_fd = open(KWS_PIPE_PATH, O_RDONLY);
    if(-1 == g_kws_fd)
    {
        LOGE(TAG, "打开kws关键词识别管道失败: %s", strerror(errno));
        return -1;
    }
    // 添加到select监听
    FD_SET(g_kws_fd, &READSET);
    update_max_fd();
    return 0;
}

// 初始化tts的管道（打开）
int init_tts_fifo()
{
    // 打开管道文件（非阻塞模式）
    g_tts_fd = open(TTS_PIPE_PATH, O_WRONLY | O_NONBLOCK);
    if(-1 == g_tts_fd)
    {
        LOGW(TAG, "打开tts语音合成管道失败: %s (程序继续运行)", strerror(errno));
        // 不返回错误，让程序继续运行
    }
    // // 添加到select监听
    // FD_SET(g_tts_fd, &READSET);
    // update_max_fd();
    // 不用监听
    return 0;
}

// 播放指定音乐人的歌曲
void player_singer_play(const char *singer)
{
    LOGI(TAG, "=======指定播放音乐=======");
    // 结束播放
    player_stop_play();
    // 清空链表
    link_clear_list();
    // 获取新的歌曲
    socket_get_music(singer);
    // 开始播放
    player_start_play();

    // 通知APP歌曲列表更新
    socket_upload_music_list();
}

// 通知asr-kws 进程，当前已切离线模式
static void asr_kws_switch_offline_mode(void)
{
    LOGI(TAG, "[asr_kws]：通知asr_kws进程切换离线模式");
    // 获取tts的进程号
    FILE *fp = popen("pgrep asr_kws", "r");
    if(fp == NULL)
    {
        LOGE(TAG, "获取asr_kws进程号失败: %s", strerror(errno));
        return;
    }
    char pid[16] = {0};
    fscanf(fp, "%s", pid);
    pclose(fp);
    // 发送终止信号
    if(-1 == kill(atoi(pid), SIGUSR1))
    {
        LOGE(TAG, "发送SIGUSR1信号给asr_kws进程失败: %s", strerror(errno));
        return;
    }
}


// 切换离线模式
int player_switch_offline_mode(void)
{
    if(g_current_online_mode == ONLINE_MODE_NO)
    {
        tts_play_text("当前为离线模式，无需切换");
        return 0;
    }

    // 结束当前音乐
    player_stop_play();

    char udisk_name[128] = {0};
    strcpy(udisk_name, UDISK_PATH_YES1);
    // 判断U盘是否插上(设备文件/dev/sdb1变为 /dev/sdc1  或 /dev/sda1)
    if(access(UDISK_PATH_YES1, F_OK) != 0)  // 使用access判断U盘未插入标志文件是否存在
    {
        strcpy(udisk_name, UDISK_PATH_YES2);
        // 先看看sdc1在不在
        if(access(UDISK_PATH_YES2, F_OK) != 0)   {
            strcpy(udisk_name, UDISK_PATH_YES3);
            // sdc1不在，看看sda1在不在 
            if(access(UDISK_PATH_YES3, F_OK) != 0)   {
                // sda1也不在，U盘未插入
                tts_play_text("请先插入存储设备");
                return 1;   // U盘未插入
            }
        }
    }

    LOGI(TAG, "当前U盘设备为：%s， 将挂载到%s", udisk_name, UDISK_MOUNT_PATH);
    // 取消挂载U盘，如果有的话。
    system("killall mplayer"); // 杀死所有尝试解码的mplayer进程
    sleep(1);
    //umount(UDISK_MOUNT_PATH);
    char buf[128];
    snprintf(buf, sizeof(buf), "umount %s", UDISK_MOUNT_PATH);
    int ret = system(buf);
    if (ret == -1) {
        LOGE(TAG, "执行umount命令失败：%s", strerror(errno));
        return -1;
    }

    // 挂载U盘到 /mnt/usb 目录
    if(access(UDISK_MOUNT_PATH, F_OK) != 0)   {   // 判断挂载目录是否存在
        // 目录不存在，创建目录
        if(mkdir(UDISK_MOUNT_PATH, 0777) != 0)   {
            LOGE(TAG, "创建挂载目录失败: %s", strerror(errno));
            tts_play_text("创建挂载存储设备目录失败");
            return -1;
        }
    }
    // 挂载U盘 到/mnt/usb 目录
    if(mount(udisk_name, UDISK_MOUNT_PATH, "exfat", 0, NULL) != 0)   {
        LOGE(TAG, "挂载U盘失败: %s", strerror(errno));
        tts_play_text("创建挂载存储设备失败");
        return -1;
    }
    

    // 读取U盘歌曲 添加到链表
    if(-1 == link_read_udisk_music())
    {
        tts_play_text("切换离线模式失败，无法读取存储设备歌曲");
        return -1;
    }
    // 获取第一首歌曲的名字(防止切换离线模式后，说 第一句话说 上一首导致的错误)
    Shm_Data s;
    shm_get(&s);
    strcpy(s.current_music, g_music_head->next->music_name);
    shm_set(&s);
    // debug
    // link_traverse_list(NULL);   // 遍历链表 查看是否有正确读取到歌曲

    // 关闭网络（断开网络、取消上报线程）
    socket_disconnect();

    // 通知asr-kws 进程，当前已切离线模式
    asr_kws_switch_offline_mode();

    // 设置标志位
    g_current_online_mode = ONLINE_MODE_NO; // 离线
    g_current_state = PLAY_STATE_STOP;      // 停止
    g_current_suspend = PLAY_SUSPEND_YES;   // 暂停

    tts_play_text("已切换到离线模式");
    return 0;
}

// 通知asr-kws 进程，当前已切在线模式
static void asr_kws_switch_online_mode(void)
{
    LOGI(TAG, "[asr_kws]：通知asr_kws进程切换在线模式");
    // 获取asr_kws的进程号
    FILE *fp = popen("pgrep asr_kws", "r");
    if(fp == NULL)
    {
        LOGE(TAG, "获取asr_kws进程号失败: %s", strerror(errno));
        return;
    }
    char pid[16] = {0};
    fscanf(fp, "%s", pid);
    pclose(fp);
    // 发送SIGUSR2信号（与离线模式的SIGUSR1区分）
    if(-1 == kill(atoi(pid), SIGUSR2))
    {
        LOGE(TAG, "发送SIGUSR2信号给asr_kws进程失败: %s", strerror(errno));
        return;
    }
}



// 切换到在线模式
int player_switch_online_mode(void)
{
    if(g_current_online_mode == ONLINE_MODE_YES)
    {
        tts_play_text("当前为在线模式，无需切换");
        return 0;
    }

    // 1. 结束当前离线音乐播放
    player_stop_play();

    // 2. 取消挂载U盘
    system("killall mplayer"); // 杀死所有尝试解码的mplayer进程
    sleep(1);
    //umount(UDISK_MOUNT_PATH);
    char buf[128];
    snprintf(buf, sizeof(buf), "umount %s", UDISK_MOUNT_PATH);
    int ret = system(buf);
    if (ret == -1) {
        LOGE(TAG, "执行umount命令失败：%s", strerror(errno));
        return -1;
    }

    // 3. 连接服务器（失败则直接返回）
    ret = socket_connect();
    if (ret != 0) {
        return -1;
    }

    // 4. 加载在线歌曲列表（默认"其他"歌手）
    player_singer_play("其他");
    // 5. 初始化共享内存的当前歌曲（防止切换后操作异常）
    Shm_Data s;
    shm_get(&s);
    strcpy(s.current_music, g_music_head->next->music_name);
    shm_set(&s);

    // 6. 通知asr-kws进程切换在线模式
    asr_kws_switch_online_mode();

    // 7. 修正模式标志位
    g_current_online_mode = ONLINE_MODE_YES; // 改为在线模式
    g_current_state = PLAY_STATE_PLAY;       // 初始播放状态
    g_current_suspend = PLAY_SUSPEND_NO;     // 未暂停

    // 8. 语音提示修正
    tts_play_text("已切换到在线模式");
    return 0;
}



