CC = gcc
CFLAGS = -Wall -g -I..

.PHONY: all clean

all: ../build/bin/ipc_inject ../build/bin/fifo_watch

../build/bin/ipc_inject: ipc_inject.c ../ipc/ipc_message.c
	mkdir -p ../build/bin
	$(CC) $(CFLAGS) ipc_inject.c ../ipc/ipc_message.c -o ../build/bin/ipc_inject

../build/bin/fifo_watch: fifo_watch.c
	mkdir -p ../build/bin
	$(CC) $(CFLAGS) fifo_watch.c -o ../build/bin/fifo_watch

clean:
	rm -f ../build/bin/ipc_inject ../build/bin/fifo_watch
