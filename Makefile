CC      = gcc
ARMCC   = arm-linux-gnueabihf-gcc
CFLAGS  = -Wall -Wextra -std=gnu99
BUILD   = build
COMMON  = protocol.c protocol.h

all: $(BUILD)/server $(BUILD)/client

server: $(BUILD)/server
client: $(BUILD)/client
arm: $(BUILD)/server_arm

$(BUILD)/server: server.c $(COMMON) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ server.c protocol.c -pthread

$(BUILD)/server_arm: server.c $(COMMON) | $(BUILD)
	$(ARMCC) $(CFLAGS) -o $@ server.c protocol.c -pthread

$(BUILD)/client: client.c $(COMMON) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ client.c protocol.c

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all server client arm clean
