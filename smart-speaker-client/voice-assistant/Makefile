
.PHONY: all clean asr_kws tts original

all: asr_kws tts

asr_kws:
	$(MAKE) -C main_asr_kws

tts:
	$(MAKE) -C main_tts

original:
	$(MAKE) -f Makefile.original

clean:
	$(MAKE) -f Makefile.original clean || true
	$(MAKE) -C main_asr_kws clean
	$(MAKE) -C main_tts clean

