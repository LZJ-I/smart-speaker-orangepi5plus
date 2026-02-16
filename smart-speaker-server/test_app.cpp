#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>     // sockaddr_in结构体
#include <arpa/inet.h>      // 地址转换函数
#include <unistd.h>         // close函数
#include <string.h>         // memset函数
#include <errno.h>
#include <jsoncpp/json/json.h>
#include <pthread.h>
#include <iostream>
#define SERVER_IP "10.102.178.47"  // 服务器IP
#define SERVER_PORT 8888            // 服务器端口


bool server_send_data(int fd, Json::Value& root)
{
    //printf("发送数据: %s", root.toStyledString().c_str());
    // 1. 计算消息长度（JSON字符串长度）
    std::string data = root.toStyledString();
    unsigned int msg_len = data.size();
    // 将前四个字节存储int，后面为信息
    char msg_buf[sizeof(int) + msg_len] = {0};
    memcpy(msg_buf, &msg_len, sizeof(int));
    memcpy(msg_buf + sizeof(int), data.c_str(), msg_len);
    // 2. 发送消息体
    if(send(fd, msg_buf, sizeof(int) + msg_len, 0) == -1)
    {
        printf("发送消息体失败: %s", strerror(errno));
        return false;
    }

    return true;
}


// 子线程函数：用于发送数据
void* app_report_thread(void* arg) {
    int sockfd = *(int*)arg;
    while(1) {
        Json::Value root;
        root["cmd"] = "app_report";
        root["appid"] = "0001";
        root["deviceid"] = "0001";
        server_send_data(sockfd, root);
        std::cerr<<"[app]"; // 应用端上报
        sleep(1);
    }
    return NULL;
}

// 子线程函数：用于接收
void* app_readdata_thread(void* arg) {
    int sockfd = *(int*)arg;
    while(1) {
        // 1. 读取消息长度（4字节int）
        char len_buf[sizeof(int)] = {0};
        int read_len = 0;
        while(read_len < (int)sizeof(int))
        {
            int n = read(sockfd, len_buf + read_len, sizeof(int) - read_len);
            if(n <= 0)
            {
                return NULL;
            }
            read_len += n;
        }
        int msg_len = *(int*)len_buf;

        // 2. 校验消息长度
        const int MAX_MSG_LEN = 1024 * 1024; // 限制最大1MB
        if(msg_len <= 0 || msg_len > MAX_MSG_LEN)
        {
            return NULL;
        }

        // 3. 读取消息体（用std::string避免栈溢出）
        std::string msg;
        msg.resize(msg_len); // 动态分配空间
        read_len = 0;
        while(read_len < msg_len)
        {
            int n = read(sockfd, &msg[read_len], msg_len - read_len);
            if(n <= 0)
            {
                return NULL;
            }
            read_len += n;
        }
        // printf("[收到消息] 消息长度: %d 消息: %s\n", msg_len, msg.c_str());

    }
    return NULL;
}


int main() {
    printf("TCP测试APP\n");

    // 1. 创建TCP套接字
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    // 2. 初始化服务器地址结构体
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));  // 清空结构体
    server_addr.sin_family = AF_INET;              // IPv4协议族
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);  // 服务器IP（转换为网络字节序）
    server_addr.sin_port = htons(SERVER_PORT);     // 服务器端口（转换为网络字节序）

    // 3. 连接服务器
    int conn_ret = connect(
        sockfd, 
        (struct sockaddr*)&server_addr,  // 服务器地址（强制类型转换）
        sizeof(server_addr)
    );
    if (conn_ret < 0) {
        perror("connect");
        close(sockfd);  // 出错关闭套接字，避免资源泄漏
        return 1;
    }
    printf("连接服务器成功! (IP: %s, Port: %d)\n", SERVER_IP, SERVER_PORT);


    // 创建一个子线程来发送数据
    pthread_t send_thread;
    pthread_create(&send_thread, NULL, app_report_thread, (void*)&sockfd);

    // 创建一个子线程来接收数据
    pthread_t read_thread;
    pthread_create(&read_thread, NULL, app_readdata_thread, (void*)&sockfd);

    // 循环发送数据
    while(1)
    {
        getchar();
        
        Json::Value root;
        // 请求暂停
        root["cmd"] = "app_suspend_play";
        server_send_data(sockfd, root);
        printf("请求暂停播放\n");
        sleep(5);

        // root["cmd"] = "app_register";
        // root["appid"] = "0002";
        // root["password"] = "1234";
        // server_send_data(sockfd, root);
        // printf("请求注册0002 1234");
        // sleep(5);
        // root["cmd"] = "app_login";
        // root["appid"] = "0002";
        // root["password"] = "1234";
        // server_send_data(sockfd, root);
        // printf("请求登录0002 1234");
        // sleep(5);
        // root["cmd"] = "app_login";
        // root["appid"] = "0003";
        // root["password"] = "1234";
        // server_send_data(sockfd, root);
        // printf("请求登录0003 1234");

        // 构建JSON消息体

        // // 请求开始播放
        // Json::Value root;
        // root["cmd"] = "app_start_play";
        // root["appid"] = "0001";
        // root["deviceid"] = "0001";
        // server_send_data(sockfd, root);
        // printf("请求开始播放\n");
        // sleep(5);

        

        // // 请求继续播放
        // root["cmd"] = "app_continue_play";
        // server_send_data(sockfd, root);
        // printf("请求继续播放\n");
        // sleep(5);

        // // 请求音量+
        // root["cmd"] = "app_add_volume";
        // server_send_data(sockfd, root);
        // printf("请求音量+\n");
        // sleep(5);

        // // 请求音量-
        // root["cmd"] = "app_sub_volume";
        // server_send_data(sockfd, root);
        // printf("请求音量-\n");
        // sleep(5);

        // // 请求下一首
        // root["cmd"] = "app_play_next_song";
        // server_send_data(sockfd, root);
        // printf("请求下一首\n");
        // sleep(5);

        // // 请求上一首
        // root["cmd"] = "app_play_prev_song";
        // server_send_data(sockfd, root);
        // printf("请求上一首\n");
        // sleep(5);

        // // 请求顺序播放
        // root["cmd"] = "app_order_mode";
        // server_send_data(sockfd, root);
        // printf("请求顺序播放\n");
        // sleep(5);

        // // 请求单曲循环
        // root["cmd"] = "app_single_mode";
        // server_send_data(sockfd, root);
        // printf("请求单曲播放\n");
        // sleep(5);

        // // 请求结束播放
        // root["cmd"] = "app_stop_play";
        // server_send_data(sockfd, root);
        // printf("请求结束播放\n");
        // sleep(5);
    }

    // 6. 关闭套接字
    close(sockfd);
    printf("客户端已断开连接.\n");

    return 0;
}
