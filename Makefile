# SPDX-License-Identifier: GPL-3.0-only
.DEFAULT_GOAL := all
HOST_CC ?= cc
HOST_FLAGS = -std=c11 -Wall -Wextra -Werror -pedantic -Iinclude -g
CORE = src/input.c src/config.c src/usb_descriptors.c
SOURCES = $(CORE) src/main.c src/usb_input.c src/vpad.c src/web.c
OBJECTS = $(SOURCES:src/%.c=build/ps5/%.o) build/ps5/web_assets.o
WEB_FILES = $(wildcard web/* web/assets/keys/* web/assets/dualsense/* web/assets/dualsense/pressed/*)

# Host tests and cleanup do not require a PS5 SDK.
ifneq ($(strip $(filter-out test test-sanitize clean preview build/preview build/web_fixture,$(or $(MAKECMDGOALS),all))),)
ifndef PS5_PAYLOAD_SDK
$(error Set PS5_PAYLOAD_SDK to the PS5 payload SDK v0.45+ directory; see README.md)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

CFLAGS += -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -MMD -MP
LDFLAGS += -lkernel_sys -lpthread

.PHONY: all test test-sanitize clean package send preview
all: build/keyboardsense.elf

build/ps5/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

build/keyboardsense.elf: $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

build/web_assets.c: tools/embed_web.py $(WEB_FILES)
	python3 tools/embed_web.py $@

build/ps5/web_assets.o: build/web_assets.c include/web.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

build/preview: tools/preview.c src/web.c $(CORE) build/web_assets.c include/web.h
	$(HOST_CC) $(HOST_FLAGS) tools/preview.c src/web.c $(CORE) build/web_assets.c -pthread -lm -o $@

preview: build/preview
	./build/preview

build/test_core: tests/test_core.c $(CORE) include/input.h include/sce_pad.h include/usb_descriptors.h include/vpad.h
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_FLAGS) tests/test_core.c $(CORE) -lm -o $@

build/test_usb: tests/test_usb.c src/usb_input.c $(CORE) include/usb_input.h tests/stubs/usb_platform.h
	@mkdir -p $(@D)
	$(HOST_CC) -Itests/stubs $(HOST_FLAGS) tests/test_usb.c src/usb_input.c $(CORE) -lm -o $@

build/web_fixture: tests/web_fixture.c src/web.c $(CORE) build/web_assets.c include/web.h
	$(HOST_CC) $(HOST_FLAGS) tests/web_fixture.c src/web.c $(CORE) build/web_assets.c -pthread -lm -o $@

build/web_lifecycle: tests/web_lifecycle.c src/web.c $(CORE) build/web_assets.c include/web.h
	$(HOST_CC) $(HOST_FLAGS) tests/web_lifecycle.c src/web.c $(CORE) build/web_assets.c -pthread -lm -o $@

test: build/test_core build/test_usb build/preview build/web_fixture build/web_lifecycle
	./build/test_core
	./build/test_usb
	node tests/test_web.mjs
	./build/web_lifecycle
	python3 -m unittest discover -s tests -p 'test_*.py' -v

test-sanitize: build/web_assets.c
	@mkdir -p build
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_core.c $(CORE) -lm -o build/test_core_sanitize
	./build/test_core_sanitize
	$(HOST_CC) -Itests/stubs $(HOST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_usb.c src/usb_input.c $(CORE) -lm -o build/test_usb_sanitize
	./build/test_usb_sanitize
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/web_fixture.c src/web.c $(CORE) build/web_assets.c -pthread -lm -o build/web_fixture_sanitize
	./build/web_fixture_sanitize > /dev/null
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/web_lifecycle.c src/web.c $(CORE) build/web_assets.c -pthread -lm -o build/web_lifecycle_sanitize
	./build/web_lifecycle_sanitize
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tools/preview.c src/web.c $(CORE) build/web_assets.c -pthread -lm -o build/preview_sanitize
	KS_PREVIEW_BINARY=$(CURDIR)/build/preview_sanitize python3 -m unittest discover -s tests -p 'test_http.py' -v

package: build/keyboardsense.elf
	cp build/keyboardsense.elf dist/keyboardsense.elf
	cp config/config.ini dist/config.ini
	cp README.md TESTING.md LICENSE NOTICE dist/
	cp web/CREDITS.txt dist/THIRD_PARTY_LICENSES.txt
	python3 tools/check_elf.py dist/keyboardsense.elf
	python3 tools/source_archive.py
	python3 tools/checksum.py dist/keyboardsense.elf dist/keyboardsense-source.tar.gz > dist/SHA256SUMS

PS5_PORT ?= 9021
send: build/keyboardsense.elf
	python3 tools/send_payload.py $(PS5_HOST) build/keyboardsense.elf --port $(PS5_PORT)

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
