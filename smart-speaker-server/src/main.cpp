#include "server.h"

#include <cstdlib>
#include <iostream>

namespace {

const char *server_bind_ip(void)
{
    const char *value = getenv("SMART_SPEAKER_SERVER_IP");
    if (value != NULL && value[0] != '\0') {
        return value;
    }
    return "0.0.0.0";
}

int server_bind_port(void)
{
    const char *value = getenv("SMART_SPEAKER_SERVER_PORT");
    char *end = NULL;
    long port;
    if (value == NULL || value[0] == '\0') {
        return PORT;
    }
    port = strtol(value, &end, 10);
    if (end == value || *end != '\0' || port <= 0 || port > 65535) {
        return PORT;
    }
    return (int)port;
}

}  // namespace

int main()
{
    Server server;
    if (!server.initialized_ok()) {
        std::cerr << "服务端初始化失败：请根据上方 [MySQL] 提示创建库/用户或检查 mysqld。" << std::endl;
        return 1;
    }
    server.listen(server_bind_ip(), server_bind_port());
    return 0;
}
