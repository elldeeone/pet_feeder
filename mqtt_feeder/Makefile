# Cross-compile for Anyka AK3918 (ARM926EJ-S, ARMv5TE)
CC       = arm-linux-musleabi-gcc
STRIP    = arm-linux-musleabi-strip
CFLAGS  = -static -Os -march=armv5te -Wall -Wextra \
          -DMQTT_PAL_NO_MUTEX -I. -Imqtt-c
LDFLAGS = -static -no-pie
HOST_CC ?= cc

SRCS    = main.c config.c feeder.c ha_discovery.c mqtt-c/mqtt.c mqtt-c/mqtt_pal.c
OBJS    = $(SRCS:.c=.o)
TARGET  = mqtt_feeder

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	$(STRIP) $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

test:
	@set -eu; test_bin=$$(mktemp /tmp/mqtt-feeder-test.XXXXXX); \
	trap 'rm -f "$$test_bin"' EXIT; \
	$(HOST_CC) -Wall -Wextra -DMQTT_PAL_NO_MUTEX -I. -Imqtt-c \
	  tests/test-commands.c config.c ha_discovery.c mqtt-c/mqtt.c mqtt-c/mqtt_pal.c \
	  -o "$$test_bin"; "$$test_bin"

.PHONY: all clean test
