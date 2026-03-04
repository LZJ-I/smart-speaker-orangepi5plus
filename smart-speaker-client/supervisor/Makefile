CC = gcc
CFLAGS = -Wall -g
TARGET = ../build/bin/supervisor

.PHONY: all clean

all: $(TARGET)

$(TARGET): main.c
	mkdir -p ../build/bin
	$(CC) $(CFLAGS) main.c -o $(TARGET)

clean:
	rm -f $(TARGET)
