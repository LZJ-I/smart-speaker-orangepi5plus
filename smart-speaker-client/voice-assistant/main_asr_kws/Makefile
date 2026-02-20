CC = gcc
TARGET = ../../asr_kws_process
CFLAGS = -Wall -g -DPROCESS_MODE -I../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/include/ -I../common -I../asr -I../kws -I../llm -I..
LIBS = -lasound -lonnxruntime -lsherpa-onnx-c-api -L../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/lib -lsamplerate -ljson-c

all: $(TARGET)

$(TARGET): main.o ../common/alsa.o ../common/mysamplerate.o ../asr/sherpa_asr.o ../kws/sherpa_kws.o ../llm/llm.o
	$(CC) -o $(TARGET) main.o ../common/alsa.o ../common/mysamplerate.o ../asr/sherpa_asr.o ../kws/sherpa_kws.o ../llm/llm.o $(LIBS)
	rm -f main.o ../common/alsa.o ../common/mysamplerate.o ../asr/sherpa_asr.o ../kws/sherpa_kws.o ../llm/llm.o

main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o main.o

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

clean:
	rm -f main.o ../common/alsa.o ../common/mysamplerate.o ../asr/sherpa_asr.o ../kws/sherpa_kws.o ../llm/llm.o $(TARGET)
