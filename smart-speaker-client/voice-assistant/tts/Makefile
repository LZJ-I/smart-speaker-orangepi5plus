CC = gcc
TARGET = tts
SRCS = main.c alsa_output.c tts.c
OBJS = $(SRCS:.c=.o)	# 目标文件列表(将SRCS中的.c替换为.o)

# 编译选项 -Wall: 显示所有警告信息  -g 生成调试信息
CFLAGS = -Wall -g -I/root/sherpa/0-sherpa-ONNX框架/sherpa-onnx-v1.12.17-linux-aarch64-jni/include/

# 链接库选项
LIBS = -lasound -lonnxruntime -lsherpa-onnx-c-api -L/root/sherpa/0-sherpa-ONNX框架/sherpa-onnx-v1.12.17-linux-aarch64-shared-cpu/lib

# 编译规则
$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LIBS)
	rm -f $(OBJS)

# 编译目标文件规则
%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

# %.o：目标文件模式，表示所有以.o结尾的文件
# %.c：依赖文件模式，表示所有以.c结尾的文件
# $@：表示目标文件（即当前要生成的.o文件）
# $<：表示第一个依赖文件（即对应的.c文件）


clean:
	rm -f $(OBJS) $(TARGET)
	rm -f $(OBJS)
# clean：清理规则
# rm -f：删除文件命令，-f 表示强制删除，不提示确认
# $(OBJS) $(TARGET)：要删除的文件列表，即所有编译生成的.o文件和可执行文件
