CC = gcc
P := .
CFLAGS = -Wall -g -I/usr/include/json-c $(shell pkg-config --cflags gstreamer-1.0 2>/dev/null) \
	-I$(P)/.. \
	-I$(P)/core \
	-I$(P)/net \
	-I$(P)/select_loop \
	-I$(P)/device \
	-I$(P)/rules \
	-I$(P)/bridge \
	-I$(P)/music_source

OBJS = \
	core/main.o \
	select_loop/select.o \
	select_loop/select_text.o \
	select_loop/select_music_llm.o \
	net/link.o \
	core/shm.o \
	net/socket.o \
	net/socket_report.o \
	device/device.o \
	core/player.o \
	core/player_fifo.o \
	core/player_gst.o \
	rules/rule_match.o \
	bridge/music_lib_bridge.o \
	music_source/music_source_local.o \
	music_source/music_source_server.o \
	music_source/music_server_async.o \
	music_source/music_source_manager.o \
	../ipc/ipc_message.o \
	../voice-assistant/llm/llm.o \
	../debug_log.o
LIBS = -lpthread -ljson-c -lasound -lm $(shell pkg-config --libs gstreamer-1.0 2>/dev/null)

TARGET = run

.PHONY: all clean test_online_music_chain
all: $(TARGET)

TEST_CFLAGS = -Wall -g -I$(P)/.. -I$(P)/core -I$(P)/net -I$(P)/music_source $(shell pkg-config --cflags json-c 2>/dev/null)
test_online_music_chain: test_online_music_chain.c music_source/music_source_server.c
	$(CC) $(TEST_CFLAGS) -o $@ \
		test_online_music_chain.c music_source/music_source_server.c $(shell pkg-config --libs json-c 2>/dev/null || echo -ljson-c)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LIBS)
	rm -f $(OBJS)

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

../ipc/ipc_message.o: ../ipc/ipc_message.c
	$(CC) $(CFLAGS) -c ../ipc/ipc_message.c -o ../ipc/ipc_message.o

../voice-assistant/llm/llm.o: ../voice-assistant/llm/llm.c
	$(CC) $(CFLAGS) -c ../voice-assistant/llm/llm.c -o ../voice-assistant/llm/llm.o

../debug_log.o: ../debug_log.c
	$(CC) $(CFLAGS) -c ../debug_log.c -o ../debug_log.o

clean:
	rm -f $(TARGET) $(OBJS) ../debug_log.o testServer test_online_music_chain
