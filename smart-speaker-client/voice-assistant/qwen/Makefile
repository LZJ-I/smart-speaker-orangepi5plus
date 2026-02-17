CC = gcc
TARGET = run
SRCS = qwen.c
OBJS = $(SRCS:.c=.o)	# 目标文件列表(将SRCS中的.c替换为.o)

LIBS = -ljson-c
CFLAGS = -I/usr/include/json-c



# 编译规则
$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LIBS)
	rm -f $(OBJS)

# 编译目标文件规则
%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)



clean:
	rm -f $(OBJS) $(TARGET)
	rm -f $(OBJS)
