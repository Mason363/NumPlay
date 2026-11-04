# Tomb of the Mask on the NumWorks calculator.
#   make            output/tombofthemask.nwa
#   make check      link it like the calculator does (output/tombofthemask.bin)
#   make host       build/play: the game on a computer, for tests and pictures
#   make test       the rules against the original's numbers, a bot that clears the stages and climbs the arcade,
#                   random keys, saves (with the sanitizers); with ORIGINAL=DIR (the web game taken apart, kept
#                   with the reference: DIR/text/stages__*, sections__*), the packed maps against its own files too
#   make data REF=DIR   src/data.c, src/data.h, src/art.c and src/art.h again, from the web game taken apart in DIR
#                       (tools/data.py and tools/art.py: Python + Pillow; nothing in DIR is run)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output

SRC = src/main.c src/game.c src/world.c src/player.c src/things.c src/progress.c src/draw.c src/save.c src/data.c \
      src/art.c src/plat_eadk.c
RULES = src/game.c src/world.c src/player.c src/things.c src/progress.c
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h ../common/np_lz.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test data
build: $(BUILD_DIR)/tombofthemask.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/tombofthemask.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/tombofthemask.bin
	@echo "BIN     $(BUILD_DIR)/tombofthemask.bin: $$(wc -c < $(BUILD_DIR)/tombofthemask.bin) bytes"

$(BUILD_DIR)/tombofthemask.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c runs the game with scripted keys and its bot, checks it and saves pictures
HOST_SRC = tests/play.c $(RULES) src/draw.c src/save.c src/data.c src/art.c
HOST_FLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Isrc -DHOST
host: build/play
build/play: $(HOST_SRC) src/main.c tests/bot.h $(wildcard src/*.h)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined $(HOST_SRC) -lm -o $@
	@echo "HOST    $@"
test: build/play
	$(if $(ORIGINAL),TOTM_REF=$(ORIGINAL)) build/play test build

data:
	python3 -I tools/data.py $(REF)
	python3 -I tools/art.py $(REF)

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = tombofthemask
PORT_NAME = Tomb of the Mask
PORT_SRC = src/main.c src/game.c src/world.c src/player.c src/things.c src/progress.c src/draw.c src/save.c src/data.c src/art.c src/plat_eadk.c
PORT_FLAGS = -DEPSILON_XL=1
include ../../platform/port.mk
