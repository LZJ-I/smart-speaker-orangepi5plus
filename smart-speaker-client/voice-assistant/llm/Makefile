CC = gcc
TARGET = qwen_test
SRCS = main.c \
       sherpa_qwen.c
OBJS = $(SRCS:.c=.o)

CFLAGS = -Wall -g -DKWS_TEST_MODE -I../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/include/ -I../common -I. -I/usr/include/json-c
LIBS = -ljson-c

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LIBS)
	rm -f $(OBJS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)
