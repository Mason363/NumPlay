# Cookie Clicker on the NumWorks calculator.
#   make            output/cookieclicker.nwa
#   make check      link it like the calculator does (output/cookieclicker.bin)
#   make host       build/play: the game on a computer, for tests and screenshots
#   make test       the game played on a computer (tests/check.py, with the sanitizers)
#   make data       src/data.bin and src/data.h from the public material tools/fetch.py gathers
#                   (REF=its folder; Node runs the game's script for its numbers and texts, then Python with
#                   Pillow and NumPy packs them)
Q ?= @
# the ARM toolchain on the PATH, or in TOOLCHAIN=<its bin folder>
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
SIZE = $(BIN)arm-none-eabi-size
BUILD_DIR = output
REF ?=

# the drawing runs every frame at -O2; the rest is small at -Os
HOT = src/gfx.c
COLD = $(filter-out $(HOT) src/plat_host.c src/plat_eadk.c,$(wildcard src/*.c))
COLD_OBJ = $(patsubst %.c,$(BUILD_DIR)/cold/%.o,$(notdir $(COLD)))
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h

# (its numbers are doubles, as the game's: no -fsingle-precision-constant)
CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1 -DDATA_BIN='"src/data.bin"'
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test data
build: $(BUILD_DIR)/cookieclicker.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/cookieclicker.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/cookieclicker.bin
	@echo "BIN     $(BUILD_DIR)/cookieclicker.bin: $$(wc -c < $(BUILD_DIR)/cookieclicker.bin) bytes"

$(BUILD_DIR)/data.o: src/data.S src/data.bin | $(BUILD_DIR)
	$(Q) $(CC) -c -DDATA_BIN='"src/data.bin"' $< -o $@

$(BUILD_DIR)/cold/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@

$(BUILD_DIR)/cookieclicker.nwa: $(HOT) src/plat_eadk.c $(COLD_OBJ) $(HEADERS) $(BUILD_DIR)/icon.o $(BUILD_DIR)/data.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(HOT) src/plat_eadk.c $(COLD_OBJ) $(BUILD_DIR)/data.o $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) $(SIZE) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c plays it with scripted keys and saves screenshots
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isrc -DHOST -DDATA_BIN='"src/data.bin"'
HOST_SAN = -fsanitize=address,undefined -fno-sanitize-recover=undefined
HOST_SRC = $(filter-out src/plat_eadk.c,$(wildcard src/*.c))
host: build/play
build/play: $(HOST_SRC) tests/play.c src/data.S src/data.bin $(HEADERS)
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) $(HOST_SRC) tests/play.c src/data.S -lm -o $@
	@echo "HOST    $@"

test: build/play
	python3 tests/check.py build/play

data:
	@test -n "$(REF)" || { echo "make data REF=<the folder tools/fetch.py filled>"; exit 1; }
	node tools/extract.js $(REF) $(REF)/game.json
	python3 -I tools/pack.py $(REF) $(REF)/game.json

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = cookieclicker
PORT_NAME = Cookie Clicker
PORT_SRC = src/ascend.c src/elder.c src/game.c src/garden.c src/grimoire.c src/logic.c src/lumps.c src/main.c src/menus.c src/minigames.c src/news.c src/numbers.c src/pantheon.c src/prestige.c src/rows.c src/save.c src/season.c src/shimmer.c src/special.c src/text.c src/tooltip.c src/ui.c src/ui_left.c src/ui_store.c src/upgrades.c src/gfx.c src/plat_eadk.c
PORT_ASM = src/data.S
PORT_FLAGS = -DEPSILON_XL=1 -DDATA_BIN='"src/data.bin"'
include ../../platform/port.mk
