CC = gcc
TARGET = voice_assistant
SRCS = main.c \
       common/alsa.c \
       common/mysamplerate.c \
       asr/sherpa_asr.c \
       kws/sherpa_kws.c
OBJS = $(SRCS:.c=.o)

CFLAGS = -Wall -g -I../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/include/ -Icommon -Iasr -Ikws -I.
LIBS = -lasound -lonnxruntime -lsherpa-onnx-c-api -L../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/lib -lsamplerate

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LIBS)
	rm -f $(OBJS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)
