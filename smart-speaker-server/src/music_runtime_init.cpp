#include "music_runtime_init.h"
#include "music_downloader.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

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

void apply_toml_string(std::string &dst, const std::string &value)
{
    std::string v = trim_copy(value);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        dst = v.substr(1, v.size() - 2);
    } else if (!v.empty()) {
        dst = v;
    }
}

struct MusicToml {
    std::string music_api_url;
    std::string music_api_key;
    std::string music_user_agent;
    std::string lx_script_import_url;
    std::string lx_script_download_url_template;
    std::string lx_script_save_path;
};

const char *kMusicTomlRel = "data/config/music.toml";
const char *kMusicServiceTomlRel = "data/config/music-service.toml";

void write_default_music_toml(const std::string &path)
{
    std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    out << "# 洛雪「导入脚本」：完整 https 链接填 lx_script_import_url（启动下载）；或直接把 .js 放到 lx_script_save_path。\n"
        << "#\n"
        << "lx_script_import_url = \"\"\n"
        << "lx_script_save_path = \"data/music-source/lx.js\"\n";
}

void load_music_toml(const std::string &path, MusicToml &out)
{
    std::ifstream in(path.c_str());
    std::string line;
    out.music_api_url.clear();
    out.music_api_key.clear();
    out.music_user_agent.clear();
    out.lx_script_import_url.clear();
    out.lx_script_download_url_template.clear();
    out.lx_script_save_path.clear();

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
        if (key == "music_api_url") {
            apply_toml_string(out.music_api_url, value);
        } else if (key == "music_api_key") {
            apply_toml_string(out.music_api_key, value);
        } else if (key == "music_user_agent") {
            apply_toml_string(out.music_user_agent, value);
        } else if (key == "lx_script_import_url") {
            apply_toml_string(out.lx_script_import_url, value);
        } else if (key == "lx_script_download_url_template") {
            apply_toml_string(out.lx_script_download_url_template, value);
        } else if (key == "lx_script_save_path") {
            apply_toml_string(out.lx_script_save_path, value);
        }
    }
}

std::string toml_quoted(const std::string &s)
{
    std::string o = "\"";
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\\' || c == '"') {
            o += '\\';
        }
        o += c;
    }
    o += '"';
    return o;
}

void upsert_toml_key(std::vector<std::string> &lines, const std::string &key, const std::string &value)
{
    std::string prefix = key + " =";
    size_t i;
    for (i = 0; i < lines.size(); ++i) {
        std::string t = trim_copy(lines[i]);
        if (t.compare(0, prefix.size(), prefix) == 0) {
            lines[i] = key + " = " + toml_quoted(value);
            return;
        }
    }
    if (!lines.empty() && !trim_copy(lines.back()).empty()) {
        lines.push_back("");
    }
    lines.push_back(key + " = " + toml_quoted(value));
}

bool curl_download(const char *url, const char *dest_path)
{
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        execlp("curl", "curl", "-fsSL", "--connect-timeout", "15", "-m", "120", "-o", dest_path, url, (char *)NULL);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) {
        return false;
    }
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

std::string build_template_url(const std::string &tmpl, const std::string &key)
{
    size_t pos = tmpl.find("%s");
    if (pos != std::string::npos) {
        return tmpl.substr(0, pos) + key + tmpl.substr(pos + 2);
    }
    return tmpl;
}

bool read_text_file(const std::string &path, std::string &out)
{
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return !out.empty();
}

bool extract_js_string_const(const std::string &js, const char *name, std::string &val)
{
    std::string a = std::string("const ") + name + "=";
    std::string b = std::string("const ") + name + " =";
    size_t p = js.find(a);
    if (p == std::string::npos) {
        p = js.find(b);
        if (p == std::string::npos) {
            return false;
        }
        p += b.size();
    } else {
        p += a.size();
    }
    while (p < js.size() && (js[p] == ' ' || js[p] == '\t')) {
        p++;
    }
    if (p >= js.size() || js[p] != '"') {
        return false;
    }
    p++;
    size_t start = p;
    while (p < js.size()) {
        char c = js[p];
        if (c == '\\' && p + 1 < js.size()) {
            p += 2;
            continue;
        }
        if (c == '"') {
            break;
        }
        p++;
    }
    if (p >= js.size() || js[p] != '"') {
        return false;
    }
    val.assign(js, start, p - start);
    return true;
}

void read_lx_script_api_fields(const std::string &script_path, std::string &api_url, std::string &api_key)
{
    api_url.clear();
    api_key.clear();
    std::string js;
    if (!read_text_file(script_path, js)) {
        return;
    }
    (void)extract_js_string_const(js, "API_URL", api_url);
    (void)extract_js_string_const(js, "API_KEY", api_key);
}

std::string script_path_for_music_service_toml(const std::string &save_rel)
{
    if (save_rel.size() > 5 && save_rel.compare(0, 5, "data/") == 0) {
        return std::string("../") + save_rel.substr(5);
    }
    return "../music-source/lx.js";
}

std::string resolve_script_download_url(const MusicToml &m)
{
    if (!m.lx_script_import_url.empty()) {
        return m.lx_script_import_url;
    }
    if (m.lx_script_download_url_template.empty()) {
        return std::string();
    }
    if (m.lx_script_download_url_template.find("%s") != std::string::npos) {
        if (m.music_api_key.empty()) {
            return std::string();
        }
        return build_template_url(m.lx_script_download_url_template, m.music_api_key);
    }
    const std::string &t = m.lx_script_download_url_template;
    if (t.size() >= 8 && t.compare(0, 8, "https://") == 0) {
        return t;
    }
    if (t.size() >= 7 && t.compare(0, 7, "http://") == 0) {
        return t;
    }
    return std::string();
}

void ensure_default_music_service_toml(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return;
    }
    ensure_dir(path.substr(0, path.rfind('/')).c_str());
    std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    out << "# Node 音乐子服务（server 会同步 resolver_*、music_source_script）\n"
        << "# 均可省略，默认见 music-service/index.js readConfig：\n"
        << "# host=127.0.0.1 port=9300 provider=lx default_source=kw\n"
        << "# search_sources=default_source playlist_search_sources=kw,wy\n"
        << "# resolve_quality=320k music_source_script=../music-source/lx.js\n"
        << "# resolver_* 空则从 lx 脚本 API_URL/API_KEY\n"
        << "host = \"127.0.0.1\"\n"
        << "port = 9300\n"
        << "\n"
        << "provider = \"lx\"\n"
        << "default_source = \"kw\"\n"
        << "search_sources = \"kw\"\n"
        << "playlist_search_sources = \"kw,wy\"\n"
        << "resolve_quality = \"320k\"\n"
        << "music_source_script = \"../music-source/lx.js\"\n"
        << "\n"
        << "resolver_api_url = \"\"\n"
        << "resolver_api_key = \"\"\n";
}

void sync_music_service_toml(const std::string &path, const MusicToml &m)
{
    ensure_default_music_service_toml(path);
    std::ifstream in(path.c_str());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    upsert_toml_key(lines, "resolver_api_url", m.music_api_url);
    upsert_toml_key(lines, "resolver_api_key", m.music_api_key);
    {
        std::string save_rel = m.lx_script_save_path.empty() ? "data/music-source/lx.js" : m.lx_script_save_path;
        upsert_toml_key(lines, "music_source_script", script_path_for_music_service_toml(save_rel));
    }

    std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc);
    for (size_t i = 0; i < lines.size(); ++i) {
        out << lines[i] << "\n";
    }
}

}  // namespace

int music_runtime_init(void)
{
    std::string base = executable_dir();
    std::string cfg_dir = join_path(base, "data/config");
    std::string music_src_dir = join_path(base, "data/music-source");
    ensure_dir(join_path(base, "data").c_str());
    ensure_dir(cfg_dir.c_str());
    ensure_dir(music_src_dir.c_str());

    std::string music_toml = join_path(base, kMusicTomlRel);
    {
        struct stat st;
        if (stat(music_toml.c_str(), &st) != 0) {
            write_default_music_toml(music_toml);
        }
    }

    MusicToml m;
    load_music_toml(music_toml, m);
    if (m.lx_script_save_path.empty()) {
        m.lx_script_save_path = "data/music-source/lx.js";
    }

    std::string script_abs = m.lx_script_save_path[0] == '/' ? m.lx_script_save_path : join_path(base, m.lx_script_save_path);
    {
        std::string parent = script_abs.substr(0, script_abs.rfind('/'));
        if (!parent.empty()) {
            ensure_dir(parent.c_str());
        }
    }

    struct stat st_js;
    bool have_script = (stat(script_abs.c_str(), &st_js) == 0 && S_ISREG(st_js.st_mode));

    if (!have_script) {
        std::string dl = resolve_script_download_url(m);
        if (!dl.empty()) {
            std::string tmp = script_abs + ".tmp";
            if (curl_download(dl.c_str(), tmp.c_str())) {
                if (rename(tmp.c_str(), script_abs.c_str()) != 0) {
                    std::cerr << "music: 无法覆盖 " << script_abs << ": " << strerror(errno) << std::endl;
                    (void)unlink(tmp.c_str());
                }
            } else {
                std::cerr << "music: 从导入链接下载失败（需安装 curl 且网络可达）。" << std::endl;
                (void)unlink(tmp.c_str());
            }
        }
    }

    have_script = (stat(script_abs.c_str(), &st_js) == 0 && S_ISREG(st_js.st_mode));
    if (!have_script) {
        char abs_cfg[PATH_MAX];
        const char *cfg_show = music_toml.c_str();
        if (realpath(music_toml.c_str(), abs_cfg) != NULL) {
            cfg_show = abs_cfg;
        }
        std::cout
            << "\n"
            << "┌──────────────────────────────────────────────────────────────\n"
            << "│ 【未配置在线音乐源】与洛雪一致：在 music.toml 填写 lx_script_import_url = 导入脚本的完整 https 链接，\n"
            << "│ 或将音源 .js 保存到：\n"
            << "│   " << script_abs << "\n"
            << "│ 配置文件：\n"
            << "│   " << cfg_show << "\n"
            << "└──────────────────────────────────────────────────────────────\n"
            << std::endl;
        return 1;
    }

    {
        std::string ju, jk;
        read_lx_script_api_fields(script_abs, ju, jk);
        if (m.music_api_url.empty()) {
            m.music_api_url = ju;
        }
        if (m.music_api_key.empty()) {
            m.music_api_key = jk;
        }
    }
    if (m.music_api_url.empty() || m.music_api_key.empty()) {
        std::cerr << "music: 当前音源脚本里读不到 API_URL/API_KEY，请换洛雪类脚本或检查文件是否完整。" << std::endl;
        return 1;
    }

    music_configure_online(m.music_api_url.c_str(), m.music_api_key.c_str(), m.music_user_agent.c_str());

    std::string ms_toml = join_path(base, kMusicServiceTomlRel);
    sync_music_service_toml(ms_toml, m);

    {
        char abs_cfg[PATH_MAX];
        const char *cfg_show = music_toml.c_str();
        if (realpath(music_toml.c_str(), abs_cfg) != NULL) {
            cfg_show = abs_cfg;
        }
        std::cout << "[music] 在线音乐源就绪。脚本：" << script_abs << " 配置：" << cfg_show << std::endl;
    }

    return 0;
}
