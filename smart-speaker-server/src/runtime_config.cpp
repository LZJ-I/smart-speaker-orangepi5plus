#include "runtime_config.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

namespace {

const char *kConfigDir = "data";
const char *kConfigSubDir = "data/config";
const char *kServerConfigPath = "data/config/server.toml";

std::string trim_copy(const std::string &input)
{
    size_t start = 0;
    size_t end = input.size();
    while (start < input.size() &&
           (input[start] == ' ' || input[start] == '\t' || input[start] == '\r' || input[start] == '\n')) {
        start++;
    }
    while (end > start &&
           (input[end - 1] == ' ' || input[end - 1] == '\t' || input[end - 1] == '\r' || input[end - 1] == '\n')) {
        end--;
    }
    return input.substr(start, end - start);
}

bool ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    if (mkdir(path, 0755) == 0) {
        return true;
    }
    return errno == EEXIST;
}

void write_default_config_if_missing(void)
{
    struct stat st;
    if (stat(kServerConfigPath, &st) == 0) {
        return;
    }
    ensure_dir(kConfigDir);
    ensure_dir(kConfigSubDir);

    std::ofstream out(kServerConfigPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    out
        << "# 服务端监听配置\n"
        << "bind_ip = \"0.0.0.0\"\n"
        << "bind_port = 8888\n"
        << "\n"
        << "# 本地曲库扫描根（相对 server 工作目录或绝对路径）\n"
        << "music_root = \"data/music-library/\"\n"
        << "\n"
        << "# 兼容 Rust music-lib：未带 source 时默认平台；与 lx-music-desktop 一致：auto 单次 HTTP 15s、总≤75s；all 每源 limit=30 并发、15s、合并规则同 lx store/search/music\n"
        << "legacy_platform = \"auto\"\n"
        << "legacy_quality = \"320k\"\n"
        << "\n"
        << "# 新 Node 音乐子服务地址\n"
        << "music_service_host = \"127.0.0.1\"\n"
        << "music_service_port = 9300\n"
        << "music_service_base_path = \"\"\n";
}

void apply_string(std::string &dst, const std::string &value)
{
    std::string v = trim_copy(value);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        dst = v.substr(1, v.size() - 2);
    } else if (!v.empty()) {
        dst = v;
    }
}

void load_config(ServerRuntimeConfig &cfg)
{
    std::ifstream in(kServerConfigPath);
    std::string line;

    if (!in.is_open()) {
        return;
    }
    while (std::getline(in, line)) {
        std::string raw = trim_copy(line);
        if (raw.empty() || raw[0] == '#') {
            continue;
        }
        size_t eq = raw.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = trim_copy(raw.substr(0, eq));
        std::string value = trim_copy(raw.substr(eq + 1));
        if (key == "bind_ip") {
            apply_string(cfg.bind_ip, value);
        } else if (key == "bind_port") {
            cfg.bind_port = std::atoi(value.c_str());
        } else if (key == "music_root") {
            apply_string(cfg.music_root, value);
        } else if (key == "legacy_platform") {
            apply_string(cfg.legacy_platform, value);
        } else if (key == "legacy_quality") {
            apply_string(cfg.legacy_quality, value);
        } else if (key == "music_service_host") {
            apply_string(cfg.music_service_host, value);
        } else if (key == "music_service_port") {
            cfg.music_service_port = std::atoi(value.c_str());
        } else if (key == "music_service_base_path") {
            apply_string(cfg.music_service_base_path, value);
        }
    }
}

ServerRuntimeConfig load_server_runtime_config(void)
{
    ServerRuntimeConfig cfg;
    cfg.bind_ip = "0.0.0.0";
    cfg.bind_port = 8888;
    cfg.music_root = "data/music-library/";
    cfg.legacy_platform = "auto";
    cfg.legacy_quality = "320k";
    cfg.music_service_host = "127.0.0.1";
    cfg.music_service_port = 9300;
    cfg.music_service_base_path = "";

    write_default_config_if_missing();
    load_config(cfg);

    if (cfg.bind_port <= 0 || cfg.bind_port > 65535) {
        cfg.bind_port = 8888;
    }
    if (cfg.music_service_port <= 0 || cfg.music_service_port > 65535) {
        cfg.music_service_port = 9300;
    }
    if (cfg.bind_ip.empty()) {
        cfg.bind_ip = "0.0.0.0";
    }
    if (cfg.music_root.empty()) {
        cfg.music_root = "data/music-library/";
    }
    if (!cfg.music_root.empty() && cfg.music_root.back() != '/') {
        cfg.music_root.push_back('/');
    }
    if (cfg.legacy_platform.empty()) {
        cfg.legacy_platform = "auto";
    }
    if (cfg.legacy_quality.empty()) {
        cfg.legacy_quality = "320k";
    }
    if (cfg.music_service_host.empty()) {
        cfg.music_service_host = "127.0.0.1";
    }
    return cfg;
}

}  // namespace

const ServerRuntimeConfig &server_runtime_config(void)
{
    static ServerRuntimeConfig cfg = load_server_runtime_config();
    return cfg;
}
