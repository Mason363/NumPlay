# UNO on the NumWorks calculator.
#   make            output/uno.nwa
#   make check      link it like the calculator does (output/uno.bin)
#   make host       build/play: the game on a computer, for tests and pictures
#   make test       the rules against the instruction sheet, bots playing whole games to 500, random keys, saves
#                   (with the sanitizers)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
L = ../../launcher/src

SRC = src/main.c src/game.c src/bot.c src/draw.c src/save.c src/plat_eadk.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
HEADERS = $(wildcard src/*.h) $(L)/gfx.h $(L)/font.h $(L)/np.h ../common/epsilon_app.h ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1 -I$(L)
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test
build: $(BUILD_DIR)/uno.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/uno.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/uno.bin
	@echo "BIN     $(BUILD_DIR)/uno.bin: $$(wc -c < $(BUILD_DIR)/uno.bin) bytes"

$(BUILD_DIR)/uno.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c checks the rules, plays whole games with the bots and random keys, and saves pictures
HOST_SRC = tests/play.c src/game.c src/bot.c src/draw.c src/save.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
HOST_FLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Isrc -I$(L) -DHOST
host: build/play
build/play: $(HOST_SRC) src/main.c $(wildcard src/*.h)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined $(HOST_SRC) -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test build

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = uno
PORT_NAME = UNO
PORT_SRC = src/main.c src/game.c src/bot.c src/draw.c src/save.c src/plat_eadk.c ../../launcher/src/gfx.c ../../launcher/src/xfont.c
PORT_FLAGS = -DEPSILON_XL=1 -I../../launcher/src
include ../../platform/port.mk
