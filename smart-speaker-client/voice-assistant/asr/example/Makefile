CC = gcc
TARGET = asr_test
SRCS = main.c \
       ../../common/alsa.c \
       ../../common/mysamplerate.c \
       ../sherpa_asr.c \
       ../../../debug_log.c
OBJS = $(SRCS:.c=.o)

CFLAGS = -Wall -g -DKWS_TEST_MODE -I../../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/include/ -I../../common -I.. -I. -I../../..
SHRP_LIB_REL = 3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu/lib
LIBS = -lasound -lonnxruntime -lsherpa-onnx-c-api -L../../../$(SHRP_LIB_REL) -Wl,-rpath,'$$ORIGIN/../../../$(SHRP_LIB_REL)' -lsamplerate -pthread

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LIBS)
	rm -f $(OBJS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)
