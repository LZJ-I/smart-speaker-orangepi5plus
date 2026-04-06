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
BATCH="$(mktemp)"
trap 'rm -f "$BATCH"' EXIT
add_line() {
	printf '%s\t%s\n' "$1" "$2" >>"$BATCH"
}
add_line "$ROOT/assets/tts/mode_order.wav" "已切换到顺序播放模式"
add_line "$ROOT/assets/tts/mode_single.wav" "已切换到单曲循环模式"
add_line "$ROOT/assets/tts/fallback_unmatched.wav" "我没有理解您的意思，请换个说法吧。"
add_line "$ROOT/assets/tts/ask_what_to_listen.wav" "你想听什么呢？"
add_line "$ROOT/assets/tts/insert_storage_device.wav" "请先插入存储设备"
add_line "$ROOT/assets/tts/online_music_unsupported.wav" "在线搜歌功能尚未配置，暂不支持。"
add_line "$ROOT/assets/tts/mode_online_already.wav" "当前为在线模式，无需切换"
add_line "$ROOT/assets/tts/server_disconnect_offline.wav" "服务器断开，已进入离线模式"
add_line "$ROOT/assets/tts/server_connect_failed.wav" "连接服务器失败，离线模式"
add_line "$ROOT/assets/tts/mode_online.wav" "已切换到在线模式"
add_line "$ROOT/assets/tts/mode_offline.wav" "已切换到离线模式"
add_line "$ROOT/assets/tts/mode_offline_already.wav" "当前为离线模式，无需切换"
add_line "$ROOT/assets/tts/mode_offline_switch_failed.wav" "切换离线模式失败，无法读取存储设备歌曲"
add_line "$ROOT/assets/tts/noop_reply_recall.wav" "有事再叫我哦。"
add_line "$ROOT/assets/tts/noop_reply_leave.wav" "那我先退下了。"
add_line "$ROOT/assets/tts/noop_reply_ok.wav" "好的。"
add_line "$ROOT/assets/tts/playlist_not_found.wav" "暂时找不到这首歌单，请稍后再试或换个说法"
add_line "$ROOT/assets/tts/voice_pause_1.wav" "好的，帮您暂停啦。"
add_line "$ROOT/assets/tts/voice_pause_2.wav" "已经暂停啦。"
add_line "$ROOT/assets/tts/voice_pause_3.wav" "收到。"
add_line "$ROOT/assets/tts/voice_pause_4.wav" "先暂停一下哦。"
add_line "$ROOT/assets/tts/voice_stop_1.wav" "好的，帮您停掉啦。"
add_line "$ROOT/assets/tts/voice_stop_2.wav" "已经停止播放啦。"
add_line "$ROOT/assets/tts/voice_stop_3.wav" "收到，不播啦。"
add_line "$ROOT/assets/tts/voice_stop_4.wav" "停下啦。"
cd voice-assistant/tts/example || exit 1
"$TTSTEST" -b "$BATCH"
echo "已生成 assets/tts 下模式/兜底/NOOP 等预合成 wav（见 player/README.md）"
