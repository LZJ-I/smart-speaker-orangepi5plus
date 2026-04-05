CXX = g++
INCLUDES = -Iinclude
CXXFLAGS = -Wall -std=c++11 $(INCLUDES)

MUSIC_LIB_DIR = music-lib/target/release
MUSIC_LIB_SO = $(MUSIC_LIB_DIR)/libmusic_downloader.so

EVENT_CFLAGS := $(shell pkg-config --cflags libevent 2>/dev/null)
EVENT_LIBS := $(shell pkg-config --libs libevent 2>/dev/null)
JSON_CFLAGS := $(shell pkg-config --cflags jsoncpp 2>/dev/null)
JSON_LIBS := $(shell pkg-config --libs jsoncpp 2>/dev/null)
MYSQL_CFLAGS := $(shell mysql_config --cflags 2>/dev/null)
MYSQL_LIBS := $(shell mysql_config --libs 2>/dev/null)

ifeq ($(strip $(EVENT_LIBS)),)
EVENT_LIBS = -levent
endif
ifeq ($(strip $(JSON_LIBS)),)
JSON_LIBS = -ljsoncpp
endif
ifeq ($(strip $(MYSQL_LIBS)),)
MYSQL_LIBS = -lmysqlclient
endif

CXXFLAGS += $(EVENT_CFLAGS) $(JSON_CFLAGS) $(MYSQL_CFLAGS)
LIBS = -L$(MUSIC_LIB_DIR) -lmusic_downloader $(EVENT_LIBS) $(JSON_LIBS) $(MYSQL_LIBS) -lcrypto -ldl -pthread
RPATH = -Wl,-rpath,'$$ORIGIN/$(MUSIC_LIB_DIR)'

TARGET = server_smart_speaker
SRCS = src/main.cpp src/server.cpp src/database.cpp src/player.cpp src/music_remote_list.cpp src/app_log.cpp
OBJS = $(SRCS:.cpp=.o)

.PHONY: all clean tests stop music-lib

all: $(TARGET)

music-lib:
	cd music-lib && cargo build --release

$(MUSIC_LIB_SO): music-lib

$(TARGET): $(MUSIC_LIB_SO) $(OBJS)
	$(CXX) $(CXXFLAGS) $(RPATH) -o $@ $(OBJS) $(LIBS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

tests: tests/test_client tests/test_app

tests/test_client: tests/test_client.c
	$(CC) -Wall -std=c11 -o $@ $<

tests/test_app: tests/test_app.cpp
	$(CXX) -Wall -std=c++11 $(INCLUDES) $(JSON_CFLAGS) -o $@ $< $(JSON_LIBS) -lpthread

clean:
	rm -f $(OBJS) $(TARGET) tests/test_client tests/test_app

stop:
	@port=$${SMART_SPEAKER_SERVER_PORT:-8888}; \
	fuser -k $$port/tcp 2>/dev/null || true; \
	pkill -x server_smart_speaker 2>/dev/null || true
