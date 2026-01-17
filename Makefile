BUILD_DIR := build
CONFIG    := Debug
TARGET    := quick_game

.PHONY: all configure build run clean distclean re

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CONFIG)

build: configure
	cmake --build $(BUILD_DIR) --parallel

run: build
	./$(BUILD_DIR)/$(TARGET)

clean:
	cmake --build $(BUILD_DIR) --target clean

distclean:
	rm -rf $(BUILD_DIR)

re: distclean all
