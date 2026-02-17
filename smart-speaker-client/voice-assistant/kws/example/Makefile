CC = gcc
TARGET = kws_test
SRCS = main.c \
       ../../common/alsa.c \
       ../sherpa_kws.c
OBJS = $(SRCS:.c=.o)

CFLAGS = -Wall -g -DKWS_TEST_MODE -I../../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/include/ -I../../common -I.. -I.
LIBS = -lasound -lonnxruntime -lsherpa-onnx-c-api -L../../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/lib

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LIBS)
	rm -f $(OBJS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)
