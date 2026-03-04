#!/bin/bash

# ==============================配置参数==============================
SHM_KEY="1234"
SEM_KEY="1235"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PIPE_PATH="$SCRIPT_DIR/../fifo"      # 管道路径
# ====================================================================

# 1. 删除旧的共享内存
shmid=$(ipcs -m | grep "$SHM_KEY" | awk '{print $2}')
if [ ! -z "$shmid" ]; then
    ipcrm -m $shmid
    echo "删除旧共享内存成功（shmkey: $SHM_KEY, shmid: $shmid）"
fi

# 2. 删除旧的信号量
semid=$(ipcs -s | grep "$SEM_KEY" | awk '{print $2}')
if [ ! -z "$semid" ]; then
    ipcrm -s $semid
    echo "删除旧信号量成功（semkey: $SEM_KEY, semid: $semid）"
fi

# 3. 确保管道目录和文件存在
mkdir -p $PIPE_PATH  # 创建管道目录（如果不存在）

rm -f $PIPE_PATH/mpv_socket
if [ ! -p "$PIPE_PATH/cmd_fifo" ]; then
    mkfifo $PIPE_PATH/cmd_fifo
    chmod 777 $PIPE_PATH/cmd_fifo
    echo "创建播放控制管道: $PIPE_PATH/cmd_fifo"
fi
if [ ! -p "$PIPE_PATH/asr_fifo" ]; then
    mkfifo $PIPE_PATH/asr_fifo
    chmod 777 $PIPE_PATH/asr_fifo
    echo "创建asr语音识别管道成功：$PIPE_PATH/asr_fifo"
fi

if [ ! -p "$PIPE_PATH/kws_fifo" ]; then
    mkfifo $PIPE_PATH/kws_fifo
    chmod 777 $PIPE_PATH/kws_fifo
    echo "创建kws关键词识别管道成功：$PIPE_PATH/kws_fifo"
fi

if [ ! -p "$PIPE_PATH/tts_fifo" ]; then
    mkfifo $PIPE_PATH/tts_fifo
    chmod 777 $PIPE_PATH/tts_fifo
    echo "创建tts语音合成管道成功：$PIPE_PATH/tts_fifo"
fi

if [ ! -p "$PIPE_PATH/asr_ctrl_fifo" ]; then
    mkfifo $PIPE_PATH/asr_ctrl_fifo
    chmod 777 $PIPE_PATH/asr_ctrl_fifo
    echo "创建asr控制管道成功：$PIPE_PATH/asr_ctrl_fifo"
fi

if [ ! -p "$PIPE_PATH/player_ctrl_fifo" ]; then
    mkfifo $PIPE_PATH/player_ctrl_fifo
    chmod 777 $PIPE_PATH/player_ctrl_fifo
    echo "创建player控制管道成功：$PIPE_PATH/player_ctrl_fifo"
fi

if [ ! -p "$PIPE_PATH/tts_wake_done_fifo" ]; then
    mkfifo $PIPE_PATH/tts_wake_done_fifo
    chmod 777 $PIPE_PATH/tts_wake_done_fifo
    echo "创建tts唤醒完成管道成功：$PIPE_PATH/tts_wake_done_fifo"
fi
