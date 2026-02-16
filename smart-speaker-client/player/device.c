#define LOG_LEVEL 4
#include "../debug_log.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <signal.h>
#include <sys/time.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>

#include "device.h"
#include "select.h"
#include "player.h"

#define TAG "DEVICE"

int g_button_fd = -1;   // 按键文件描述符
BUTTON_STATE state = STATE_IDLE;    // 按键状态
unsigned long old, new;     // 用于判断长按和短按
struct itimerval tv;        // 按键定时器结构体
int g_current_vol = 0;      // 当前音量

//获取音量控制元素（ALSA混音器元素）
static int get_mixer_elem(snd_mixer_t** snd_mixer, snd_mixer_elem_t** mixer_elem)
{
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *elem = NULL;
    snd_mixer_selem_id_t *sid = NULL;
    // 打开一个空的混音设备
    if (snd_mixer_open(&mixer, 0) != 0) {
        LOGE(TAG, "snd_mixer_open error");
        return -1;
    }
    // 附加声卡设备
    if (snd_mixer_attach(mixer, CARD) != 0) {
        LOGE(TAG, "snd_mixer_attach error");
        snd_mixer_close(mixer);
        return -1;
    }
    // 注册混音器
    if (snd_mixer_selem_register(mixer, NULL, NULL) != 0) {
        LOGE(TAG, "snd_mixer_selem_register error");
        snd_mixer_close(mixer);
        return -1;
    }
    // 加载混音器配置
    if (snd_mixer_load(mixer) != 0) {
        LOGE(TAG, "snd_mixer_load error");
        snd_mixer_close(mixer);
        return -1;
    }
    // 设置元素ID
    snd_mixer_selem_id_alloca(&sid);        // 栈上分配空间
    snd_mixer_selem_id_set_index(sid, 0);   // 设置元素索引
    snd_mixer_selem_id_set_name(sid, ELEM); // 设置元素名称
    // 获取目标音量控制元素
    elem = snd_mixer_find_selem(mixer, sid);
    if (elem == NULL) {
        LOGE(TAG, "snd_mixer_find_selem error: 未找到音量元素[%s]", ELEM);
        snd_mixer_close(mixer);
        return -1;
    }
    // 输出结果
    *snd_mixer = mixer;
    *mixer_elem = elem;

    return 0;
}

//设置系统音量（0-100范围）
int device_set_volume(int volume)
{
    snd_mixer_t* mixer = NULL;
    snd_mixer_elem_t* elem = NULL;
    long vol_min = 0;  // 音量最小值（ALSA原生范围）
    long vol_max = 0;  // 音量最大值（ALSA原生范围）
    long target_vol = 0; // 转换后的目标音量（ALSA原生值）

    // 输入音量范围检查（0-100）
    if(volume < 0) volume = 0;
    if(volume > 100) volume = 100;


    // 获取混音器元素（失败直接返回）
    if (get_mixer_elem(&mixer, &elem) != 0) {
        LOGE(TAG, "device_set_volume error: 获取混音器元素失败");
        return -1;
    }
    // 获取ALSA原生音量范围
    if (snd_mixer_selem_get_playback_volume_range(elem, &vol_min, &vol_max) < 0) {
        LOGE(TAG, "snd_mixer_selem_get_playback_volume_range error");
        snd_mixer_close(mixer);
        return -1;
    }
    // 转换0-100音量到ALSA原生范围（四舍五入）
    target_vol = vol_min + round((vol_max - vol_min) * (volume / 100.0));
    // 设置所有声道音量（保证立体声平衡）
    if (snd_mixer_selem_set_playback_volume_all(elem, target_vol) < 0) {
        LOGE(TAG, "snd_mixer_selem_set_playback_volume_all error");
        snd_mixer_close(mixer);
        return -1;
    }
    // 关闭混音器
    snd_mixer_close(mixer);

    g_current_vol = volume;
    return 0; // 成功返回
}

//获取当前系统音量（0-100范围）
int device_get_volume(int *volume)
{
    /*
        通过此方法获取的音量会因为计算问题丢失精度。
        所以采取全局变量的方法， 将0-63的音量范围映射到0-100
    */
    *volume = g_current_vol;
    return 0;

#if 0
    snd_mixer_t *mixer = NULL;    // 混音设备句柄
    snd_mixer_elem_t *elem = NULL; // 音量控制元素
    long vol_min = 0;             // 音量最小值（ALSA原生范围）
    long vol_max = 0;             // 音量最大值（ALSA原生范围）
    long current_vol_alsa = 0;    // 当前音量（ALSA原生值）
    int current_vol = 0;          // 当前音量（0-100，转换后）
    // 获取混音器元素（失败直接返回）
    if (get_mixer_elem(&mixer, &elem) != 0) {
        fprintf(stderr, "[ERROR] get_mixer_elem error\n");
        return -1;
    }
    // 获取ALSA原生音量范围
    if (snd_mixer_selem_get_playback_volume_range(elem, &vol_min, &vol_max) < 0) {
        fprintf(stderr, "[ERROR] snd_mixer_selem_get_playback_volume_range error\n");
        snd_mixer_close(mixer);
        return -1;
    }
    // 获取左声道音量（默认立体声，左声道可代表整体音量）
    if (snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, &current_vol_alsa) < 0) {
        fprintf(stderr, "[ERROR] snd_mixer_selem_get_playback_volume error\n");
        snd_mixer_close(mixer);
        return -1;
    }
    // 反向转换：ALSA原生值 -> 0-100（四舍五入）
    current_vol = round((current_vol_alsa - vol_min) / (double)(vol_max - vol_min) * 100);
    // 边界修正（防止因浮点误差导致超出0-100范围）
    current_vol = (current_vol < 0) ? 0 : (current_vol > 100) ? 100 : current_vol;
    // 关闭混音器（释放资源）
    snd_mixer_close(mixer);

    // 返回当前音量给调用者
    *volume = current_vol;
    return 0;
#endif
}



/**
 * @brief 调整系统音量（增减10%）
 * @param adjust_type 调整类型：0=减小10%音量，1=增加10%音量
 * @return 0：成功，-1：失败
 * @note 音量始终限制在0-100范围内，超出边界时自动截断
 */
int device_adjust_volume(int adjust_type)
{
    /*
        通过此方法获取的音量会因为计算问题丢失精度。
        所以采取全局变量的方法， 将0-63的音量范围映射到0-100
    */

    // 检查输入参数有效性（仅支持0或1）
    if (adjust_type != 0 && adjust_type != 1) {
        LOGE(TAG, "device_adjust_volume: 无效参数！仅支持 0(减10%%)或 1(加10%%)");
        return -1;
    }
    int current_vol = 0;
    int new_vol = 0;
    // 1. 获取当前音量
    if (device_get_volume(&current_vol) != 0) {
        LOGE(TAG, "device_adjust_volume: 获取当前音量失败");
        return -1;
    }

    // 2. 计算目标音量（增减10%）
    new_vol = (adjust_type == 1) ? (current_vol + 10) : (current_vol - 10);

    // 3. 确保音量在0-100有效范围（边界截断）
    new_vol = (new_vol < 0) ? 0 : (new_vol > 100) ? 100 : new_vol;

    // 4. 避免重复设置相同音量
    if (new_vol == current_vol) {
        LOGI(TAG, "========[INFO] 音量调整：当前已处于%s音量（%d%%）========",
                (adjust_type == 1) ? "最大" : "最小", current_vol);
        
        return 0;
    }

    // 5. 设置新音量
    if (device_set_volume(new_vol) != 0) {
        LOGE(TAG, "device_adjust_volume: 设置新音量失败");
        return -1;
    }

    // 输出调整结果（便于调试和日志记录）
    LOGI(TAG, "========[INFO] 音量调整成功：%d%% -> %d%%（%s10%%）========",
            current_vol, new_vol, (adjust_type == 1) ? "增加" : "减少");

    // 6. 检查是否成功设置新音量
    if(current_vol != new_vol)
    {
        return 0;
    }
    else
    {
        // 不太可能进入到这个分支，只是为了防止警告
        return -1;
    } 


#if 0
    int current_vol = 0;
    int new_vol = 0;

    // 检查输入参数有效性（仅支持0或1）
    if (adjust_type != 0 && adjust_type != 1) {
        fprintf(stderr, "[ERROR] device_adjust_volume: 无效参数！仅支持 0(减10%%)或 1(加10%%)\n");
        return -1;
    }

    // 1. 获取当前音量
    if (device_get_volume(&current_vol) != 0) {
        fprintf(stderr, "[ERROR] device_adjust_volume: 获取当前音量失败\n");
        return -1;
    }

    // 2. 计算目标音量（增减10%）
    new_vol = (adjust_type == 1) ? (current_vol + 10) : (current_vol - 10);

    // 3. 确保音量在0-100有效范围（边界截断）
    new_vol = (new_vol < 0) ? 0 : (new_vol > 100) ? 100 : new_vol;

    // 4. 避免重复设置相同音量
    if (new_vol == current_vol) {
        fprintf(stdout, "========[INFO] 音量调整：当前已处于%s音量（%d%%）========\n",
                (adjust_type == 1) ? "最大" : "最小", current_vol);
        
        return 0;
    }

    // 5. 设置新音量
    if (device_set_volume(new_vol) != 0) {
        fprintf(stderr, "[ERROR] device_adjust_volume: 设置新音量失败\n");
        return -1;
    }

    // 输出调整结果（便于调试和日志记录）
    fprintf(stdout, "========[INFO] 音量调整成功：%d%% -> %d%%（%s10%%）========\n",
            current_vol, new_vol, (adjust_type == 1) ? "增加" : "减少");

    // 6. 检查是否成功设置新音量
    if(current_vol != new_vol)
    {
        return 0;
    }
    else
    {
        // 不太可能进入到这个分支，只是为了防止警告
        return -1;
    } 
#endif
}


// = === = = = = == = 按键


// 按钮短按事件处理函数
void button_handler(int sig)
{
    // 用于暂停和继续播放
	LOGI(TAG, "短按");
    // 如果没有播放，那么播放
    if(g_current_state == PLAY_STATE_STOP)
    {   
        player_start_play();
    }
    // 如果播放 且 没有暂停，那么暂停播放
    else if(g_current_state == PLAY_STATE_PLAY && g_current_suspend == PLAY_SUSPEND_NO)
    {
        player_suspend_play();
    }
    // 如果播放 且 暂停，那么继续播放
    else if(g_current_state == PLAY_STATE_PLAY && g_current_suspend == PLAY_SUSPEND_YES)
    {
        player_continue_play();
    }


	state = STATE_IDLE;
}


// 动态查找 gpio-keys 设备节点
static int find_gpio_keys_device()
{
    struct dirent *entry;   // 目录项指针
    DIR *dir = opendir("/dev/input");   // 打开输入设备目录
    // 检查目录是否成功打开
    if (!dir) return -1;
    // 遍历目录中的所有文件
    while ((entry = readdir(dir)) != NULL) {
        // 过滤出 event 开头的文件
        if (strncmp(entry->d_name, "event", 5) == 0) {
            char path[512];
            int fd;
            char name[512] = "Unknown";
            // 构建完整路径
            snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
            // 打开文件描述符
            fd = open(path, O_RDONLY);
            if (fd >= 0) {
                // 获取设备名称
                ioctl(fd, EVIOCGNAME(sizeof(name)), name);
                // 检查设备名称是否包含 "gpio-keys"
                if (strstr(name, "gpio-keys") != NULL) {
                    close(fd);      // 关闭当前文件描述符
                    closedir(dir);  // 关闭目录流
                    // 找到 gpio-keys 设备，返回文件描述符
                    return open(path, O_RDONLY);  // 返回打开的文件描述符
                }
                close(fd);  // 关闭当前文件描述符
            }
        }
    }
    
    closedir(dir);
    return -1;
}


int key_init()
{
    // 查找 gpio-keys 设备节点, 打开后返回fd
    g_button_fd = find_gpio_keys_device();
    if (g_button_fd < 0) {
        LOGE(TAG, "查找设备节点失败，请检查路径信息是否正确!");
        return -1;
    }
    // 将文件描述符添加到select监听集合中
    FD_SET(g_button_fd, &READSET);
    // 更新select监听的文件描述符集合中的最大文件描述符值
    update_max_fd();

    return 0;
}

// 获取时间 
static unsigned long get_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// 处理按键事件
void device_read_button(void)
{
    struct input_event ev;
    // 阻塞等待事件发生
    int ret = read(g_button_fd, &ev, sizeof(ev));
    if (-1 == ret)
    {
        perror("read");
        return;
    }
    if (ev.type != EV_KEY)  return;

    // 按键按下
    if (ev.value == 1)
    {
        // 空闲状态下按下，记录时间，切换到第一次按下状态
        if (state == STATE_IDLE)
        {
            state = STATE_FIRST_PRESS;  // 切换到第一次按下状态
            old = get_time(); // 记录第一次按下的时间
        }
        // 第一次松开状态下按下，判断双击还是单击
        else if (state == STATE_FIRST_RELEASE)
        {
            state = STATE_IDLE;
            LOGI(TAG, "双击");
            // 下一首
            player_next_song();

            //取消定时器
            tv.it_value.tv_sec = 0;
            tv.it_value.tv_usec = 0;
            tv.it_interval.tv_sec = 0;
            tv.it_interval.tv_usec = 0;
            setitimer(ITIMER_REAL, &tv, NULL);
        }
    }
    // 按键松开
    else if (ev.value == 0)
    {
        // 第一次按下状态下松开
        if (state == STATE_FIRST_PRESS)
        {
            new = get_time();
            // 判断是否长按
            if (new - old > 300)
            {
                LOGI(TAG, "长按");
                // 上一首
                player_prev_song();
                state = STATE_IDLE;
            }
            else // 否则为第一次松开
            {
                state = STATE_FIRST_RELEASE;

                tv.it_value.tv_sec = 0;
                tv.it_value.tv_usec = 300 * 1000;   // 300ms后触发定时器

                tv.it_interval.tv_sec = 0;
                tv.it_interval.tv_usec = 0;     // 不需要重复触发

                //启动定时器
                setitimer(ITIMER_REAL, &tv, NULL);
            }
        }
    }
}
