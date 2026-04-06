#ifndef SMART_SPEAKER_RUNTIME_CONFIG_H
#define SMART_SPEAKER_RUNTIME_CONFIG_H

#include <string>

struct ServerRuntimeConfig {
    std::string bind_ip;
    int bind_port;
    std::string music_root;
    std::string legacy_platform;
    std::string legacy_quality;
    std::string music_service_host;
    int music_service_port;
    std::string music_service_base_path;
    std::string default_leaderboard_source;
    std::string default_leaderboard_id;
};

const ServerRuntimeConfig &server_runtime_config(void);

#endif
