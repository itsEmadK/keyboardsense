# SPDX-License-Identifier: GPL-3.0-only
.DEFAULT_GOAL := all
HOST_CC ?= cc
HOST_FLAGS = -std=c11 -Wall -Wextra -Werror -pedantic -Iinclude -g
CORE = src/input.c src/config.c src/usb_descriptors.c src/editor.c
SOURCES = $(CORE) src/main.c src/usb_input.c src/vpad.c src/web.c src/game_ps5.c
NATIVE_CPP = native/overlay_bridge.cpp native/shellui_injector.cpp native/stable_shellui_injector.cpp native/remote_call.cpp native/shellui_hook_controller.cpp native/stable_sampler/proc_rw_v960.cpp
NATIVE_C = native/stable_elfldr_bridge.c native/shsrv/pt.c
NATIVE_OBJECTS = $(NATIVE_CPP:%.cpp=build/%.o) $(NATIVE_C:%.c=build/%.o) build/native/renderer_blob.o
OBJECTS = $(SOURCES:src/%.c=build/ps5/%.o) build/ps5/web_assets.o $(NATIVE_OBJECTS)
WEB_FILES = $(wildcard web/* web/assets/keys/* web/assets/dualsense/* web/assets/dualsense/pressed/*)

# Host tests and cleanup do not require a PS5 SDK.
ifneq ($(strip $(filter-out test test-sanitize clean preview build/preview build/web_fixture,$(or $(MAKECMDGOALS),all))),)
ifndef PS5_PAYLOAD_SDK
$(error Set PS5_PAYLOAD_SDK to the PS5 payload SDK v0.45+ directory; see README.md)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

CFLAGS += -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -MMD -MP
LDFLAGS += -lkernel_sys -lpthread -lSceSystemService
NATIVE_FLAGS = -Iinclude -Inative -Inative/shsrv -DPS5 -DCOMMON_FPS_DIAGNOSTIC_SYSCTL_ONLY=1 -DCOMMON_FPS_V121=1 -O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -MMD -MP

.PHONY: all test test-sanitize clean package send preview
all: build/keyboardsense.elf

build/ps5/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

build/keyboardsense.elf: $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

build/native/%.o: native/%.cpp
	@mkdir -p $(@D)
	$(CXX) -std=c++20 -fno-exceptions -fno-rtti $(NATIVE_FLAGS) -c $< -o $@

build/native/%.o: native/%.c
	@mkdir -p $(@D)
	$(CC) -std=c11 $(NATIVE_FLAGS) -c $< -o $@

build/native/editor_renderer.o: src/editor.c
	@mkdir -p $(@D)
	$(CC) -std=c11 $(NATIVE_FLAGS) -c $< -o $@

build/native/config_renderer.o: src/config.c
	@mkdir -p $(@D)
	$(CC) -std=c11 $(NATIVE_FLAGS) -c $< -o $@

build/sidebar_renderer.elf: build/native/sidebar_renderer.o build/native/sidebar_entry.o build/native/editor_renderer.o build/native/config_renderer.o native/renderer.exports tools/check_renderer.py
	$(CXX) -shared -nostartfiles -nostdlib++ -Wl,-Bsymbolic,-e,elf_main,--hash-style=sysv,-z,now,-z,undefs,--gc-sections,--version-script=native/renderer.exports -o $@ $(filter %.o,$^) -lkernel_sys -lSceLibcInternal -lSceNet
	python3 tools/check_renderer.py $@

build/renderer_blob.cpp: build/sidebar_renderer.elf tools/embed_renderer.py
	python3 tools/embed_renderer.py $< $@

build/native/renderer_blob.o: build/renderer_blob.cpp
	$(CXX) -std=c++20 $(NATIVE_FLAGS) -c $< -o $@

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

build/test_editor: tests/test_editor.c $(CORE) include/editor.h
	$(HOST_CC) $(HOST_FLAGS) tests/test_editor.c $(CORE) -lm -o $@

build/web_fixture: tests/web_fixture.c src/web.c $(CORE) build/web_assets.c include/web.h
	$(HOST_CC) $(HOST_FLAGS) tests/web_fixture.c src/web.c $(CORE) build/web_assets.c -pthread -lm -o $@

build/web_lifecycle: tests/web_lifecycle.c src/web.c $(CORE) build/web_assets.c include/web.h
	$(HOST_CC) $(HOST_FLAGS) tests/web_lifecycle.c src/web.c $(CORE) build/web_assets.c -pthread -lm -o $@

test: build/test_core build/test_usb build/test_editor build/preview build/web_fixture build/web_lifecycle
	./build/test_core
	./build/test_usb
	./build/test_editor
	node tests/test_web.mjs
	./build/web_lifecycle
	python3 -m unittest discover -s tests -p 'test_*.py' -v

test-sanitize: build/web_assets.c
	@mkdir -p build
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_core.c $(CORE) -lm -o build/test_core_sanitize
	./build/test_core_sanitize
	$(HOST_CC) -Itests/stubs $(HOST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_usb.c src/usb_input.c $(CORE) -lm -o build/test_usb_sanitize
	./build/test_usb_sanitize
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_editor.c $(CORE) -lm -o build/test_editor_sanitize
	./build/test_editor_sanitize
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

-include $(OBJECTS:.o=.d) build/native/sidebar_renderer.d build/native/sidebar_entry.d build/native/editor_renderer.d build/native/config_renderer.d
