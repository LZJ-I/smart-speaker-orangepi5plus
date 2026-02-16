#ifndef __PLAYER_H__
#define __PLAYER_H__ 

// #define ONNINE_URL "/home/lzj/lzj_project/Music/"
// #define MUSIC_PATH  "/home/lzj/lzj_project/Music/"
// #define FIFO_PATH   "/home/lzj/fifo/cmd_fifo"           // mpylayer控制管道的路径
// #define CMD_FIFO_PATH "file=/home/lzj/fifo/cmd_fifo"    // 用于mplayer命令 指定管道
// #define MPLAYER_PATH "/usr/bin/mplayer"                 // mplayer的路径

#define UDISK_MOUNT_PATH "/mnt/usb/"    // U盘挂载路径

//#define ONNINE_URL "/root/Music/"       // 在线音乐目录
#define ONNINE_URL "http://10.102.178.47/music/"       // 在线音乐目录
// #define MUSIC_PATH  "/root/Music/"      // 本地音乐目录（debug）
#define MUSIC_PATH  UDISK_MOUNT_PATH      // 本地音乐目录（release）

#define FIFO_PATH   "../fifo/cmd_fifo"           // mpylayer控制管道的路径
#define CMD_FIFO_PATH "file=../fifo/cmd_fifo"    // 用于mplayer命令 指定管道
#define MPLAYER_PATH "/usr/bin/mplayer"                 // mplayer的路径
#define DEFAULT_VOLUME 60      // 默认音量

#define ASR_PIPE_PATH "../fifo/asr_fifo"    // asr模型的管道文件路径
#define KWS_PIPE_PATH "../fifo/kws_fifo"    // kws模型的管道文件路径
#define TTS_PIPE_PATH "../fifo/tts_fifo"    // tts模型的管道文件路径

#define UDISK_PATH_YES1 "/dev/sdb1"  // U盘设备存在
#define UDISK_PATH_YES2 "/dev/sdc1"  // U盘设备存在
#define UDISK_PATH_YES3 "/dev/sda1"  // U盘设备存在



// 播放模式
enum {
    ORDER_PLAY,     // 顺序播放     //0
    RANDOM_PLAY,    // 随机播放     //1
    SINGLE_PLAY     // 单曲循环     //2
};

// 播放状态
enum {
    PLAY_STATE_STOP,    // 停止播放
    PLAY_STATE_PLAY     // 开始播放
};

// 暂停状态
enum {
    PLAY_SUSPEND_NO,  // 正在播放
    PLAY_SUSPEND_YES  // 暂停播放
};

// 在线模式
enum{
    ONLINE_MODE_NO,     // 离线模式
    ONLINE_MODE_YES     // 在线模式
};

extern int g_current_state;            // 当前播放状态
extern int g_current_suspend;          // 当前是否处于暂停状态
extern int g_current_online_mode;       // 当前是否处于在线模式

extern int g_asr_fd;  // asr语音识别管道文件描述符
extern int g_kws_fd;  // kws关键词识别管道文件描述符
extern int g_tts_fd;  // tts语音合成管道文件描述符

// 开始播放
void player_start_play();
// 结束播放
void player_stop_play(void);
// 播放继续
void player_continue_play();
// 播放暂停
void player_suspend_play();
// 播放下一首
int player_next_song();
// 播放上一首
int player_prev_song();
// 切换模式
void player_set_mode(int mode);
// 播放指定音乐人的歌曲
void player_singer_play(const char *singer);

// 初始化asr的管道（打开并监听）
int init_asr_fifo();
// 初始化kws的管道（打开并监听）
int init_kws_fifo();
// 初始化tts的管道（打开）
int init_tts_fifo();

// 切换到离线模式
int player_switch_offline_mode(void);
// 切换到在线模式
int player_switch_online_mode(void);
#endif
