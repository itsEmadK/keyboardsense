# SPDX-License-Identifier: GPL-3.0-only
.DEFAULT_GOAL := all
HOST_CC ?= cc
HOST_FLAGS = -std=c11 -Wall -Wextra -Werror -pedantic -Iinclude -g
CORE = src/input.c src/config.c src/usb_descriptors.c
SOURCES = $(CORE) src/main.c src/usb_input.c src/vpad.c
OBJECTS = $(SOURCES:src/%.c=build/ps5/%.o)

ifneq ($(strip $(filter-out test test-sanitize clean,$(or $(MAKECMDGOALS),all))),)
ifndef PS5_PAYLOAD_SDK
$(error Set PS5_PAYLOAD_SDK to the PS5 payload SDK v0.45+ directory; see README.md)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

CFLAGS += -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -MMD -MP
LDFLAGS += -lkernel_sys
.PHONY: all test test-sanitize clean package send
all: build/keyboardsense.elf

build/ps5/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

build/keyboardsense.elf: $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

build/test_core: tests/test_core.c $(CORE) include/input.h include/sce_pad.h include/usb_descriptors.h include/vpad.h
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_FLAGS) tests/test_core.c $(CORE) -lm -o $@

build/test_usb: tests/test_usb.c src/usb_input.c $(CORE) include/usb_input.h tests/stubs/usb_platform.h
	@mkdir -p $(@D)
	$(HOST_CC) -Itests/stubs $(HOST_FLAGS) tests/test_usb.c src/usb_input.c $(CORE) -lm -o $@

test: build/test_core build/test_usb
	./build/test_core
	./build/test_usb
	python3 -m unittest discover -s tests -p 'test_*.py' -v

test-sanitize:
	@mkdir -p build
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_core.c $(CORE) -lm -o build/test_core_sanitize
	./build/test_core_sanitize
	$(HOST_CC) -Itests/stubs $(HOST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_usb.c src/usb_input.c $(CORE) -lm -o build/test_usb_sanitize
	./build/test_usb_sanitize

package: build/keyboardsense.elf
	cp build/keyboardsense.elf dist/keyboardsense.elf
	cp config/config.ini dist/config.ini
	cp README.md TESTING.md LICENSE NOTICE dist/
	python3 tools/check_elf.py dist/keyboardsense.elf
	python3 tools/source_archive.py
	python3 tools/checksum.py dist/keyboardsense.elf dist/keyboardsense-source.tar.gz > dist/SHA256SUMS

PS5_PORT ?= 9021
send: build/keyboardsense.elf
	python3 tools/send_payload.py $(PS5_HOST) build/keyboardsense.elf --port $(PS5_PORT)

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
