CC = gcc
CFLAGS = -Wall -g
TARGET = bin/rule_match_example
SDCARD_TARGET = bin/sdcard_mount_example

.PHONY: all clean
all: $(TARGET) $(SDCARD_TARGET)

$(TARGET): rule_match_example.o ../rules/rule_match.o
	@mkdir -p bin
	$(CC) $(CFLAGS) -I../rules -o $@ $^
	rm -f rule_match_example.o

$(SDCARD_TARGET): sdcard_mount_example.o
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^
	rm -f sdcard_mount_example.o

rule_match_example.o: rule_match_example.c ../rules/rule_match.h
	$(CC) $(CFLAGS) -I../rules -c rule_match_example.c -o rule_match_example.o

../rules/rule_match.o: ../rules/rule_match.c ../rules/rule_match.h
	$(CC) $(CFLAGS) -I../rules -c ../rules/rule_match.c -o ../rules/rule_match.o

clean:
	rm -f rule_match_example.o sdcard_mount_example.o ../rules/rule_match.o $(TARGET) $(SDCARD_TARGET)
