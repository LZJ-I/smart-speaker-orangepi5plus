# Makefile for music_downloader C 示例程序

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -I.
LDFLAGS = -L../target/release -lmusic_downloader

# 各个示例程序
TARGET_SEARCH = music_search
TARGET_DOWNLOAD = music_download
TARGET_SEARCH_AND_DOWNLOAD = music_search_and_download
TARGET_URL = music_url

# 源文件在子目录中
SOURCE_SEARCH = search/search.c
SOURCE_DOWNLOAD = download/download.c
SOURCE_SEARCH_AND_DOWNLOAD = search_and_download/search_and_download.c
SOURCE_URL = url/url.c

HEADER = music.h

RUST_LIB_DEBUG = ../target/debug
RUST_LIB_RELEASE = ../target/release

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    LIB_NAME = libmusic_downloader.so
    RPATH_FLAG = -Wl,-rpath,../target/release
    LIB_PATH_VAR = LD_LIBRARY_PATH
else ifeq ($(UNAME_S),Darwin)
    LIB_NAME = libmusic_downloader.dylib
    RPATH_FLAG = -Wl,-rpath,../target/release
    LIB_PATH_VAR = DYLD_LIBRARY_PATH
else
    LIB_NAME = libmusic_downloader.dll
    RPATH_FLAG = 
    LIB_PATH_VAR = PATH
endif

# 默认目标：编译所有测试程序
all: check-rust-lib $(TARGET_SEARCH) $(TARGET_DOWNLOAD) $(TARGET_SEARCH_AND_DOWNLOAD) $(TARGET_URL)

# 编译各个测试程序
$(TARGET_SEARCH): $(SOURCE_SEARCH) $(HEADER)
	@echo "编译搜索测试程序..."
	$(CC) $(CFLAGS) $(SOURCE_SEARCH) -o $(TARGET_SEARCH) $(LDFLAGS) $(RPATH_FLAG)
	@echo "编译完成！"

$(TARGET_DOWNLOAD): $(SOURCE_DOWNLOAD) $(HEADER)
	@echo "编译下载测试程序..."
	$(CC) $(CFLAGS) $(SOURCE_DOWNLOAD) -o $(TARGET_DOWNLOAD) $(LDFLAGS) $(RPATH_FLAG)
	@echo "编译完成！"

$(TARGET_SEARCH_AND_DOWNLOAD): $(SOURCE_SEARCH_AND_DOWNLOAD) $(HEADER)
	@echo "编译搜索并下载测试程序..."
	$(CC) $(CFLAGS) $(SOURCE_SEARCH_AND_DOWNLOAD) -o $(TARGET_SEARCH_AND_DOWNLOAD) $(LDFLAGS) $(RPATH_FLAG)
	@echo "编译完成！"

$(TARGET_URL): $(SOURCE_URL) $(HEADER)
	@echo "编译获取直链测试程序..."
	$(CC) $(CFLAGS) $(SOURCE_URL) -o $(TARGET_URL) $(LDFLAGS) $(RPATH_FLAG)
	@echo "编译完成！"

# 检查 Rust 库是否存在
check-rust-lib:
	@if [ ! -f "$(RUST_LIB_RELEASE)/$(LIB_NAME)" ]; then \
		echo ""; \
		echo "╔════════════════════════════════════════════════════════════════╗"; \
		echo "║  错误：找不到 Rust 库文件！                                      ║"; \
		echo "╠════════════════════════════════════════════════════════════════╣"; \
		echo "║  请先运行以下任一命令构建 Rust 库：                              ║"; \
		echo "║    • 调试版本（快速编译）：cd .. && cargo build                 ║"; \
		echo "║    • 发布版本（优化性能）：cd .. && cargo build --release       ║"; \
		echo "╚════════════════════════════════════════════════════════════════╝"; \
		echo ""; \
		exit 1; \
	fi

# 运行各个测试程序
search: $(TARGET_SEARCH) check-rust-lib
	@echo "运行搜索测试程序..."
	@$(LIB_PATH_VAR)=../target/release ./$(TARGET_SEARCH)

download: $(TARGET_DOWNLOAD) check-rust-lib
	@echo "运行下载测试程序..."
	@$(LIB_PATH_VAR)=../target/release ./$(TARGET_DOWNLOAD)

search-and-download: $(TARGET_SEARCH_AND_DOWNLOAD) check-rust-lib
	@echo "运行搜索并下载测试程序..."
	@$(LIB_PATH_VAR)=../target/release ./$(TARGET_SEARCH_AND_DOWNLOAD)

url: $(TARGET_URL) check-rust-lib
	@echo "运行获取直链测试程序..."
	@$(LIB_PATH_VAR)=../target/release ./$(TARGET_URL)

# 清理编译文件
clean:
	@echo "清理编译文件..."
	@rm -f $(TARGET_SEARCH) $(TARGET_DOWNLOAD) $(TARGET_SEARCH_AND_DOWNLOAD) $(TARGET_URL)
	@echo "清理完成！"

# 清理所有文件（包括 Rust 库）
clean-all: clean
	@echo "清理 Rust 库..."
	@cd .. && cargo clean
	@echo "全部清理完成！"

# Debug 模式
debug: CFLAGS += -g -O0
debug: LDFLAGS = -L../target/debug -lmusic_downloader
debug: RPATH_FLAG = -Wl,-rpath,../target/debug
debug: check-rust-lib-debug $(TARGET_SEARCH) $(TARGET_DOWNLOAD) $(TARGET_SEARCH_AND_DOWNLOAD) $(TARGET_URL)
	@echo "使用 Debug 版本的 Rust 库编译完成！"

check-rust-lib-debug:
	@if [ ! -f "$(RUST_LIB_DEBUG)/$(LIB_NAME)" ]; then \
		echo ""; \
		echo "╔════════════════════════════════════════════════════════════════╗"; \
		echo "║  错误：找不到 Debug 版本的 Rust 库文件！                          ║"; \
		echo "╠════════════════════════════════════════════════════════════════╣"; \
		echo "║  请先运行以下命令构建 Debug 版本的 Rust 库：                      ║"; \
		echo "║    cd .. && cargo build                                          ║"; \
		echo "╚════════════════════════════════════════════════════════════════╝"; \
		echo ""; \
		exit 1; \
	fi

# 帮助信息
help:
	@echo "可用的目标："
	@echo "  make                      - 编译所有测试程序"
	@echo "  make debug                 - 编译测试程序（Debug 模式）"
	@echo "  make search                - 编译并运行搜索测试程序"
	@echo "  make download              - 编译并运行下载测试程序"
	@echo "  make search-and-download   - 编译并运行搜索并下载测试程序"
	@echo "  make url                   - 编译并运行获取直链测试程序"
	@echo "  make clean                 - 清理编译文件"
	@echo "  make clean-all             - 清理所有（包括 Rust 库）"
	@echo "  make help                  - 显示此帮助信息"
	@echo ""
	@echo "目录结构："
	@echo "  search/                - 搜索测试程序"
	@echo "  download/              - 下载测试程序"
	@echo "  search_and_download/   - 搜索并下载测试程序"
	@echo "  url/                   - 获取直链测试程序"
	@echo ""
	@echo "参数顺序："
	@echo "  搜索:      <平台> <关键词>"
	@echo "  下载:      <平台> <歌曲ID> <音质> <输出目录>"
	@echo "  搜索并下载: <平台> <关键词> <音质> <输出目录>"
	@echo "  获取直链:  <平台> <歌曲ID> <音质>"
	@echo ""
	@echo "平台选项："
	@echo "  tx   - QQ 音乐"
	@echo "  wy   - 网易云音乐"
	@echo "  auto - 自动选择（先 tx 后 wy）"
	@echo ""
	@echo "音质支持说明："
	@echo "  酷我音乐 (kw):    128k, 320k, flac, flac24bit, hires"
	@echo "  咪咕音乐 (mg):    128k, 320k, flac, flac24bit, hires"
	@echo "  酷狗音乐 (kg):    128k, 320k, flac, flac24bit, hires, atmos, master"
	@echo "  QQ 音乐 (tx):     128k, 320k, flac, flac24bit, hires, atmos, atmos_plus, master"
	@echo "  网易云音乐 (wy):  128k, 320k, flac, flac24bit, hires, atmos, master"

.PHONY: all clean clean-all debug check-rust-lib check-rust-lib-debug help search download search-and-download url
