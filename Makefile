BUILD_DIR ?= build
BUILD_TYPE ?= Release

.PHONY: all configure build clean install run

all: build

configure:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_INSTALL_PREFIX=/usr -G Ninja

build:
	cmake --build $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

install: build
	cmake --install $(BUILD_DIR)

run: build
	./$(BUILD_DIR)/steppewm
