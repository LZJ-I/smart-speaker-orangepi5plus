#include "select.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h> 
#include <string.h>
#include "player.h"
#include <unistd.h>
#include "socket.h"
#include <pthread.h>
#include "device.h"
#include <json-c/json.h>
#include <signal.h>
#include "../debug_log.h"

#define TAG "SELECT"

fd_set READSET;         // 读事件集合
int g_max_fd;           // 最大文件描述符

int g_need_continue = 0;  // 记录上一次的播放情况， 上一次处于播放，则现在为播放。

// 初始化select

int select_init()
{
    FD_ZERO(&READSET);      // 清空集合
    FD_SET(0, &READSET);    // 添加标准输入到文件描述符(Test)
    g_max_fd = 0;           // 初始化最大fd为0
    return 0;    
}

// 显示操作菜单
static void show_menu()
{
    printf("=============================================\n");
    printf("\t1. 开始音乐  \t\t2.  结束播放\n");
    printf("\t3. 暂停播放  \t\t4.  继续播放\n");
    printf("\t5. 上一首    \t\t6.  下一首  \n");
    printf("\t7. 减小音量  \t\t8.  增加音量\n");
    printf("\t9. 单曲循环  \t\ta.  顺序播放\n");
    printf("=============================================\n");
}


// 清空输入缓冲区（读取所有残留字符，直到\n或EOF）
static void clear_input_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// 处理标准输入
static void select_read_stdio(void)
{
    char ch;
    // 使用read直接读取1个字符
    ssize_t ret = read(0, &ch, 1);
    if (ret != 1) { // 读取失败或无有效字符
        clear_input_buffer();
        return;
    }
    // 忽略空白字符（回车、空格、制表符）
    if (ch == '\n' || ch == ' ' || ch == '\t') {
        return;
    }
    clear_input_buffer(); // 清空剩余缓冲区

    LOGI(TAG, "键盘输入数据[%c]", ch);
    switch (ch)
    {
    case '1':
        player_start_play();    // 开始播放
        break;
    case '2':
        player_stop_play();     // 停止播放
        break;
    case '3':
        player_suspend_play();  // 暂停播放
        break;
    case '4':
        player_continue_play(); // 继续播放
        break;
    case '5':
        player_prev_song();     // 上一首
        break;
    case '6':
        player_next_song();     // 下一首
        break;
    case '7':
        device_adjust_volume(0);    // 减小音量
        break;
    case '8':
        device_adjust_volume(1);    // 增大音量
        break;
    case '9':
        player_set_mode(SINGLE_PLAY);   //单曲循环
        break;
    case 'a':
        player_set_mode(ORDER_PLAY);   //顺序播放
        break;
    default:
        break;
    }
}

// 辅助函数：更新g_max_fd为当前READSET中最大的fd
void update_max_fd() {
    g_max_fd = 0;
    // 遍历所有可能的fd（FD_SETSIZE是select支持的最大fd数，通常为1024）
    for (int fd = 0; fd < FD_SETSIZE; fd++) {
        if (FD_ISSET(fd, &READSET)) {   // 判断fd是否在READSET中
            g_max_fd = (fd > g_max_fd) ? fd : g_max_fd;
        }
    }
}

// 终止当前正在合成的语音
static void tts_end_play(void)
{
    LOGI(TAG, "尝试终止当前正在合成的语音");
    // 获取tts的进程号
    FILE *fp = popen("pgrep tts", "r");
    if(fp == NULL)
    {
        LOGE(TAG, "获取tts进程号失败: %s", strerror(errno));
        return;
    }
    char pid[16] = {0};
    fscanf(fp, "%s", pid);
    pclose(fp);
    // 发送终止信号
    if(-1 == kill(atoi(pid), SIGUSR1))
    {
        LOGE(TAG, "发送SIGINT信号给tts进程失败: %s", strerror(errno));
        return;
    }
}

// 语音合成：xxx
void tts_play_text(char * text)
{
    // 写入管道
    if(-1 == write(g_tts_fd, text, strlen(text)))
    {
        LOGE(TAG, "写入tts管道失败: %s", strerror(errno));
        return;
    }
    LOGI(TAG, "写入tts管道：%s", text);
}

// 更换tts语音合成的声音
static void tts_change_voice(void)
{
    LOGI(TAG, "更换说话人音色");
    // 获取tts的进程号
    FILE *fp = popen("pgrep tts", "r");
    if(fp == NULL)
    {
        LOGE(TAG, "获取tts进程号失败: %s", strerror(errno));
        return;
    }
    char pid[16] = {0};
    fscanf(fp, "%s", pid);
    pclose(fp);
    // 发送终止信号
    if(-1 == kill(atoi(pid), SIGUSR2))
    {
        LOGE(TAG, "发送SIGUSR2信号给tts进程失败: %s", strerror(errno));
        return;
    }
    // 稍作延时100ms
    usleep(100000);

    // 通知
    tts_play_text("那我以后用这个音色和你说话吧");

}



// 处理asr管道事件
static void select_read_asr(void)
{
    char buf[256] = {0};
    ssize_t ret = read(g_asr_fd, buf, sizeof(buf)-1);
    if(ret == -1)
    {
        LOGE(TAG, "读取asr管道失败: %s", strerror(errno));
        return;
    }else if(ret == 0)  // 对端关闭
    {
        LOGE(TAG, "asr管道对端关闭");
        FD_CLR(g_asr_fd, &READSET); // 从监听集合中移除
        update_max_fd();    // 更新最大fd
        return;
    }
    LOGI(TAG, "读取asr管道数据[%s]", buf);

    /* 
        暂时使用关键字匹配，
        后续尝试使用自然语言分析来让音箱动作。
    */

    // 简单的关键字匹配, 然后恢复播放（除了暂停、结束以外）

    // 1. 先处理停止/结束相关的命令（最高优先级）
    if(strstr(buf, "停止播放") || 
       strstr(buf, "结束") || strstr(buf, "退出") || strstr(buf, "关闭") || strstr(buf, "关掉"))
    {
        g_need_continue = 0;
        player_stop_play();     // 停止播放
    }
    // 2. 处理暂停相关的命令
    else if(strstr(buf, "暂停") || strstr(buf, "停一下") || 
            strstr(buf, "等一下") || strstr(buf, "先停下"))
    {
        g_need_continue = 0;
        player_suspend_play();  // 暂停播放
    }
    // 3. 处理继续播放
    else if(strstr(buf, "继续播放") || strstr(buf, "继续") || strstr(buf, "接着播放"))
    {
        player_continue_play(); // 继续播放
    }
    // 4. 处理上一首/下一首
    else if(strstr(buf, "下一首") || strstr(buf, "换一首") || strstr(buf, "切歌") || 
            strstr(buf, "切换歌曲") || strstr(buf, "下一曲") || strstr(buf, "下一首歌"))
    {
        player_next_song();     // 下一首
    }
    else if(strstr(buf, "上一首") || strstr(buf, "上一曲") || strstr(buf, "上一首歌") || 
            strstr(buf, "前一首"))
    {
        player_prev_song();     // 上一首
    }
    // 5. 处理音量调节
    else if( (strstr(buf, "减") && (strstr(buf, "音量") || strstr(buf, "声音"))) ||
             (strstr(buf, "降低") && (strstr(buf, "音量") || strstr(buf, "声音"))) ||
             ((strstr(buf, "音量") || strstr(buf, "声音")) && (strstr(buf, "太大") || strstr(buf, "有点大") || strstr(buf, "小点") || strstr(buf, "小一点") || strstr(buf, "调小"))) )
    {
        device_adjust_volume(0);    // 减小音量
    }
    else if( (strstr(buf, "增大") && (strstr(buf, "音量") || strstr(buf, "声音"))) ||
             (strstr(buf, "增加") && (strstr(buf, "音量") || strstr(buf, "声音"))) ||
             (strstr(buf, "提高") && (strstr(buf, "音量") || strstr(buf, "声音"))) ||
             ((strstr(buf, "音量") || strstr(buf, "声音")) && (strstr(buf, "太小") || strstr(buf, "有点小") || strstr(buf, "大点") || strstr(buf, "大一点") || strstr(buf, "调大"))) )
    {
        device_adjust_volume(1);    // 增大音量
    }
    // 6. 处理播放模式
    else if(strstr(buf, "单曲循环") || strstr(buf, "循环播放") || strstr(buf, "单曲循环播放"))
    {
        player_set_mode(SINGLE_PLAY);   // 单曲循环
    }
    else if(strstr(buf, "顺序播放") || strstr(buf, "列表顺序") || strstr(buf, "按顺序播放"))
    {
        player_set_mode(ORDER_PLAY);   // 顺序播放
    }
    // else if(strstr(buf, "随机播放") || strstr(buf, "随便播放") || strstr(buf, "乱序播放"))
    // {
    //     player_set_mode(RANDOM_PLAY);   // 随机播放
    // }
    // 7. 处理特定歌手
    else if((strstr(buf, "想") || strstr(buf, "要") || strstr(buf, "听") || strstr(buf, "播放") || strstr(buf, "来一首")) &&
            strstr(buf, "周杰伦"))
    {
        player_singer_play("周杰伦");
    }
    else if((strstr(buf, "想") || strstr(buf, "要") || strstr(buf, "听") || strstr(buf, "播放") || strstr(buf, "来一首")) &&
            strstr(buf, "刘宇宁"))
    {
        player_singer_play("刘宇宁");
    }
    else if((strstr(buf, "想") || strstr(buf, "要") || strstr(buf, "听") || strstr(buf, "播放") || strstr(buf, "来一首")) &&
            strstr(buf, "薛之谦"))
    {
        player_singer_play("薛之谦");
    }
    else if((strstr(buf, "想") || strstr(buf, "要") || strstr(buf, "听") || strstr(buf, "播放") || strstr(buf, "来一首")) &&
            strstr(buf, "张杰"))
    {
        player_singer_play("张杰");
    }
    else if((strstr(buf, "想") || strstr(buf, "要") || strstr(buf, "听") || strstr(buf, "播放") || strstr(buf, "来一首")) &&
            (strstr(buf, "其他") || strstr(buf, "流行")))
    {
        player_singer_play("其他");
    }
    // 8. 开始播放
    else if(strstr(buf, "开始播放") || strstr(buf, "播放音乐") || 
            (strstr(buf, "首") && strstr(buf, "听听")) ||
            strstr(buf, "播放") || strstr(buf, "开始"))
    {
        // 额外检查是否包含"结束"等否定词
        if(!strstr(buf, "结束") && !strstr(buf, "停止") && !strstr(buf, "暂停"))
        {
            player_start_play();    // 开始播放
        }
    }

    // 9. 更换tts语音合成的声音
    else if(strstr(buf, "换") && (strstr(buf, "声音") || strstr(buf, "音色")))
    {
        tts_change_voice();
    }
    
    // 10. 切换离线模式
    else if(strstr(buf, "离线模式"))
    {
        player_switch_offline_mode();
    }

    else    //以上都不是，则ai回复
    {
        // 启动千问进程
        char cmd[1024] = {0};
        sprintf(cmd, "../qwen/run %s", buf);
        // 执行命令
        system(cmd);   // 启动qwen进程、调用sh脚本、发送数据、接收数据、解析、写入管道
        g_need_continue = 0;    //强制暂停
    }

    // 恢复播放
    // 代码中我有写 状态自动判断，例如已结束、已开始，会自动退出
    if(g_need_continue)
        player_continue_play(); // 继续播放音乐
}


// 处理kws管道事件
static void select_read_kws(void)
{
    char buf[128] = {0};
    ssize_t ret = read(g_kws_fd, buf, sizeof(buf)-1);
    if(ret < 0)
    {
        LOGE(TAG, "读取kws管道失败: %s", strerror(errno));
        return;
    }
    else if(ret == 0)   // 对端关闭
    {
        LOGE(TAG, "kws管道对端关闭");
        FD_CLR(g_kws_fd, &READSET); // 从监听集合中移除
        update_max_fd();    // 更新最大fd
        return;
    }
    

    // 如果为在线模式
    if(g_current_online_mode == ONLINE_MODE_YES)
    {
        LOGI(TAG, "[KWS在线]读取kws管道数据[%s]", buf);
         // 判断当前状态，确定后续是否需要恢复播放
        if(g_current_state == PLAY_STATE_PLAY && g_current_suspend == PLAY_SUSPEND_NO)  //播放且未暂停
            g_need_continue = 1;
        else
            g_need_continue = 0;
        // 当识别到收到关键字，那么就暂停音乐，以防止音乐干扰
        player_suspend_play();
        // 结束正在合成的语音，通知tts模型
        tts_end_play();
        // 语音合成：我在
        tts_play_text("我在");
    
       
    }

    // 离线模式
    else if(g_current_online_mode == ONLINE_MODE_NO)
    {
        LOGI(TAG, "[KWS离线]读取kws管道数据[%s]", buf);

        // 判断关键字
        if(strstr(buf, "在线模式"))
        {
            player_switch_online_mode();   // 切换到在线模式
        }
        else if(strstr(buf, "下一首") ||
        strstr(buf, "换一首") ||
        strstr(buf, "切换歌曲") ||
        strstr(buf, "下一曲"))
        {
            player_next_song();    // 下一首
        }

        else if(strstr(buf, "上一首") ||
        strstr(buf, "前一首") ||
        strstr(buf, "上一曲"))
        {
            player_prev_song();    // 上一首
        }
        else if(strstr(buf, "我想听音乐") ||
        strstr(buf, "想听歌") ||
        strstr(buf, "给我放首歌") ||
        strstr(buf, "开始播放"))
        {
            player_start_play();    // 开始播放
        }

        else if(strstr(buf, "暂停") ||
        strstr(buf, "等一下") ||
        strstr(buf, "停一下") ||
        strstr(buf, "先停下"))
        {
            player_suspend_play();    // 暂停播放
        }

        else if(strstr(buf, "继续"))
        {
            player_continue_play();    // 继续播放
        }

        else if(strstr(buf, "我不想听了") ||
        strstr(buf, "结束") ||
        strstr(buf, "退出") ||
        strstr(buf, "关闭") ||
        strstr(buf, "关掉") ||
        strstr(buf, "停止"))
        {
            player_stop_play();    // 结束播放
        }

	else if(strstr(buf, "声音小一些") ||
        strstr(buf, "音量小一些") ||
        strstr(buf, "减小音量") ||
        strstr(buf, "减小声音") ||  // 新增：用户列出的指令
        strstr(buf, "减少音量") ||  // 新增：用户列出的指令
        strstr(buf, "降低音量") ||
        strstr(buf, "降低声音") ||
        strstr(buf, "声音太大") ||
        strstr(buf, "音量太大"))
	{
    		device_adjust_volume(0);    // 音量减小
	}

        else if(strstr(buf, "声音大一些") ||
        strstr(buf, "音量大一些") ||
        strstr(buf, "增大音量") ||
        strstr(buf, "增大声音") ||
        strstr(buf, "声音太小") ||
        strstr(buf, "音量太小"))
        {
            device_adjust_volume(1);    // 音量增大
        }

        else if(strstr(buf, "单曲循环"))
        {
            player_set_mode(SINGLE_PLAY);   // 单曲循环
        }

        else if(strstr(buf, "顺序播放"))
        {
            player_set_mode(ORDER_PLAY);  // 顺序播放
        }
    }
}


// 运行事件监听
void select_run(void)
{
    fd_set TMPSET;  // 临时文件描述符集合
    show_menu();    // 显示菜单
    
    LOGI(TAG, "select 监听开始：g_max_fd=%d, g_asr_fd=%d, g_kws_fd=%d", 
         g_max_fd, g_asr_fd, g_kws_fd);
    LOGI(TAG, "FD_ISSET(asr)=%d, FD_ISSET(kws)=%d", 
         FD_ISSET(g_asr_fd, &READSET), FD_ISSET(g_kws_fd, &READSET));
    
    while (1)
    {
        TMPSET = READSET;   // 将全局变量赋给临时变量， 防止因为被监听到导致的取消监听
        int ret = select(g_max_fd+1, &TMPSET, NULL, NULL, NULL);
        if(-1 == ret)
        {
            // 排除信号对select的干扰
            if(EINTR == errno)
                continue;
            LOGE(TAG, "select错误: %s", strerror(errno));
            return;
        }

        // 处理标准输入
        if(FD_ISSET(0, &TMPSET)){
            select_read_stdio();
        }

        // 处理服务器事件
        else if(FD_ISSET(g_socket_fd, &TMPSET)){
            select_read_socket();
        }

        // 处理按键事件
        else if(FD_ISSET(g_button_fd, &TMPSET)){
            device_read_button();
        }

        // 处理asr管道事件
        else if(FD_ISSET(g_asr_fd, &TMPSET)){
            select_read_asr();
        }

        // 处理kws管道事件
        else if(FD_ISSET(g_kws_fd, &TMPSET)){
            select_read_kws();
        }
    }
}



// 解析服务器命令
static void Parse_server_cmd(char *buf, char *cmd)
{
    if(!buf || !cmd) return;

    // 解析json字符串
    json_object *json = json_tokener_parse(buf);
    if(json == NULL)
    {
        LOGE(TAG, "解析json字符串失败");
        // json_object_put(json);
        return;
    }
    // 解析cmd字段
    json_object *json_cmd = json_object_object_get(json, "cmd");
    if(json_cmd == NULL)
    {
        LOGE(TAG, "解析json中没有cmd字段");
        json_object_put(json);
        return;
    }
    // 转换cmd字段为字符串
    const char *cmd_str = json_object_get_string(json_cmd);
    if(cmd_str == NULL)
    {
        LOGE(TAG, "转换cmd字段为字符串失败");
        json_object_put(json);
        return;
    }
    strcpy(cmd, cmd_str);
    json_object_put(json);
}




// 处理服务器发送的信息
void select_read_socket(void)
{
    // 接收服务器信息
    char buf[1024] = {0};
    socket_recv_data(buf);
    // 解析服务器命令
    char cmd[128] = {0};
    Parse_server_cmd(buf, cmd);
    LOGI(TAG, "服务器发送的命令:%s", cmd);
    
    // 处理服务器控制应用开始播放
    if(strcmp(cmd, "app_start_play") == 0)
    {
        socket_start_play();
    }

    // 处理服务器控制应用停止播放
    else if(strcmp(cmd, "app_stop_play") == 0)
    {
        socket_stop_play();
    }

    // 处理服务器控制应用继续播放
    else if(strcmp(cmd, "app_continue_play") == 0)
    {
        socket_continue_play();
    }

    // 处理服务器控制应用暂停播放
    else if(strcmp(cmd, "app_suspend_play") == 0)
    {
        socket_suspend_play();
    }

    // 处理服务器控制应用上一首音乐
    else if(strcmp(cmd, "app_play_prev_song") == 0)
    {
        socket_prev_song();
    }

    // 处理服务器控制应用下一首音乐
    else if(strcmp(cmd, "app_play_next_song") == 0)
    {
        socket_next_song();
    }

    // 处理服务器控制应用增加音量
    else if(strcmp(cmd, "app_add_volume") == 0)
    {
        socket_add_volume();
    }

    // 处理服务器控制应用减少音量
    else if(strcmp(cmd, "app_sub_volume") == 0)
    {
        socket_sub_volume();
    }

    // 处理服务器控制应用设置随机播放模式
    else if(strcmp(cmd, "app_random_mode") == 0)
    {
        socket_set_random_mode();
    }

    // 处理服务器控制应用设置顺序播放模式
    else if(strcmp(cmd, "app_order_mode") == 0)
    {
        socket_set_order_mode();
    }

    // 处理服务器控制应用设置单曲循环播放模式
    else if(strcmp(cmd, "app_single_mode") == 0)
    {
        socket_set_single_mode();
    }

    // 处理服务器获取当前音乐列表
    else if(strcmp(cmd, "app_get_music_list") == 0)
    {
        socket_upload_music_list();
    }

    // // 处理服务器控制应用播放指定歌曲（歌手/歌曲）
    // else if(strcmp(cmd, "app_play_assign_song") == 0)
    // {
    //     socket_play_assign_song();
    // }
}


