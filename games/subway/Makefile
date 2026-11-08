# Subway Surfers on the NumWorks calculator.
#   make            output/subway.nwa
#   make check      link it like the calculator does (output/subway.bin)
#   make host       build/play: the game on a computer, for tests and screenshots
#   make test       the game played on a computer (tests/check.py, with the sanitizers)
#   make bench      how fast it draws a run at an N0110's speed (tools/bench.py, on NumPlay's emulator tools/emu.py;
#                   MODEL=n0120 for an N0120's memory)
#   make assets     src/assets.c again: the fonts (tools/assets.py: Python + Pillow + NumPy)
#   make data APK=DIR   src/data.c, src/data.h and src/icon.png again, from the game's own Android package unzipped
#                   in DIR (tools/apk.py, tools/icon.py: Python + UnityPy + Pillow + NumPy; the package is only read)
Q ?= @
# the ARM toolchain on the PATH, or in TOOLCHAIN=<its bin folder>
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
SIZE = $(BIN)arm-none-eabi-size
BUILD_DIR = output
MODEL ?= n0110

# the drawing runs every frame at -O2; the rest is small at -Os (each file says so)
SRC = $(filter-out src/plat_host.c,$(wildcard src/*.c))
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test bench assets data
build: $(BUILD_DIR)/subway.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/subway.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/subway.bin
	@echo "BIN     $(BUILD_DIR)/subway.bin: $$(wc -c < $(BUILD_DIR)/subway.bin) bytes"

$(BUILD_DIR)/subway.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) $(SIZE) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c plays it with scripted keys and saves screenshots
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isrc -DHOST
HOST_SAN = -fsanitize=address,undefined -fno-sanitize-recover=undefined
HOST_SRC = $(filter-out src/plat_eadk.c src/main.c,$(wildcard src/*.c))
host: build/play
build/play: $(HOST_SRC) tests/play.c $(HEADERS)
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) $(HOST_SRC) tests/play.c -lm -o $@
	@echo "HOST    $@"

test: build/play
	python3 tests/check.py build/play

bench:
	$(Q) $(MAKE) --no-print-directory build BUILD_DIR=build/bench EXTRA=-DSS_BENCH NWLINK="$(NWLINK)" TOOLCHAIN="$(TOOLCHAIN)"
	python3 tools/bench.py build/bench/subway.nwa --model $(MODEL)

assets:
	python3 tools/assets.py src/assets.c

data:
	python3 -I tools/apk.py $(APK)
	python3 -I tools/icon.py $(APK) src/icon.png
	python3 ../../tools/round_icons.py src/icon.png

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = subway
PORT_NAME = Subway Surfers
PORT_SRC = src/actors.c src/app.c src/assets.c src/data.c src/draw.c src/game.c src/icons.c src/main.c src/plat_eadk.c src/r3d.c src/save.c src/scene.c src/world.c
PORT_FLAGS = -DEPSILON_XL=1
include ../../platform/port.mk
