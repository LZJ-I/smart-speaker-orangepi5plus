.PHONY: all clean asr_kws tts player supervisor tools install_bins run stop

all: asr_kws tts player supervisor tools install_bins

asr_kws:
	$(MAKE) -C voice-assistant/main_asr_kws

tts:
	$(MAKE) -C voice-assistant/main_tts

player:
	$(MAKE) -C player

supervisor:
	$(MAKE) -C supervisor

tools:
	$(MAKE) -C tools

install_bins:
	mkdir -p build/bin
	cp -f asr_kws_process build/bin/asr_kws_process
	cp -f tts_process build/bin/tts_process
	cp -f player/run build/bin/player_run

run: all
	cd player && ./init.sh
	./build/bin/supervisor

stop:
	pkill -f asr_kws_process || true
	pkill -f tts_process || true
	pkill -f player/run || true
	pkill -f build/bin/supervisor || true

clean:
	$(MAKE) -C voice-assistant/main_asr_kws clean
	$(MAKE) -C voice-assistant/main_tts clean
	$(MAKE) -C voice-assistant/tts/example clean
	$(MAKE) -C player clean
	$(MAKE) -C supervisor clean
	$(MAKE) -C tools clean
	rm -f build/bin/asr_kws_process build/bin/tts_process build/bin/player_run

