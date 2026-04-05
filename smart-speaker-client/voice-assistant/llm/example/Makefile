CC = gcc
TARGET = llm_test
SRCS = main.c \
       ../llm.c \
       ../../../debug_log.c
OBJS = $(SRCS:.c=.o)

CFLAGS = -Wall -g -DKWS_TEST_MODE -I../../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/include/ -I../../common -I.. -I. -I/usr/include/json-c -I../../..
LIBS = -ljson-c -pthread

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LIBS)
	rm -f $(OBJS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)
