#!/bin/bash
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1
mkdir -p assets/tts
make -C voice-assistant/tts/example tts_test 2>/dev/null || true
TTSTEST="$ROOT/voice-assistant/tts/example/tts_test"
if [ ! -x "$TTSTEST" ]; then
	echo "请先编译 TTS 示例: make -C voice-assistant/tts/example"
	exit 1
fi
cd voice-assistant/tts/example || exit 1
./tts_test -t "已切换到顺序播放模式" -o "$ROOT/assets/tts/mode_order.wav"
./tts_test -t "已切换到单曲循环模式" -o "$ROOT/assets/tts/mode_single.wav"
echo "已生成 assets/tts/mode_order.wav 与 assets/tts/mode_single.wav"
