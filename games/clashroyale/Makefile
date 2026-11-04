# Clash Royale on the NumWorks calculator.
#   make            output/clashroyale.nwa
#   make check      link it like the calculator does (output/clashroyale.bin)
#   make host       build/rules and build/shots: the game on a computer, for tests and screenshots
#   make test       the game played on a computer (tests/rules.c, with the sanitizers)
#   make data       src/data.bin, src/data.h, src/ids.h and src/tables.c from the game's Android package
#                   (APK=its path; Python + Pillow + NumPy + SciPy)

Q ?= @
# the ARM toolchain on the PATH, or in TOOLCHAIN=<its bin folder>
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
SIZE = $(BIN)arm-none-eabi-size
BUILD_DIR = output
APK ?=

# drawing and the battle run every frame at -O2; the rest is small at -Os
HOT = src/gfx.c src/view.c src/sim.c
COLD = $(filter-out $(HOT) src/plat_host.c src/plat_eadk.c,$(wildcard src/*.c))
COLD_OBJ = $(patsubst src/%.c,$(BUILD_DIR)/cold/%.o,$(COLD))
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h ../common/np_lz.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1 -DDATA_BIN='"src/data.bin"'
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test data
build: $(BUILD_DIR)/clashroyale.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/clashroyale.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/clashroyale.bin
	@echo "BIN     $(BUILD_DIR)/clashroyale.bin: $$(wc -c < $(BUILD_DIR)/clashroyale.bin) bytes"

$(BUILD_DIR)/data.o: src/data.S src/data.bin | $(BUILD_DIR)
	$(Q) $(CC) -c -DDATA_BIN='"src/data.bin"' $< -o $@

$(BUILD_DIR)/cold/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@

$(BUILD_DIR)/clashroyale.nwa: $(HOT) src/plat_eadk.c $(COLD_OBJ) $(HEADERS) $(BUILD_DIR)/icon.o $(BUILD_DIR)/data.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(HOT) src/plat_eadk.c $(COLD_OBJ) $(BUILD_DIR)/data.o $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) $(SIZE) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/rules.c checks units against the spec's numbers, plays bot against bot and random keys
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isrc -DHOST -DDATA_BIN='"src/data.bin"'
HOST_SAN = -fsanitize=address,undefined -fno-sanitize-recover=undefined
HOST_SRC = $(filter-out src/plat_eadk.c src/main.c,$(wildcard src/*.c))
host: build/rules

build/rules: $(HOST_SRC) tests/rules.c src/data.S src/data.bin $(HEADERS)
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) $(HOST_SRC) tests/rules.c src/data.S -lm -o $@

test: host
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/rules

data:
	python3 -I tools/pack.py $(APK)

clean:
	$(Q) rm -rf $(BUILD_DIR) build/rules build/host-saves build/shots

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = clashroyale
PORT_NAME = Clash Royale
PORT_SRC = src/bot.c src/game.c src/main.c src/save.c src/tables.c src/gfx.c src/view.c src/sim.c src/plat_eadk.c
PORT_ASM = src/data.S
PORT_FLAGS = -DEPSILON_XL=1 -DDATA_BIN='"src/data.bin"'
include ../../platform/port.mk
