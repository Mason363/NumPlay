# Monkey Mart on the NumWorks calculator.
#   make            output/monkeymart.nwa
#   make check      link it like the calculator does (output/monkeymart.bin)
#   make host       build/play: the game on a computer, for tests and pictures
#   make test       the rules against the web game's numbers, a bot through the first mart to the second, random
#                   keys, saves (with the sanitizers); with ORIGINAL=DIR (the web game taken apart, kept with the
#                   reference: its Lua sources in DIR/lua), the marts' tables against the game's own text too
#   make data WEB=DIR   src/data.c, src/data.h, src/art.c and src/art.h again, from the web game taken apart in DIR
#                       (tools/data.py and tools/art.py: Python + Pillow; nothing in DIR is run)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output

SRC = src/main.c src/game.c src/draw.c src/save.c src/data.c src/art.c src/plat_eadk.c
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test data
build: $(BUILD_DIR)/monkeymart.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/monkeymart.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/monkeymart.bin
	@echo "BIN     $(BUILD_DIR)/monkeymart.bin: $$(wc -c < $(BUILD_DIR)/monkeymart.bin) bytes"

$(BUILD_DIR)/monkeymart.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c runs the game with scripted keys and its bot, checks it and saves pictures
HOST_SRC = tests/play.c src/draw.c src/save.c src/data.c src/art.c
HOST_FLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Isrc -DHOST
host: build/play
build/play: $(HOST_SRC) src/main.c src/game.c tests/bot.h $(wildcard src/*.h)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined $(HOST_SRC) -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test build
	$(if $(ORIGINAL),python3 -I tools/data.py $(ORIGINAL) --check)

data:
	python3 -I tools/data.py $(WEB)

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = monkeymart
PORT_NAME = Monkey Mart
PORT_SRC = src/main.c src/game.c src/draw.c src/save.c src/data.c src/art.c src/plat_eadk.c
PORT_FLAGS = -DEPSILON_XL=1
include ../../platform/port.mk
