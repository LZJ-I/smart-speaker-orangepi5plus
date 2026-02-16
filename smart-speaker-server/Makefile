CC = g++
CFLAGS = -Wall -std=c++11
TARGET = server_smart_speaker
# 源文件
SRCS = main.c server.cpp database.cpp player.cpp
# 目标文件
OBJS = $(patsubst %.c,%.o,$(filter %.c,$(SRCS))) \
       $(patsubst %.cpp,%.o,$(filter %.cpp,$(SRCS)))
# 链接
LIB = -levent -lmysqlclient -ljsoncpp -lcrypto

# 最终目标
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LIB)
# 链接后删除中间目标文件（可选，保留也可以）
	rm -rf $(OBJS)  
# 把测试客户端也编译
#	$(CC) $(CFLAGS) -o test_client test_client.c
# 把测试APP也编译
#	$(CC) $(CFLAGS) -o test_app test_app.cpp -ljsoncpp -lpthread

# .c 文件→.o 文件的编译规则
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# .cpp 文件→.o 文件的编译规则
%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

# 清理规则
clean:
	rm -f $(OBJS) $(TARGET)
