#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>     // sockaddr_in结构体
#include <arpa/inet.h>      // 地址转换函数
#include <unistd.h>         // close函数
#include <string.h>         // memset函数
#include <errno.h>

#define SERVER_IP "10.13.157.199"  // 服务器IP
#define SERVER_PORT 8888            // 服务器端口

int main() {
    printf("TCP测试客户端\n");

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


    // 循环发送数据
    while(1)
    {
        char text[1024] = {0};
        scanf("%s", text);
        // 判断长度
        int len = strlen(text);
        char buf[1024+4] = {0};
        // 封装成一个包
        memcpy(buf, &len, sizeof(len));
        memcpy(buf + sizeof(len), text, len);
        // 发送数据
        if(-1 == send(sockfd, buf, len + sizeof(len), 0))
        {
            fprintf(stderr, "[ERROR] 发送失败: %s\n", strerror(errno));
            return -1;
        }
        // 接收数据
        // 1. 接收int类型的消息长度
        // 2. 接收消息体
        unsigned int size = 0;
        char recv_buf[1024+4] = {0};
        while (size < sizeof(int))
        {
            int ret = recv(sockfd, recv_buf + size, sizeof(int) - size, 0);
            if(ret <= 0)
            {
                fprintf(stderr, "[ERROR] 接收消息长度失败: %s\n", strerror(errno));
                return -1;
            }
            size += ret;
        }
        // 3. 接收消息体
        size = 0;
        // 4. 接收消息长度
        unsigned int msg_len = *((unsigned int*)recv_buf);
        memset(recv_buf, 0, sizeof(recv_buf));
        while (size < msg_len)
        {
            int ret = recv(sockfd, recv_buf + size, msg_len - size, 0);
            if(ret <= 0)
            {
                fprintf(stderr, "[ERROR] 接收消息体失败: %s\n", strerror(errno));
                return -1;
            }
            size += ret;
        }
        // 解析数据
        char reply_text[4096] = {0};
        memcpy(reply_text, recv_buf, msg_len);  // 用msg_len（实际回复长度）复制
        reply_text[msg_len] = '\0';             // 手动添加字符串结束符，避免乱码

        printf("服务器回复: \n%s\n", reply_text);  // 换行显示，更清晰
        sleep(2);
    }

    // 6. 关闭套接字
    close(sockfd);
    printf("客户端已断开连接.\n");

    return 0;
}
