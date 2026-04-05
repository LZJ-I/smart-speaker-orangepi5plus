#!/bin/bash
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1
export LD_LIBRARY_PATH="$ROOT/3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
mkdir -p assets/tts
make -C voice-assistant/tts/example tts_test 2>/dev/null || true
TTSTEST="$ROOT/voice-assistant/tts/example/tts_test"
if [ ! -x "$TTSTEST" ]; then
	echo "请先编译 TTS 示例: make -C voice-assistant/tts/example"
	exit 1
fi
cd voice-assistant/tts/example || exit 1
./tts_test -t "已切换到顺序播放模式" -o "$ROOT/assets/tts/mode_order.wav"
./tts_test -t "已切换到单娶循环模式" -o "$ROOT/assets/tts/mode_single.wav"
./tts_test -t "我没有理解您的意思，请换个说法吧。" -o "$ROOT/assets/tts/fallback_unmatched.wav"
./tts_test -t "你想听什么呢？" -o "$ROOT/assets/tts/ask_what_to_listen.wav"
./tts_test -t "请先插入存储设备" -o "$ROOT/assets/tts/insert_storage_device.wav"
./tts_test -t "在线搜歌功能尚未配置，暂不支持。" -o "$ROOT/assets/tts/online_music_unsupported.wav"
./tts_test -t "当前为在线模式，无需切换" -o "$ROOT/assets/tts/mode_online_already.wav"
./tts_test -t "服务器断开，已进入离线模式" -o "$ROOT/assets/tts/server_disconnect_offline.wav"
./tts_test -t "连接服务器失败，离线模式" -o "$ROOT/assets/tts/server_connect_failed.wav"
./tts_test -t "已切换到在线模式" -o "$ROOT/assets/tts/mode_online.wav"
./tts_test -t "已切换到离线模式" -o "$ROOT/assets/tts/mode_offline.wav"
./tts_test -t "当前为离线模式，无需切换" -o "$ROOT/assets/tts/mode_offline_already.wav"
./tts_test -t "切换离线模式失败，无法读取存储设备歌曲" -o "$ROOT/assets/tts/mode_offline_switch_failed.wav"
./tts_test -t "有事再叫我哦。" -o "$ROOT/assets/tts/noop_reply_recall.wav"
./tts_test -t "那我先退下了。" -o "$ROOT/assets/tts/noop_reply_leave.wav"
./tts_test -t "好的。" -o "$ROOT/assets/tts/noop_reply_ok.wav"
./tts_test -t "暂时找不到这首歌单，请稍后再试或换个说法" -o "$ROOT/assets/tts/playlist_not_found.wav"
echo "已生成 assets/tts 下模式/兜底/NOOP 等预合成 wav（见 player/README.md）"
