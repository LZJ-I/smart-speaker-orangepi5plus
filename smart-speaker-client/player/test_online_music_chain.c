#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "music_source/music_source_manager.h"

static int try_gst_uri(const char *uri)
{
    char cmd[2048];
    int n = snprintf(cmd, sizeof(cmd),
                     "timeout 4 gst-launch-1.0 playbin uri=\"%s\" audio-sink=fakesink "
                     ">/dev/null 2>&1",
                     uri);
    if (n <= 0 || (size_t)n >= sizeof(cmd))
        return -1;
    return system(cmd);
}

int main(int argc, char **argv)
{
    const MusicSourceBackend *srv = music_source_server_backend();
    MusicSourceResult r;
    /* 默认 "." 可匹配多数带扩展名的文件名；中文歌可用: ./test_online_music_chain 雪 */
    const char *kw = (argc > 1) ? argv[1] : ".";
    int do_play = (argc > 2 && strcmp(argv[2], "--play") == 0);

    memset(&r, 0, sizeof(r));
    if (srv->search(kw, 1, 5, &r) != 0) {
        fprintf(stderr, "list_music TCP 失败（检查 SMART_SPEAKER_SERVER_IP/PORT 与服务端）\n");
        return 1;
    }
    if (r.count <= 0) {
        fprintf(stderr, "无结果，换关键词: %s ./test_online_music_chain <关键词>\n", argv[0]);
        srv->free_result(&r);
        return 1;
    }

    char url[1024];
    if (srv->get_url("server", r.items[0].song_id, url, sizeof(url)) != 0) {
        fprintf(stderr, "get_url 失败\n");
        srv->free_result(&r);
        return 1;
    }

    printf("path=%s\nurl=%s\n", r.items[0].song_id, url);
    srv->free_result(&r);

    if (do_play) {
        if (try_gst_uri(url) != 0)
            fprintf(stderr, "gst-launch 试播失败（可忽略，仅作链路探测）\n");
    }
    return 0;
}
