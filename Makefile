BUILD_DIR ?= build
BUILD_TYPE ?= Debug
GENERATOR ?= Ninja

.PHONY: all configure build reconfigure clean run debug test

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/arm-none-eabi.cmake
	ln -sfn $(BUILD_DIR)/compile_commands.json compile_commands.json

build: configure
	cmake --build $(BUILD_DIR)

reconfigure:
	cmake -S . -B $(BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/arm-none-eabi.cmake
	ln -sfn $(BUILD_DIR)/compile_commands.json compile_commands.json

run: build
	qemu-system-arm -M lm3s6965evb -kernel $(BUILD_DIR)/stellaris_spi \
		-nographic -serial stdio -monitor none

debug: build
	qemu-system-arm -M lm3s6965evb -kernel $(BUILD_DIR)/stellaris_spi \
		-nographic -serial stdio -monitor none -S -gdb tcp::1234

test: build
	./run_test.sh

clean:
	cmake --build $(BUILD_DIR) --target clean
