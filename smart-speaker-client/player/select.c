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
#include "../ipc/ipc_message.h"
#include "../voice-assistant/common/ipc_protocol.h"
#include "../voice-assistant/llm/llm.h"
#include "rule_match.h"
#include "music_lib_bridge.h"
#include "select_text.h"
#include "select_music_llm.h"

#define TAG "SELECT"

fd_set READSET;         // 读事件集合
int g_max_fd;           // 最大文件描述符

static uint32_t g_tts_seq = 0;
static const char *FALLBACK_WAV_PATH = "./assets/tts/fallback_unmatched.wav";
static const char *MODE_ORDER_WAV_PATH = "./assets/tts/mode_order.wav";
static const char *MODE_SINGLE_WAV_PATH = "./assets/tts/mode_single.wav";

void select_on_player_stopped(void)
{
    player_set_audio_focus(AUDIO_FOCUS_IDLE);
}

static int ensure_tts_fifo_ready(void)
{
    if (g_tts_fd != -1) {
        return 0;
    }
    init_tts_fifo();
    if (g_tts_fd == -1) {
        return -1;
    }
    return 0;
}

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
    printf("\tk.  模拟播完  \t\to.  在线/离线切换\n");
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
        player_stop_play();
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
    case 'k':
        player_simulate_song_finished();
        break;
    case 'o':
        if (g_current_online_mode == ONLINE_MODE_YES) {
            player_switch_offline_mode();
        } else {
            player_switch_online_mode();
        }
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

// 语音合成：xxx
void tts_play_text(char * text)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    if (ensure_tts_fifo_ready() != 0) {
        LOGW(TAG, "tts管道未打开，跳过播报");
        return;
    }
    // 写入管道
    if (ipc_send_message(g_tts_fd, IPC_CMD_PLAY_TEXT, text, (uint32_t)(strlen(text) + 1), &g_tts_seq) != 0)
    {
        LOGE(TAG, "写入tts管道失败: %s", strerror(errno));
        return;
    }
    LOGI(TAG, "写入tts管道：%s", text);
}

void tts_play_audio_file(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return;
    }
    if (ensure_tts_fifo_ready() != 0) {
        LOGW(TAG, "tts管道未打开，跳过音频文件播报");
        return;
    }
    if (ipc_send_message(g_tts_fd, IPC_CMD_PLAY_AUDIO_FILE, (const uint8_t *)path,
                         (uint32_t)(strlen(path) + 1), &g_tts_seq) != 0)
    {
        LOGE(TAG, "写入tts音频文件命令失败: %s", strerror(errno));
        return;
    }
    LOGI(TAG, "写入tts音频文件命令：%s", path);
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
        close(g_asr_fd);
        g_asr_fd = -1;
        init_asr_fifo();
        update_max_fd();    // 更新最大fd
        return;
    }
    buf[ret] = '\0';
    if (buf[ret - 1] == '\n') buf[ret - 1] = '\0';
    LOGI(TAG, "读取asr管道数据[%s]", buf);
    if (strcmp(buf, ASR_TIMEOUT_SENTINEL) == 0) {
        if (player_audio_focus_should_resume()) {
            player_audio_focus_prepare_resume();
            player_continue_play();
        }
        return;
    }

    rule_match_result_t match_result;
    int resume_after_handle = 0;
    int had_music_before_wakeup = player_audio_focus_should_resume();
    int match_ret = rule_match_text(buf, &match_result);
    if (match_ret != 0) {
        LOGE(TAG, "规则匹配失败");
        return;
    }

    if (match_result.matched) {
        LOGI(TAG, "规则命中: %s (%s)", rule_cmd_to_string(match_result.cmd), match_result.action_desc);
    }

    switch (match_result.cmd) {
    case RULE_CMD_STOP:
        player_stop_play();
        break;
    case RULE_CMD_PAUSE:
        player_audio_focus_cancel_resume();
        player_suspend_play();
        break;
    case RULE_CMD_RESUME:
        player_audio_focus_cancel_resume();
        player_continue_play();
        break;
    case RULE_CMD_NEXT:
        player_next_song();
        break;
    case RULE_CMD_PREV:
        player_prev_song();
        break;
    case RULE_CMD_VOL_DOWN:
        device_adjust_volume(0);
        resume_after_handle = had_music_before_wakeup;
        break;
    case RULE_CMD_VOL_UP:
        device_adjust_volume(1);
        resume_after_handle = had_music_before_wakeup;
        break;
    case RULE_CMD_MODE_SINGLE:
        player_set_mode(SINGLE_PLAY);
        tts_play_audio_file(MODE_SINGLE_WAV_PATH);
        resume_after_handle = had_music_before_wakeup;
        break;
    case RULE_CMD_MODE_ORDER:
        player_set_mode(ORDER_PLAY);
        tts_play_audio_file(MODE_ORDER_WAV_PATH);
        resume_after_handle = had_music_before_wakeup;
        break;
    case RULE_CMD_PLAY_START:
        player_start_play();
        break;
    case RULE_CMD_PLAY_QUERY:
        if (try_music_lib_play(buf)) {
        } else {
            player_audio_focus_cancel_resume();
            tts_play_audio_file(FALLBACK_WAV_PATH);
        }
        break;
    case RULE_CMD_SWITCH_OFFLINE:
        player_switch_offline_mode();
        break;
    case RULE_CMD_SWITCH_ONLINE:
        player_switch_online_mode();
        break;
    case RULE_CMD_NONE:
    default:
        if (g_current_online_mode == ONLINE_MODE_NO) {
            player_audio_focus_cancel_resume();
            tts_play_audio_file(FALLBACK_WAV_PATH);
        } else {
            if (run_llm_and_tts(buf) == 0) {
            } else {
                player_audio_focus_cancel_resume();
                tts_play_audio_file(FALLBACK_WAV_PATH);
            }
        }
        break;
    }

    if (resume_after_handle)
    {
        player_audio_focus_prepare_resume();
        player_continue_play();
    }
}

static void select_read_kws(void)
{
    char buf[256] = {0};
    ssize_t ret = read(g_kws_fd, buf, sizeof(buf) - 1);
    if (ret < 0)
    {
        LOGE(TAG, "读取kws管道失败: %s", strerror(errno));
        return;
    }
    if (ret == 0)
    {
        LOGW(TAG, "kws管道对端关闭");
        FD_CLR(g_kws_fd, &READSET);
        close(g_kws_fd);
        g_kws_fd = -1;
        init_kws_fifo();
        update_max_fd();
        return;
    }

    buf[ret] = '\0';
    if (buf[ret - 1] == '\n') buf[ret - 1] = '\0';
    LOGI(TAG, "读取kws管道数据[%s]", buf);
}


static void select_read_player_ctrl(void)
{
    char buf[256] = {0};
    ssize_t ret = read(g_player_ctrl_fd, buf, sizeof(buf) - 1);
    if (ret < 0)
    {
        LOGE(TAG, "读取player控制管道失败: %s", strerror(errno));
        return;
    }
    if (ret == 0)
    {
        FD_CLR(g_player_ctrl_fd, &READSET);
        close(g_player_ctrl_fd);
        g_player_ctrl_fd = -1;
        init_player_ctrl_fifo();
        return;
    }

    if (strstr(buf, "tts:start") != NULL)
    {
        LOGI(TAG, "收到TTS开始事件");
        if (player_get_audio_focus() == AUDIO_FOCUS_MUSIC_PLAYING)
        {
            player_suspend_for_tts();
        }
        else if (player_get_audio_focus() == AUDIO_FOCUS_IDLE)
        {
            player_audio_focus_mark_tts_standalone();
        }
    }
    else if (strstr(buf, "tts:done") != NULL)
    {
        LOGI(TAG, "收到TTS结束事件");
        if (player_audio_focus_should_resume() && g_current_state != PLAY_STATE_STOP)
        {
            player_audio_focus_prepare_resume();
            player_continue_play();
        }
        else if (player_get_audio_focus() == AUDIO_FOCUS_TTS_PLAYING)
        {
            player_set_audio_focus(AUDIO_FOCUS_IDLE);
        }
    }
}


// 运行事件监听
void select_run(void)
{
    fd_set TMPSET;  // 临时文件描述符集合
    show_menu();    // 显示菜单
    
    LOGI(TAG, "select 监听开始：g_max_fd=%d, g_asr_fd=%d", g_max_fd, g_asr_fd);
    
    while (!g_player_shutdown_requested)
    {
        player_process_async_events();
        TMPSET = READSET;   // 将全局变量赋给临时变量， 防止因为被监听到导致的取消监听
        int ret = select(g_max_fd+1, &TMPSET, NULL, NULL, NULL);
        if(-1 == ret)
        {
            // 排除信号对select的干扰
            if(EINTR == errno) {
                player_process_async_events();
                continue;
            }
            LOGE(TAG, "select错误: %s", strerror(errno));
            return;
        }
        player_process_async_events();

        if(FD_ISSET(0, &TMPSET)){
            select_read_stdio();
        }
        if(g_socket_fd >= 0 && FD_ISSET(g_socket_fd, &TMPSET)){
            select_read_socket();
        }
        if(g_button_fd >= 0 && FD_ISSET(g_button_fd, &TMPSET)){
            device_read_button();
        }
        if(g_asr_fd >= 0 && FD_ISSET(g_asr_fd, &TMPSET)){
            select_read_asr();
        }
        if(g_kws_fd >= 0 && FD_ISSET(g_kws_fd, &TMPSET)){
            select_read_kws();
        }
        if(g_player_ctrl_fd >= 0 && FD_ISSET(g_player_ctrl_fd, &TMPSET)){
            select_read_player_ctrl();
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
    if (socket_recv_data(buf) != 0) {
        return;
    }
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


