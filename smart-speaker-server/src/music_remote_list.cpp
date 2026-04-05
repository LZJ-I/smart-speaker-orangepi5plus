#include "music_downloader.h"
#include "music_remote_list.h"
#include "runtime_config.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <string>

namespace {

struct SourceHint {
    const char *needle;
    const char *platform;
};

static const SourceHint kSourceHints[] = {
    {u8"\u7f51\u6613\u4e91\u97f3\u4e50\u7684", "wy"},
    {u8"\u7f51\u6291\u4e91\u97f3\u4e50\u7684", "wy"},
    {u8"QQ\u97f3\u4e50\u7684", "tx"},
    {u8"qq\u97f3\u4e50\u7684", "tx"},
    {u8"\u817e\u8baf\u97f3\u4e50\u7684", "tx"},
    {u8"\u9177\u72d7\u97f3\u4e50\u7684", "kg"},
    {u8"\u9177\u6211\u97f3\u4e50\u7684", "kw"},
    {u8"\u54aa\u5495\u97f3\u4e50\u7684", "mg"},
    {u8"\u7f51\u6613\u4e91\u7684", "wy"},
    {u8"\u7f51\u6291\u4e91\u7684", "wy"},
    {u8"\u9177\u72d7\u7684", "kg"},
    {u8"\u9177\u6211\u7684", "kw"},
    {u8"\u54aa\u5495\u7684", "mg"},
    {u8"\u817e\u8baf\u7684", "tx"},
    {u8"\u7f51\u6613\u4e91\u97f3\u4e50", "wy"},
    {u8"\u7f51\u6291\u4e91\u97f3\u4e50", "wy"},
    {u8"\u5c0f\u67b8\u97f3\u4e50", "kg"},
    {u8"\u5c0f\u8717\u97f3\u4e50", "kw"},
    {u8"\u5c0f\u871c\u97f3\u4e50", "mg"},
    {u8"\u5c0f\u79cb\u97f3\u4e50", "tx"},
    {u8"\u5c0f\u82b8\u97f3\u4e50", "wy"},
    {u8"QQ\u97f3\u4e50", "tx"},
    {u8"qq\u97f3\u4e50", "tx"},
    {u8"\u817e\u8baf\u97f3\u4e50", "tx"},
    {u8"\u9177\u72d7\u97f3\u4e50", "kg"},
    {u8"\u9177\u6211\u97f3\u4e50", "kw"},
    {u8"\u54aa\u5495\u97f3\u4e50", "mg"},
    {u8"\u7f51\u6613\u4e91", "wy"},
    {u8"\u7f51\u6291\u4e91", "wy"},
    {u8"\u9177\u72d7", "kg"},
    {u8"\u9177\u6211", "kw"},
    {u8"\u54aa\u5495", "mg"},
    {u8"\u817e\u8baf", "tx"},
    {u8"\u5c0f\u67b8", "kg"},
    {u8"\u5c0f\u8717", "kw"},
    {u8"\u5c0f\u871c", "mg"},
    {u8"\u5c0f\u79cb", "tx"},
    {u8"\u5c0f\u82b8", "wy"},
    {"QQ", "tx"},
    {"qq", "tx"},
};

static const char *kIntentPrefixes[] = {
    u8"\u7ed9\u6211\u64ad\u653e\u4e00\u9996",
    u8"\u6211\u60f3\u542c\u4e00\u9996",
    u8"\u6211\u8981\u542c\u4e00\u9996",
    u8"\u7ed9\u6211\u653e\u4e00\u9996",
    u8"\u5e2e\u6211\u653e\u4e00\u9996",
    u8"\u542c\u4e00\u9996",
    u8"\u6765\u4e00\u9996",
    u8"\u64ad\u653e\u4e00\u4e0b",
    u8"\u6211\u60f3\u542c",
    u8"\u6211\u8981\u542c",
    u8"\u7ed9\u6211\u64ad\u653e",
    u8"\u7ed9\u6211\u653e",
    u8"\u5e2e\u6211\u653e",
    u8"\u5e2e\u5fd9\u653e",
    u8"\u542c\u4e00\u4e0b",
    u8"\u6765\u9996",
    u8"\u653e\u4e00\u9996",
    u8"\u653e\u9996",
    u8"\u64ad\u4e00\u9996",
    u8"\u64ad\u4e2a",
    u8"\u542c\u4e2a",
    u8"\u64ad\u653e",
    u8"\u542c\u542c",
    u8"\u7528",
    u8"\u4ece",
    u8"\u5728",
    u8"\u5230",
};

static const char *kLeadingConnectors[] = {
    u8"\u7684",
    u8"\u7248",
    u8"\u91cc",
    u8"\u4e0a",
    u8"\u4e2d",
    u8"\u548c",
    u8"\u4e0e",
    u8"\u8ddf",
    u8"\u5427",
    u8"\u554a",
    u8"\u5457",
};

static void sort_intent_prefixes_by_len(void)
{
    static bool done = false;
    size_t n = sizeof(kIntentPrefixes) / sizeof(kIntentPrefixes[0]);
    if (done) {
        return;
    }
    std::sort(kIntentPrefixes, kIntentPrefixes + n,
              [](const char *a, const char *b) { return strlen(a) > strlen(b); });
    done = true;
}

static void strip_intent_prefixes(std::string &kw)
{
    bool progress;
    size_t i;

    sort_intent_prefixes_by_len();
    do {
        progress = false;
        for (i = 0; i < sizeof(kIntentPrefixes) / sizeof(kIntentPrefixes[0]); ++i) {
            const char *p = kIntentPrefixes[i];
            size_t len = strlen(p);
            if (kw.size() >= len && kw.compare(0, len, p, len) == 0) {
                kw.erase(0, len);
                progress = true;
                break;
            }
        }
    } while (progress);
}

static void strip_leading_connectors(std::string &kw)
{
    bool progress;
    size_t ci;

    do {
        progress = false;
        for (ci = 0; ci < sizeof(kLeadingConnectors) / sizeof(kLeadingConnectors[0]); ++ci) {
            const char *c = kLeadingConnectors[ci];
            size_t len = strlen(c);
            if (kw.size() >= len && kw.compare(0, len, c, len) == 0) {
                kw.erase(0, len);
                progress = true;
                break;
            }
        }
    } while (progress);
}

static void erase_longest_source_match(std::string &keyword, std::set<std::string> &seen)
{
    size_t best_i = std::string::npos;
    size_t best_len = 0;
    const char *best_plat = NULL;
    size_t hi;
    size_t i;
    size_t n = sizeof(kSourceHints) / sizeof(kSourceHints[0]);

    for (i = 0; i < keyword.size(); ++i) {
        for (hi = 0; hi < n; ++hi) {
            const SourceHint &h = kSourceHints[hi];
            size_t len = strlen(h.needle);
            if (len == 0 || i + len > keyword.size()) {
                continue;
            }
            if (keyword.compare(i, len, h.needle, len) != 0) {
                continue;
            }
            if (len > best_len || (len == best_len && i < best_i)) {
                best_len = len;
                best_i = i;
                best_plat = h.platform;
            }
        }
    }
    if (best_i != std::string::npos && best_plat != NULL) {
        seen.insert(std::string(best_plat));
        keyword.erase(best_i, best_len);
    }
}

static void collapse_spaces(std::string &s)
{
    std::string out;
    bool prev_space = true;
    size_t i;

    for (i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!prev_space) {
                out.push_back(' ');
                prev_space = true;
            }
        } else {
            out.push_back(static_cast<char>(c));
            prev_space = false;
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    while (!out.empty() && out.front() == ' ') {
        out.erase(out.begin());
    }
    s = out;
}

}  // namespace

bool music_remote_keyword_is_vague(const std::string &keyword)
{
    static const char *generic[] = {
        "热门", "音乐", "歌", "歌曲", "一首歌", "来首歌", "听歌", "热门歌曲", NULL,
    };
    size_t i;

    if (keyword.empty()) {
        return true;
    }
    for (i = 0; generic[i] != NULL; ++i) {
        if (keyword == generic[i]) {
            return true;
        }
    }
    return false;
}

void music_remote_apply_source_hints(std::string &keyword, std::string &platform,
                                     const std::string &fallback_platform)
{
    std::set<std::string> seen;
    size_t before;

    platform = fallback_platform;
    strip_intent_prefixes(keyword);
    collapse_spaces(keyword);
    strip_leading_connectors(keyword);
    for (;;) {
        before = keyword.size();
        erase_longest_source_match(keyword, seen);
        if (keyword.size() == before) {
            break;
        }
        collapse_spaces(keyword);
        strip_leading_connectors(keyword);
    }
    strip_intent_prefixes(keyword);
    collapse_spaces(keyword);
    strip_leading_connectors(keyword);

    if (seen.size() == 1u) {
        platform = *seen.begin();
    } else if (seen.size() > 1u) {
        platform = "all";
    }
    collapse_spaces(keyword);
}

bool music_remote_list_music_page(const std::string &keyword, int page, int page_size, Json::Value &music,
                                  int &out_total, int &out_total_pages)
{
    music_search_result_t res;
    music_result_t mr;
    const char *qual;
    size_t i;
    std::string kw = keyword;
    std::string plat;
    const ServerRuntimeConfig &rt = server_runtime_config();
    const std::string &env_pl_str = rt.legacy_platform;

    memset(&res, 0, sizeof(res));
    if (page <= 0 || page_size <= 0 || keyword.empty()) {
        return false;
    }
    if (!music_api_configured()) {
        return false;
    }

    plat = env_pl_str;
    music_remote_apply_source_hints(kw, plat, env_pl_str);
    if (kw.empty()) {
        return false;
    }

    mr = music_search_page(kw.c_str(), plat.c_str(), (uint32_t)page, (uint32_t)page_size, &res);
    if (mr != Ok) {
        return false;
    }

    out_total = (int)res.total;
    out_total_pages = (int)res.total_pages;
    qual = rt.legacy_quality.c_str();

    for (i = 0; i < res.count; ++i) {
        music_info_t *info = &res.results[i];
        Json::Value item(Json::objectValue);
        char *url = NULL;

        item["singer"] = std::string(info->artist);
        item["song"] = std::string(info->name);
        item["path"] = "";
        item["source"] = std::string(info->source);
        item["song_id"] = std::string(info->id);

        if (i == 0) {
            url = music_get_url(info->source, info->id, qual);
            if (url != NULL && url[0] != '\0') {
                item["play_url"] = std::string(url);
                music_free_string(url);
            } else {
                if (url != NULL) {
                    music_free_string(url);
                }
            }
        }
        music.append(item);
    }

    music_free_search_result(&res);
    return true;
}
