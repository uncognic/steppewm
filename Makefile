BUILD_DIR ?= build
BUILD_TYPE ?= release
PREFIX ?= /usr

.PHONY: all configure build clean install run

all: build

configure:
	@if [ -d $(BUILD_DIR) ]; then \
		meson setup --reconfigure $(BUILD_DIR) --buildtype=$(BUILD_TYPE) --prefix=$(PREFIX); \
	else \
		meson setup $(BUILD_DIR) --buildtype=$(BUILD_TYPE) --prefix=$(PREFIX); \
	fi

build:
	@[ -d $(BUILD_DIR) ] || $(MAKE) configure
	meson compile -C $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

install: build
	meson install -C $(BUILD_DIR)

run: build
	./$(BUILD_DIR)/steppewm
