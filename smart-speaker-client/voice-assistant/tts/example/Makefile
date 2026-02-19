CC = gcc

TARGETS = tts_test play_wav
TTS_SRCS = tts_test.c \
           ../sherpa_tts.c \
           ../alsa_output.c
WAV_SRCS = play_wav.c \
           ../alsa_output.c

TTS_OBJS = $(TTS_SRCS:.c=.o)
WAV_OBJS = $(WAV_SRCS:.c=.o)

CFLAGS = -Wall -g -DKWS_TEST_MODE -I../../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/include/ -I../../common -I.. -I.
LIBS = -lasound -lonnxruntime -lsherpa-onnx-c-api -L../../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/lib

all: $(TARGETS)

tts_test: $(TTS_OBJS)
	$(CC) -o $@ $(TTS_OBJS) $(LIBS)

play_wav: $(WAV_OBJS)
	$(CC) -o $@ $(WAV_OBJS) $(LIBS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(TTS_OBJS) $(WAV_OBJS) $(TARGETS)
