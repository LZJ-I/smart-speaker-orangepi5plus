#ifndef __SHM_H__
#define __SHM_H__

#include <sys/types.h>
#include <unistd.h>
#include "link.h"

#define SHMKEY 0x1234       // 创建共享内存的key
#define SHMSIZE 4096        //  共享内存的大小以页为单位，一页为4096。也就是说至少创建一个页大小的共享内存
#define SEMKEY 0x1235       // 创建信号量的key

// 信号量联合体
union semun {
    int              val;    /* Value for SETVAL */
    struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
    unsigned short  *array;  /* Array for GETALL, SETALL */
    struct seminfo  *__buf;  /* Buffer for IPC_INFO
                                (Linux-specific) */
};

// 共享内存数据结构体
typedef struct {
    char current_music[MUSIC_MAX_NAME];    // 当前播放的音乐名称
    char current_singer[SINGER_MAX_NAME];   // 当前播放的歌手名称
    int current_mode;           // 0-顺序播放 1-随机播放 2-单曲循环
    pid_t parent_pid;           // 父进程的pid
    pid_t child_pid;            // 子进程的pid
    pid_t grand_pid;           // 孙子进程的pid
} Shm_Data;




// 初始化共享内存
int shm_init();
// 初始化信号量
int shm_sem_init(void);

// 获取共享内存数据
void shm_get(Shm_Data* data);
// 设置共享内存数据
void shm_set(Shm_Data* data);
#endif
