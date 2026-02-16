#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <alsa/asoundlib.h>


// 声卡名 (使用aplay -l 查看)
// #define CARD "hw:AudioPCI"   // 虚拟机的声卡名
#define CARD "hw:audiocodec"           // 嵌入式设备的声卡名
// 控制单元(使用amixer 查看)
// #define ELEM "Master"        // 虚拟机的控制单元
#define ELEM "lineout volume"           // 嵌入式设备的控制单元

// 舍弃此方法，改用动态查找 gpio-keys 设备节点
// #define USER_BUTTON_DEV "/dev/input/event2"	// 用户按键设备节点

#include "device.h"
#include <math.h>
#include <assert.h>

extern int g_button_fd; // 按键文件描述符

typedef enum{
	STATE_IDLE,             // 空闲状态
	STATE_FIRST_PRESS,      // 第一次按键按下
	STATE_FIRST_RELEASE,    // 第一次按键释放
	STATE_SECOND_PRESS      // 第二次按键按下
}BUTTON_STATE;


//设置系统音量（0-100范围）
int device_set_volume(int volume);

//获取当前系统音量（0-100范围）
int device_get_volume(int *volume);

// 调整音量
int device_adjust_volume(int adjust_type);

// 初始化按键
int key_init(void);

// 按钮短按事件处理函数
void button_handler(int sig);

// 处理按键事件
void device_read_button(void);
#endif

