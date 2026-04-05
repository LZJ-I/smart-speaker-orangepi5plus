CC = gcc
TARGET = ../../asr_kws_process
CFLAGS = -Wall -g -DPROCESS_MODE -I../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/include/ -I../common -I../asr -I../kws -I../llm -I.. -I../../ipc
SHERPA_LIB = ../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu/lib
LIBS = -lasound -lonnxruntime -lsherpa-onnx-c-api -L$(SHERPA_LIB) -lsamplerate -ljson-c
RPATH = -Wl,-rpath,'$$ORIGIN/3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu/lib'

all: $(TARGET)

$(TARGET): main.o asr_kws_pipe.o ../common/alsa.o ../common/mysamplerate.o ../asr/sherpa_asr.o ../kws/sherpa_kws.o ../llm/llm.o ../../ipc/ipc_message.o
	$(CC) -o $(TARGET) main.o asr_kws_pipe.o ../common/alsa.o ../common/mysamplerate.o ../asr/sherpa_asr.o ../kws/sherpa_kws.o ../llm/llm.o ../../ipc/ipc_message.o $(LIBS) $(RPATH)
	rm -f main.o asr_kws_pipe.o ../common/alsa.o ../common/mysamplerate.o ../asr/sherpa_asr.o ../kws/sherpa_kws.o ../llm/llm.o ../../ipc/ipc_message.o

main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o main.o

asr_kws_pipe.o: asr_kws_pipe.c asr_kws_pipe.h
	$(CC) $(CFLAGS) -c asr_kws_pipe.c -o asr_kws_pipe.o

../common/alsa.o: ../common/alsa.c
	$(CC) $(CFLAGS) -c ../common/alsa.c -o ../common/alsa.o

../common/mysamplerate.o: ../common/mysamplerate.c
	$(CC) $(CFLAGS) -c ../common/mysamplerate.c -o ../common/mysamplerate.o

../asr/sherpa_asr.o: ../asr/sherpa_asr.c
	$(CC) $(CFLAGS) -c ../asr/sherpa_asr.c -o ../asr/sherpa_asr.o

../kws/sherpa_kws.o: ../kws/sherpa_kws.c
	$(CC) $(CFLAGS) -c ../kws/sherpa_kws.c -o ../kws/sherpa_kws.o

../llm/llm.o: ../llm/llm.c
	$(CC) $(CFLAGS) -c ../llm/llm.c -o ../llm/llm.o

../../ipc/ipc_message.o: ../../ipc/ipc_message.c
	$(CC) $(CFLAGS) -c ../../ipc/ipc_message.c -o ../../ipc/ipc_message.o

clean:
	rm -f main.o asr_kws_pipe.o ../common/alsa.o ../common/mysamplerate.o ../asr/sherpa_asr.o ../kws/sherpa_kws.o ../llm/llm.o ../../ipc/ipc_message.o $(TARGET)
