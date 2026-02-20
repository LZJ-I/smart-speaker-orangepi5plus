CC = gcc
TARGET = ../../tts_process
CFLAGS = -Wall -g -DPROCESS_MODE -I../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/include/ -I../tts -I../common -I..
LIBS = -lasound -lonnxruntime -lsherpa-onnx-c-api -lpthread -L../../3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/lib

all: $(TARGET)

$(TARGET): main.o ../tts/alsa_output.o ../tts/sherpa_tts.o
	$(CC) -o $(TARGET) main.o ../tts/alsa_output.o ../tts/sherpa_tts.o $(LIBS)
	rm -f main.o ../tts/alsa_output.o ../tts/sherpa_tts.o

main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o main.o

../tts/alsa_output.o: ../tts/alsa_output.c
	$(CC) $(CFLAGS) -c ../tts/alsa_output.c -o ../tts/alsa_output.o

../tts/sherpa_tts.o: ../tts/sherpa_tts.c
	$(CC) $(CFLAGS) -c ../tts/sherpa_tts.c -o ../tts/sherpa_tts.o

clean:
	rm -f main.o ../tts/alsa_output.o ../tts/sherpa_tts.o $(TARGET)
