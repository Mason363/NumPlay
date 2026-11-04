# Donkey Kong on the NumWorks calculator.
#   make            output/donkeykong.nwa
#   make check      link it like the calculator does (output/donkeykong.bin)
#   make host       build/play: the game on a computer, for tests and pictures
#   make test       the rules against the arcade's numbers, a bot through the four boards, random keys, saves
#                   (with the sanitizers)
#   make rom LST=FILE    src/rom.h again, from tcdev's listing of the arcade's program ROM (tools/rom.py)
#   make art             src/art.h again (tools/art.py)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
L = ../../launcher/src

SRC = src/main.c src/sim.c src/draw.c src/save.c src/plat_eadk.c
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h $(L)/gfx.h $(L)/np.h $(L)/font.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test rom art
build: $(BUILD_DIR)/donkeykong.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/donkeykong.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/donkeykong.bin
	@echo "BIN     $(BUILD_DIR)/donkeykong.bin: $$(wc -c < $(BUILD_DIR)/donkeykong.bin) bytes"

$(BUILD_DIR)/donkeykong.nwa: $(SRC) $(HEADERS) $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c runs the game with scripted keys, checks it and saves pictures
HOST_FLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Isrc -DHOST \
  -fsanitize=address,undefined -fno-sanitize-recover=undefined
host: build/play
build/play: $(filter-out src/plat_eadk.c,$(SRC)) tests/play.c $(HEADERS) $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) tests/play.c src/sim.c src/draw.c src/save.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test

rom:
	python3 -I tools/rom.py $(LST) src/rom.h
art:
	python3 -I tools/art.py src/art.h

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = donkeykong
PORT_NAME = Donkey Kong
PORT_SRC = src/main.c src/sim.c src/draw.c src/save.c src/plat_eadk.c ../../launcher/src/gfx.c ../../launcher/src/xfont.c
PORT_FLAGS = -DEPSILON_XL=1
include ../../platform/port.mk
