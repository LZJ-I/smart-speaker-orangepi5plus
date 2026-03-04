CC = gcc
CFLAGS = -Wall -g -I/usr/include/json-c $(shell pkg-config --cflags gstreamer-1.0 2>/dev/null)

OBJS = main.o select.o select_text.o select_music_llm.o link.o shm.o socket.o socket_report.o device.o player.o player_fifo.o player_gst.o rule_match.o music_lib_bridge.o ../ipc/ipc_message.o ../voice-assistant/llm/llm.o
LIBS = -lpthread -ljson-c -lasound -lm -ldl $(shell pkg-config --libs gstreamer-1.0 2>/dev/null)

# 可执行文件
TARGET = run

# 默认目标
.PHONY: all clean
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LIBS)
	rm -f $(OBJS)

# 模式规则
%.o: %.c %.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

../ipc/ipc_message.o: ../ipc/ipc_message.c
	$(CC) $(CFLAGS) -c ../ipc/ipc_message.c -o ../ipc/ipc_message.o

../voice-assistant/llm/llm.o: ../voice-assistant/llm/llm.c
	$(CC) $(CFLAGS) -c ../voice-assistant/llm/llm.c -o ../voice-assistant/llm/llm.o

# 清理
clean:
	rm -f $(TARGET) $(OBJS) testServer
