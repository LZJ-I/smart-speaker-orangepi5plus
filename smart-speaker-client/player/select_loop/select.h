#ifndef __SELECT_H__
#define __SELECT_H__ 

#include <sys/select.h>

extern fd_set READSET;  // fd的集合
extern int g_max_fd;    // 最大文件描述符

// 初始化select
int select_init();

// 更新最大文件描述符
void update_max_fd();

// 运行select循环监听
void select_run();

void select_on_player_stopped(void);


/* 返回 0 表示已成功写入 TTS 管道（将收到 tts:start / tts:done） */
int tts_play_text(const char *text);
int tts_play_audio_file(const char *path);
#endif
