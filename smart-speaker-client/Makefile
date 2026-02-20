
.PHONY: all clean asr_kws tts voice_assistant

all: asr_kws tts

voice_assistant:
	$(MAKE) -C voice-assistant original

asr_kws:
	$(MAKE) -C voice-assistant asr_kws

tts:
	$(MAKE) -C voice-assistant tts

clean:
	$(MAKE) -C voice-assistant clean

