#include <arpa/inet.h>
#include <json/json.h>
#include <netinet/in.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8888

static const char *server_ip()
{
    const char *value = getenv("SMART_SPEAKER_SERVER_IP");
    if (value != NULL && value[0] != '\0') {
        return value;
    }
    return SERVER_IP;
}

static int server_port()
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

static bool server_send_data(int fd, const Json::Value &root)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string data = Json::writeString(wb, root);
    unsigned int msg_len = (unsigned int)data.size();
    std::string packet(sizeof(int), '\0');
    packet.append(data.data(), data.size());
    memcpy(&packet[0], &msg_len, sizeof(int));
    return send(fd, packet.data(), packet.size(), 0) != -1;
}

int main()
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    Json::Value root(Json::objectValue);
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

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    root["cmd"] = "app_report";
    root["appid"] = "0001";
    root["deviceid"] = "0001";
    server_send_data(sockfd, root);
    printf("app_report 已发送\n");

    close(sockfd);
    return 0;
}
