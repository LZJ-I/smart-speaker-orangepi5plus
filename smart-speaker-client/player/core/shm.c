#define LOG_LEVEL 4
#include "debug_log.h"
#include "shm.h"

#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>  // 用于strerror获取错误详情
#include "player.h"
#include <sys/sem.h>  // 信号量

#define TAG "SHM"

int g_shmid = 0;      // 共享内存的id标识符

static int g_semid = 0;      // 信号量id标识符
static void *g_shm_base = NULL;

void shm_detach(void)
{
    if (g_shm_base != NULL && g_shm_base != (void *)-1) {
        (void)shmdt(g_shm_base);
        g_shm_base = NULL;
    }
}

int shm_init()
{
    Shm_Data shm_data = {0};
    int created = 0;
    void *addr;

    if (g_shm_base != NULL) {
        return 0;
    }

    g_shmid = shmget(SHMKEY, SHMSIZE, IPC_EXCL | IPC_CREAT | 0664);
    if (-1 == g_shmid) {
        if (errno != EEXIST) {
            LOGE(TAG, "shmget failed: %s", strerror(errno));
            return -1;
        }
        g_shmid = shmget(SHMKEY, SHMSIZE, 0664);
        if (-1 == g_shmid) {
            LOGE(TAG, "attach existing shm failed: %s", strerror(errno));
            return -1;
        }
    } else {
        created = 1;
    }

    addr = shmat(g_shmid, NULL, 0);
    if ((void *)-1 == addr) {
        LOGE(TAG, "shmat failed (shm_init): %s", strerror(errno));
        return -1;
    }

    if (created) {
        memset(addr, 0, sizeof(Shm_Data));
        shm_data.parent_pid = getpid();
        shm_data.current_mode = ORDER_PLAY;
        memcpy(addr, &shm_data, sizeof(Shm_Data));
    }

    g_shm_base = addr;
    return 0;
}

// 初始化信号量
int shm_sem_init(void)
{ 
    // 创建信号量
    g_semid = semget(SEMKEY, 1, IPC_CREAT | IPC_EXCL | 0664);   // 创建信号量，所有用户可读
    if(-1 == g_semid)
    {
        if (errno != EEXIST) {
            LOGE(TAG, "信号量初始化错误%s", strerror(errno));
            return -1;
        }
        g_semid = semget(SEMKEY, 1, 0664);
        if (-1 == g_semid) {
            LOGE(TAG, "获取已有信号量失败%s", strerror(errno));
            return -1;
        }
        return 0;
    }
    // 初始化信号量
    union semun s;
    s.val = 1;  //初值为1
    if(-1 == semctl(g_semid, 0, SETVAL, s))
    {
        LOGE(TAG, "创建信号量错误%s", strerror(errno));
        semctl(g_semid, 0, IPC_RMID, NULL); //删除信号量
        return -1;
    }
    return 0;
}

// 信号量加锁
static void shm_sem_p()
{
    struct sembuf buf;
    buf.sem_num = 0;
    buf.sem_op = -1;    // 减1
    buf.sem_flg = SEM_UNDO; // 退出时取消操作，防止挂掉
    // 锁定信号量
    if(semop(g_semid, &buf, 1) == -1)
    {
        LOGE(TAG, "共享内存 信号量加锁失败");
    }
}

// 信号量解锁
static void shm_sem_v()
{
    struct sembuf buf;
    buf.sem_num = 0;
    buf.sem_op = 1;
    buf.sem_flg = SEM_UNDO; // 退出时取消操作，防止挂掉
    // 解锁信号量
    if(semop(g_semid, &buf, 1) == -1)
    {
        LOGE(TAG, "共享内存 信号量解锁失败");
    }
}



void shm_get(Shm_Data *data)
{
    if (data == NULL || g_shm_base == NULL || g_shm_base == (void *)-1) {
        return;
    }
    shm_sem_p();
    memcpy(data, g_shm_base, sizeof(Shm_Data));
    shm_sem_v();
}

void shm_set(Shm_Data *data)
{
    if (data == NULL || g_shm_base == NULL || g_shm_base == (void *)-1) {
        return;
    }
    shm_sem_p();
    memcpy(g_shm_base, data, sizeof(Shm_Data));
    shm_sem_v();
}
