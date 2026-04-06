#include "server.h"
#include "app_log.h"
#include "music_runtime_init.h"
#include "music_service_client.h"
#include "runtime_config.h"

#include <iostream>

int main()
{
    app_log_init("server");
    if (music_runtime_init() != 0) {
        return 1;
    }
    const ServerRuntimeConfig &cfg = server_runtime_config();
    std::string music_service_error;
    if (!music_service_restart_local(&music_service_error)) {
        std::cerr << "music-service 未就绪：" << music_service_error << std::endl;
        return 1;
    }
    Server server;
    if (!server.initialized_ok()) {
        std::cerr << "服务端初始化失败：请根据上方 [MySQL] 提示创建库/用户或检查 mysqld。" << std::endl;
        music_service_shutdown_spawned_process();
        return 1;
    }
    server.listen(cfg.bind_ip.c_str(), cfg.bind_port);
    music_service_shutdown_spawned_process();
    return 0;
}
