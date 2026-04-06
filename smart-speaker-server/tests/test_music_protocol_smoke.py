#!/usr/bin/env python3
import json
import socket
import struct
import sys


def send_packet(host, port, payload):
    raw = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    with socket.create_connection((host, port), timeout=3) as sock:
        sock.sendall(struct.pack("i", len(raw)))
        sock.sendall(raw)

        length_buf = recv_exact(sock, 4)
        length = struct.unpack("i", length_buf)[0]
        body = recv_exact(sock, length)
        return json.loads(body.decode("utf-8"))


def recv_exact(sock, size):
    chunks = []
    remaining = size
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise RuntimeError("连接被提前关闭")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def assert_true(value, message):
    if not value:
        raise RuntimeError(message)


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8888

    search_reply = send_packet(
        host,
        port,
        {
            "cmd": "music.search.song",
            "keyword": "周杰伦 晴天",
            "page": 1,
            "page_size": 2,
        },
    )
    assert_true(search_reply.get("cmd") == "music.search.song.reply", "search reply cmd 错误")
    assert_true(search_reply.get("result") == "ok", "search result 非 ok")
    items = search_reply.get("items") or []
    assert_true(isinstance(items, list) and items, "search items 为空")

    resolve_reply = send_packet(
        host,
        port,
        {
            "cmd": "music.url.resolve",
            "id": items[0]["id"],
            "source": items[0].get("source", ""),
        },
    )
    assert_true(resolve_reply.get("cmd") == "music.url.resolve.reply", "resolve reply cmd 错误")
    assert_true(resolve_reply.get("result") == "ok", "resolve result 非 ok")
    assert_true(bool(resolve_reply.get("play_url")), "play_url 为空")

    leaderboard_list_reply = send_packet(
        host,
        port,
        {
            "cmd": "music.leaderboard.list",
            "source": "wy",
        },
    )
    assert_true(leaderboard_list_reply.get("cmd") == "music.leaderboard.list.reply", "leaderboard list reply cmd 错误")
    assert_true(leaderboard_list_reply.get("result") in ("ok", "empty"), "leaderboard list result 非法")

    leaderboard_detail_reply = send_packet(
        host,
        port,
        {
            "cmd": "music.leaderboard.detail",
            "source": "wy",
            "page": 1,
            "page_size": 3,
        },
    )
    assert_true(leaderboard_detail_reply.get("cmd") == "music.leaderboard.detail.reply", "leaderboard detail reply cmd 错误")
    assert_true(leaderboard_detail_reply.get("result") in ("ok", "empty"), "leaderboard detail result 非法")
    detail_items = leaderboard_detail_reply.get("items") or []
    assert_true(isinstance(detail_items, list), "leaderboard detail items 非数组")

    artist_search_reply = send_packet(
        host,
        port,
        {
            "cmd": "music.search.artist",
            "keyword": "周杰伦",
            "source": "wy",
            "page": 1,
            "page_size": 2,
        },
    )
    assert_true(artist_search_reply.get("cmd") == "music.search.artist.reply", "artist search reply cmd 错误")
    assert_true(artist_search_reply.get("result") in ("ok", "empty"), "artist search result 非法")
    artist_items = artist_search_reply.get("items") or []
    assert_true(isinstance(artist_items, list), "artist search items 非数组")
    if artist_items:
        artist_hot_reply = send_packet(
            host,
            port,
            {
                "cmd": "music.artist.hot",
                "id": artist_items[0]["id"],
                "source": artist_items[0].get("source", "wy"),
                "page": 1,
                "page_size": 3,
            },
        )
        assert_true(artist_hot_reply.get("cmd") == "music.artist.hot.reply", "artist hot reply cmd 错误")
        assert_true(artist_hot_reply.get("result") in ("ok", "empty"), "artist hot result 非法")
    print("music protocol smoke ok")


if __name__ == "__main__":
    main()
