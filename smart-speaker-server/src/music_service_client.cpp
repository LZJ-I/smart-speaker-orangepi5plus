#include "music_service_client.h"

#include "runtime_config.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

pid_t g_music_service_pid = -1;

bool write_all(int fd, const std::string &data)
{
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

std::string join_base_and_path(const std::string &base, const std::string &path)
{
    if (base.empty()) {
        return path;
    }
    if (path.empty()) {
        return base;
    }
    if (base.back() == '/' && path.front() == '/') {
        return base.substr(0, base.size() - 1) + path;
    }
    if (base.back() != '/' && path.front() != '/') {
        return base + "/" + path;
    }
    return base + path;
}

bool parse_http_status(const std::string &header, int *status_code)
{
    if (status_code == NULL) {
        return false;
    }
    size_t first_space = header.find(' ');
    if (first_space == std::string::npos) {
        return false;
    }
    size_t second_space = header.find(' ', first_space + 1);
    std::string code_text = header.substr(first_space + 1, second_space - first_space - 1);
    *status_code = std::atoi(code_text.c_str());
    return *status_code > 0;
}

std::string executable_dir(void)
{
    char exe_path[4096];
    ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (n <= 0) {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            return std::string(cwd);
        }
        return ".";
    }
    exe_path[n] = '\0';
    std::string path(exe_path);
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) {
        return ".";
    }
    return path.substr(0, slash);
}

std::string join_path(const std::string &base, const std::string &name)
{
    if (base.empty()) {
        return name;
    }
    if (!base.empty() && base[base.size() - 1] == '/') {
        return base + name;
    }
    return base + "/" + name;
}

void ensure_dir_if_missing(const std::string &path)
{
    if (!path.empty()) {
        (void)mkdir(path.c_str(), 0755);
    }
}

bool music_service_host_is_local(const std::string &host)
{
    return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

bool run_wait_command(const char *file, const char *arg0, const char *arg1, const char *arg2, const char *arg3)
{
    pid_t pid = fork();
    int status = 0;
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        execlp(file, arg0, arg1, arg2, arg3, (char *)NULL);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        return false;
    }
    return WIFEXITED(status);
}

void reap_spawned_music_service(void)
{
    int status = 0;
    if (g_music_service_pid <= 0) {
        return;
    }
    if (waitpid(g_music_service_pid, &status, WNOHANG) == g_music_service_pid) {
        g_music_service_pid = -1;
    }
}

int connect_music_service_socket(std::string *error_message)
{
    const ServerRuntimeConfig &cfg = server_runtime_config();
    char port_buf[16] = {0};
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    int gai;
    int fd = -1;

    snprintf(port_buf, sizeof(port_buf), "%d", cfg.music_service_port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    gai = getaddrinfo(cfg.music_service_host.c_str(), port_buf, &hints, &result);
    if (gai != 0) {
        if (error_message != NULL) {
            *error_message = gai_strerror(gai);
        }
        return -1;
    }

    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);

    if (fd < 0 && error_message != NULL) {
        *error_message = strerror(errno);
    }
    return fd;
}

bool spawn_local_music_service(std::string *error_message)
{
    std::string base_dir = executable_dir();
    std::string script_path = join_path(join_path(base_dir, "music-service"), "index.js");
    std::string data_dir = join_path(base_dir, "data");
    std::string logs_dir = join_path(data_dir, "logs");
    std::string log_path = join_path(logs_dir, "music-service.log");

    reap_spawned_music_service();
    if (access(script_path.c_str(), R_OK) != 0) {
        if (error_message != NULL) {
            *error_message = "music-service/index.js 不存在";
        }
        return false;
    }

    ensure_dir_if_missing(data_dir);
    ensure_dir_if_missing(logs_dir);

    pid_t pid = fork();
    if (pid < 0) {
        if (error_message != NULL) {
            *error_message = strerror(errno);
        }
        return false;
    }
    if (pid == 0) {
        int null_fd = open("/dev/null", O_RDONLY);
        int log_fd = open(log_path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            if (log_fd > STDERR_FILENO) {
                close(log_fd);
            }
        }
        execlp("node", "node", script_path.c_str(), (char *)NULL);
        _exit(127);
    }

    g_music_service_pid = pid;
    return true;
}

bool stop_local_music_service(std::string *error_message)
{
    const ServerRuntimeConfig &cfg = server_runtime_config();
    std::string script_path = join_path(join_path(executable_dir(), "music-service"), "index.js");
    std::string script_suffix = "music-service/index.js";
    char port_spec[32];
    int fd;
    int i;

    if (!music_service_host_is_local(cfg.music_service_host)) {
        if (error_message != NULL) {
            *error_message = "music-service host 不是本机";
        }
        return false;
    }

    music_service_shutdown_spawned_process();
    snprintf(port_spec, sizeof(port_spec), "%d/tcp", cfg.music_service_port);
    (void)run_wait_command("fuser", "fuser", "-k", port_spec, NULL);
    (void)run_wait_command("pkill", "pkill", "-f", script_path.c_str(), NULL);
    (void)run_wait_command("pkill", "pkill", "-f", script_suffix.c_str(), NULL);

    for (i = 0; i < 30; ++i) {
        fd = connect_music_service_socket(NULL);
        if (fd < 0) {
            return true;
        }
        close(fd);
        usleep(100 * 1000);
    }
    if (error_message != NULL) {
        *error_message = "music-service 停止超时";
    }
    return false;
}

}  // namespace

bool music_service_ensure_ready(std::string *error_message)
{
    const ServerRuntimeConfig &cfg = server_runtime_config();
    int fd;
    int i;
    std::string last_error;

    fd = connect_music_service_socket(NULL);
    if (fd >= 0) {
        close(fd);
        return true;
    }
    if (!music_service_host_is_local(cfg.music_service_host)) {
        if (error_message != NULL) {
            *error_message = "music-service 不可达";
        }
        return false;
    }
    if (!spawn_local_music_service(&last_error)) {
        if (error_message != NULL) {
            *error_message = last_error;
        }
        return false;
    }
    for (i = 0; i < 50; ++i) {
        fd = connect_music_service_socket(NULL);
        if (fd >= 0) {
            close(fd);
            return true;
        }
        reap_spawned_music_service();
        usleep(100 * 1000);
    }
    if (error_message != NULL) {
        *error_message = "music-service 启动超时";
    }
    return false;
}

bool music_service_restart_local(std::string *error_message)
{
    const ServerRuntimeConfig &cfg = server_runtime_config();
    std::string last_error;

    if (!music_service_host_is_local(cfg.music_service_host)) {
        return music_service_ensure_ready(error_message);
    }
    if (!stop_local_music_service(&last_error)) {
        if (error_message != NULL) {
            *error_message = last_error;
        }
        return false;
    }
    if (!spawn_local_music_service(&last_error)) {
        if (error_message != NULL) {
            *error_message = last_error;
        }
        return false;
    }
    if (!music_service_ensure_ready(&last_error)) {
        if (error_message != NULL) {
            *error_message = last_error;
        }
        return false;
    }
    return true;
}

void music_service_shutdown_spawned_process(void)
{
    int status = 0;
    reap_spawned_music_service();
    if (g_music_service_pid <= 0) {
        return;
    }
    kill(g_music_service_pid, SIGTERM);
    waitpid(g_music_service_pid, &status, 0);
    g_music_service_pid = -1;
}

bool music_service_post_json(const std::string &path, const Json::Value &request, Json::Value *response,
                             std::string *error_message)
{
    const ServerRuntimeConfig &cfg = server_runtime_config();
    Json::StreamWriterBuilder writer_builder;
    writer_builder["indentation"] = "";
    std::string payload = Json::writeString(writer_builder, request);
    std::string request_path = join_base_and_path(cfg.music_service_base_path, path);
    if (request_path.empty()) {
        request_path = "/";
    }

    char port_buf[16] = {0};
    snprintf(port_buf, sizeof(port_buf), "%d", cfg.music_service_port);
    int fd = connect_music_service_socket(error_message);
    if (fd < 0 && music_service_host_is_local(cfg.music_service_host)) {
        std::string ensure_error;
        if (!music_service_ensure_ready(&ensure_error)) {
            if (error_message != NULL && ensure_error.size() > 0) {
                *error_message = ensure_error;
            }
            return false;
        }
        fd = connect_music_service_socket(error_message);
    }

    if (fd < 0) {
        return false;
    }

    std::string http_request =
        "POST " + request_path + " HTTP/1.1\r\n" +
        "Host: " + cfg.music_service_host + ":" + port_buf + "\r\n" +
        "Content-Type: application/json\r\n" +
        "Connection: close\r\n" +
        "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n" +
        payload;

    if (!write_all(fd, http_request)) {
        if (error_message != NULL) {
            *error_message = "send failed";
        }
        close(fd);
        return false;
    }

    std::string raw_response;
    char buf[4096];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (error_message != NULL) {
                *error_message = strerror(errno);
            }
            close(fd);
            return false;
        }
        raw_response.append(buf, static_cast<size_t>(n));
    }
    close(fd);

    size_t header_end = raw_response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        if (error_message != NULL) {
            *error_message = "bad http response";
        }
        return false;
    }

    std::string header = raw_response.substr(0, header_end);
    std::string body = raw_response.substr(header_end + 4);
    int status_code = 0;
    if (!parse_http_status(header, &status_code) || status_code < 200 || status_code >= 300) {
        if (error_message != NULL) {
            *error_message = "http status " + std::to_string(status_code);
        }
        return false;
    }

    if (response == NULL) {
        return true;
    }

    Json::CharReaderBuilder reader_builder;
    std::string json_error;
    std::unique_ptr<Json::CharReader> reader(reader_builder.newCharReader());
    if (!reader->parse(body.data(), body.data() + body.size(), response, &json_error)) {
        if (error_message != NULL) {
            *error_message = json_error;
        }
        return false;
    }
    return true;
}
