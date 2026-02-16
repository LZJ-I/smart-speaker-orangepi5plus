# 编译器设置
CC = gcc
CFLAGS = -Wall -g -I/usr/include/json-c

# 源文件和目标文件
OBJS = main.o select.o link.o shm.o socket.o device.o player.o
LIBS = -lpthread -ljson-c -lasound -lm

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

# 清理
clean:
	rm -f $(TARGET) $(OBJS) testServer
