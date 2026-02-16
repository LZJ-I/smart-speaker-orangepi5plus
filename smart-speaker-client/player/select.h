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


// 语音合成：xxx
void tts_play_text(char * text);
#endif
