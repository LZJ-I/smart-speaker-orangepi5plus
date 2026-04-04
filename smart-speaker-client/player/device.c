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
#include <string.h>

#define TAG "DEVICE"
#define DB_PERCENT_SCALE 2000.0

int g_button_fd = -1;   // 按键文件描述符
BUTTON_STATE state = STATE_IDLE;    // 按键状态
unsigned long old, new;     // 用于判断长按和短按
struct itimerval tv;        // 按键定时器结构体
int g_current_vol = 0;      // 当前音量

static int percent_to_playback_volume(snd_mixer_elem_t *elem, int volume, long *target_vol)
{
    long vol_min = 0;
    long vol_max = 0;
    long db_min = 0;
    long db_max = 0;

    if (elem == NULL || target_vol == NULL) return -1;
    if (snd_mixer_selem_get_playback_volume_range(elem, &vol_min, &vol_max) < 0) {
        LOGE(TAG, "snd_mixer_selem_get_playback_volume_range error");
        return -1;
    }
    if (vol_max <= vol_min) {
        *target_vol = vol_min;
        return 0;
    }
    if (volume <= 0) {
        *target_vol = vol_min;
        return 0;
    }
    if (volume >= 100) {
        *target_vol = vol_max;
        return 0;
    }
    if (snd_mixer_selem_get_playback_dB_range(elem, &db_min, &db_max) >= 0 && db_max > db_min) {
        double ratio = volume / 100.0;
        double target_db = db_max + DB_PERCENT_SCALE * log10(ratio);
        long target_db_long;
        if (target_db < db_min) target_db = db_min;
        if (target_db > db_max) target_db = db_max;
        target_db_long = (long)llround(target_db);
        if (snd_mixer_selem_ask_playback_dB_vol(elem, target_db_long, 0, target_vol) >= 0) {
            return 0;
        }
    }
    *target_vol = (long)llround(vol_min + (vol_max - vol_min) * (volume / 100.0));
    return 0;
}

static int playback_volume_to_percent(snd_mixer_elem_t *elem, long current_alsa, int *volume)
{
    long vol_min = 0;
    long vol_max = 0;
    long current_db = 0;
    long db_min = 0;
    long db_max = 0;

    if (elem == NULL || volume == NULL) return -1;
    if (snd_mixer_selem_get_playback_volume_range(elem, &vol_min, &vol_max) < 0) {
        return -1;
    }
    if (vol_max <= vol_min) {
        *volume = g_current_vol;
        return 0;
    }
    if (current_alsa <= vol_min) {
        *volume = 0;
        return 0;
    }
    if (current_alsa >= vol_max) {
        *volume = 100;
        return 0;
    }
    if (snd_mixer_selem_get_playback_dB_range(elem, &db_min, &db_max) >= 0 &&
        db_max > db_min &&
        snd_mixer_selem_ask_playback_vol_dB(elem, current_alsa, &current_db) >= 0) {
        double ratio = pow(10.0, (current_db - db_max) / DB_PERCENT_SCALE);
        int mapped = (int)llround(ratio * 100.0);
        if (mapped < 0) mapped = 0;
        if (mapped > 100) mapped = 100;
        *volume = mapped;
        return 0;
    }
    *volume = (int)llround((current_alsa - vol_min) * 100.0 / (double)(vol_max - vol_min));
    if (*volume < 0) *volume = 0;
    if (*volume > 100) *volume = 100;
    return 0;
}

static int get_mixer_elem(snd_mixer_t** snd_mixer, snd_mixer_elem_t** mixer_elem)
{
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *elem = NULL;
    snd_mixer_selem_id_t *sid = NULL;
    if (snd_mixer_open(&mixer, 0) != 0) {
        LOGE(TAG, "snd_mixer_open error");
        return -1;
    }
    if (snd_mixer_attach(mixer, CARD) != 0) {
        LOGE(TAG, "snd_mixer_attach error");
        snd_mixer_close(mixer);
        return -1;
    }
    if (snd_mixer_selem_register(mixer, NULL, NULL) != 0) {
        LOGE(TAG, "snd_mixer_selem_register error");
        snd_mixer_close(mixer);
        return -1;
    }
    if (snd_mixer_load(mixer) != 0) {
        LOGE(TAG, "snd_mixer_load error");
        snd_mixer_close(mixer);
        return -1;
    }
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, ELEM);
    elem = snd_mixer_find_selem(mixer, sid);
    if (elem == NULL) {
        static const char *playback_priority[] = { "PCM", "Playback", "Master", "Speaker", "Line", "Headphone", NULL };
        snd_mixer_elem_t *fallback = NULL;
        for (elem = snd_mixer_first_elem(mixer); elem; elem = snd_mixer_elem_next(elem)) {
            if (!snd_mixer_selem_has_playback_volume(elem)) continue;
            snd_mixer_selem_id_t *eid;
            snd_mixer_selem_id_alloca(&eid);
            snd_mixer_selem_get_id(elem, eid);
            const char *name = snd_mixer_selem_id_get_name(eid);
            for (const char **p = playback_priority; *p; p++) {
                if (name && strstr(name, *p)) {
                    *snd_mixer = mixer;
                    *mixer_elem = elem;
                    return 0;
                }
            }
            if (fallback == NULL) fallback = elem;
        }
        if (fallback) {
            *snd_mixer = mixer;
            *mixer_elem = fallback;
            return 0;
        }
        LOGE(TAG, "未找到播放音量控件");
        snd_mixer_close(mixer);
        return -1;
    }
    *snd_mixer = mixer;
    *mixer_elem = elem;
    return 0;
}

//设置系统音量（0-100范围）
int device_set_volume(int volume)
{
    snd_mixer_t* mixer = NULL;
    snd_mixer_elem_t* elem = NULL;
    long target_vol = 0; // 转换后的目标音量（ALSA原生值）

    // 输入音量范围检查（0-100）
    if(volume < 0) volume = 0;
    if(volume > 100) volume = 100;


    // 获取混音器元素（失败直接返回）
    if (get_mixer_elem(&mixer, &elem) != 0) {
        LOGE(TAG, "device_set_volume error: 获取混音器元素失败");
        return -1;
    }
    if (percent_to_playback_volume(elem, volume, &target_vol) != 0) {
        snd_mixer_close(mixer);
        return -1;
    }
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

int device_get_volume(int *volume)
{
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *elem = NULL;
    long current_alsa = 0;
    if (get_mixer_elem(&mixer, &elem) != 0)
        return -1;
    if (snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, &current_alsa) < 0) {
        snd_mixer_close(mixer);
        return -1;
    }
    if (playback_volume_to_percent(elem, current_alsa, volume) != 0) {
        snd_mixer_close(mixer);
        return -1;
    }
    snd_mixer_close(mixer);
    g_current_vol = *volume;
    return 0;
}



/**
 * @brief 调整系统音量（增减10%）
 * @param adjust_type 调整类型：0=减小10%音量，1=增加10%音量
 * @return 0：成功，-1：失败
 * @note 音量始终限制在0-100范围内，超出边界时自动截断
 */
int device_adjust_volume(int adjust_type)
{
    if (adjust_type != 0 && adjust_type != 1) {
        LOGE(TAG, "device_adjust_volume: 无效参数");
        return -1;
    }
    int current_vol = 0;
    if (device_get_volume(&current_vol) != 0)
        return -1;
    int new_vol = (adjust_type == 1) ? (current_vol + 10) : (current_vol - 10);
    new_vol = (new_vol < 0) ? 0 : (new_vol > 100) ? 100 : new_vol;
    if (new_vol == current_vol) {
        LOGI(TAG, "音量已处于%s（%d%%）", (adjust_type == 1) ? "最大" : "最小", current_vol);
        return 0;
    }
    if (device_set_volume(new_vol) != 0)
        return -1;
    LOGI(TAG, "音量 %d%% -> %d%%", current_vol, new_vol);
    return 0;
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
