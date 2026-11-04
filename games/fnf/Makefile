# Friday Night Funkin' on the NumWorks calculator.
#   make            output/fnf.nwa
#   make check      link it like the calculator does (output/fnf.bin)
#   make host       build/play: the game on a computer, for tests and pictures
#   make test       the rules against the game's own numbers (PBOT1, health, holds, ghost misses), every chart
#                   played by a bot to a full combo, random keys, saves (with the sanitizers)
#
# The charts and pictures are the Funkin' Crew's, and are not in this repository: tools/fetch.py fetches the files
# tools/ref.txt lists from their funkin.assets repository, at its pinned commit, into REF (git, nothing run), and
# tools/data.py packs them into $(BUILD_DIR)/gen (Python 3 with Pillow and numpy; zopfli packs them smaller).
# REF=DIR uses a copy of funkin.assets already there (its files are checked against tools/ref.txt).
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
PYTHON ?= python3
BUILD_DIR = output
REF ?= $(BUILD_DIR)/ref
GEN = $(BUILD_DIR)/gen
L = ../../launcher/src

SRC = src/main.c src/app.c src/game.c src/draw.c src/screens.c src/save.c src/inflate.c src/plat_eadk.c \
      $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c $(GEN)/data.c
HEADERS = $(wildcard src/*.h) $(GEN)/ids.h $(L)/gfx.h $(L)/font.h $(L)/np.h ../common/epsilon_app.h \
          ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1 -I$(L) -Isrc -I$(GEN)
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test data fetch
build: $(BUILD_DIR)/fnf.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/fnf.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/fnf.bin
	@echo "BIN     $(BUILD_DIR)/fnf.bin: $$(wc -c < $(BUILD_DIR)/fnf.bin) bytes"

$(BUILD_DIR)/fnf.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# the game's files: fetched (or checked), then packed
$(GEN)/ref.ok: tools/fetch.py tools/ref.txt | $(BUILD_DIR)
	@echo "FETCH   $(REF)"
	$(Q) $(PYTHON) -I tools/fetch.py $(REF)
	$(Q) mkdir -p $(GEN) && touch $@
$(GEN)/data.c: tools/data.py tools/atlas.py $(GEN)/ref.ok
	@echo "DATA    $(GEN)"
	$(Q) $(PYTHON) -I tools/data.py $(REF) $(GEN)
$(GEN)/ids.h: $(GEN)/data.c
fetch: $(GEN)/ref.ok
data: $(GEN)/data.c

# on a computer: tests/play.c checks the rules, plays every chart with a bot, random keys, saves, and takes pictures
HOST_SRC = tests/play.c src/app.c src/game.c src/draw.c src/screens.c src/save.c src/inflate.c \
           $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c $(GEN)/data.c
HOST_FLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Isrc -I$(L) -I$(GEN) -DHOST
host: build/play
build/play: $(HOST_SRC) src/main.c $(wildcard src/*.h) $(GEN)/ids.h
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined $(HOST_SRC) -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test build

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = fnf
PORT_NAME = Friday Night Funkin
PORT_SRC = src/main.c src/app.c src/game.c src/draw.c src/screens.c src/save.c src/inflate.c src/plat_eadk.c ../../launcher/src/gfx.c ../../launcher/src/xfont.c $(BUILD_DIR)/gen/data.c
PORT_FLAGS = -DEPSILON_XL=1 -I../../launcher/src -Isrc -I$(BUILD_DIR)/gen
include ../../platform/port.mk
