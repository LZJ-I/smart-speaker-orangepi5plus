CC = gcc
TARGET = ../../tts_process
CFLAGS = -Wall -g -DPROCESS_MODE -I../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/include/ -I../tts -I../common -I.. -I../../ipc
SHERPA_LIB = ../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu/lib
LIBS = -lasound -lonnxruntime -lsherpa-onnx-c-api -lpthread -L$(SHERPA_LIB)
RPATH = -Wl,-rpath,'$$ORIGIN/3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu/lib'

all: $(TARGET)

$(TARGET): main.o tts_playback.o tts_ipc_handler.o ../tts/alsa_output.o ../tts/sherpa_tts.o ../../ipc/ipc_message.o
	$(CC) -o $(TARGET) main.o tts_playback.o tts_ipc_handler.o ../tts/alsa_output.o ../tts/sherpa_tts.o ../../ipc/ipc_message.o $(LIBS) $(RPATH)
	rm -f main.o tts_playback.o tts_ipc_handler.o ../tts/alsa_output.o ../tts/sherpa_tts.o ../../ipc/ipc_message.o

main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o main.o

tts_playback.o: tts_playback.c tts_playback.h
	$(CC) $(CFLAGS) -c tts_playback.c -o tts_playback.o

tts_ipc_handler.o: tts_ipc_handler.c tts_ipc_handler.h
	$(CC) $(CFLAGS) -c tts_ipc_handler.c -o tts_ipc_handler.o

../tts/alsa_output.o: ../tts/alsa_output.c
	$(CC) $(CFLAGS) -c ../tts/alsa_output.c -o ../tts/alsa_output.o

../tts/sherpa_tts.o: ../tts/sherpa_tts.c
	$(CC) $(CFLAGS) -c ../tts/sherpa_tts.c -o ../tts/sherpa_tts.o

../../ipc/ipc_message.o: ../../ipc/ipc_message.c
	$(CC) $(CFLAGS) -c ../../ipc/ipc_message.c -o ../../ipc/ipc_message.o

clean:
	rm -f main.o tts_playback.o tts_ipc_handler.o ../tts/alsa_output.o ../tts/sherpa_tts.o ../../ipc/ipc_message.o $(TARGET)
