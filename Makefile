CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
UNAME := $(shell uname -s)
LDLIBS := -lm -lpthread
ifeq ($(UNAME),Linux)
LDLIBS += -ldl
endif
ifeq ($(UNAME),Darwin)
LDLIBS += -framework CoreAudio -framework CoreFoundation -framework AudioToolbox
endif

all: koe

koe: koe.c config.c config.h miniaudio.h voicevox_core.h
	$(CC) $(CFLAGS) -o $@ koe.c config.c $(LDLIBS)

config_test: config_test.c config.c config.h
	$(CC) $(CFLAGS) -o $@ config_test.c config.c -lm

test: config_test
	./config_test

clean:
	rm -f koe config_test

.PHONY: all test clean
