BUILD_DIR := build
CONFIG    := Debug

CLIENT_BIN := quick_game_client
SERVER_BIN := quick_game_server

# Defaults (override like: make run-server PORT=7777)
PORT ?= 7777
HOST ?= 127.0.0.1

.PHONY: all configure build run run-client run-server clean distclean re

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CONFIG)

build: configure
	cmake --build $(BUILD_DIR) --parallel

# Backwards-compatible: `make run` runs the client by default
run: run-client

run-client: build
	./$(BUILD_DIR)/$(CLIENT_BIN)

run-server: build
	./$(BUILD_DIR)/$(SERVER_BIN) $(PORT)

clean:
	cmake --build $(BUILD_DIR) --target clean

distclean:
	rm -rf $(BUILD_DIR)

re: distclean all
