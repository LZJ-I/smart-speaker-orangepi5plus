#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_IP "10.102.178.47"
#define SERVER_PORT 8888

static const char *server_ip(void)
{
    const char *value = getenv("SMART_SPEAKER_SERVER_IP");
    if (value != NULL && value[0] != '\0') {
        return value;
    }
    return SERVER_IP;
}

static int server_port(void)
{
    const char *value = getenv("SMART_SPEAKER_SERVER_PORT");
    char *end = NULL;
    long port;
    if (value == NULL || value[0] == '\0') {
        return SERVER_PORT;
    }
    port = strtol(value, &end, 10);
    if (end == value || *end != '\0' || port <= 0 || port > 65535) {
        return SERVER_PORT;
    }
    return (int)port;
}

int main(void)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    const char *ip = server_ip();
    int port = server_port();

    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    printf("连接服务器成功: %s:%d\n", ip, port);
    close(sockfd);
    return 0;
}
