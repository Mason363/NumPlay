# Jetpack Joyride on the NumWorks calculator.
#   make            output/jetpackjoyride.nwa
#   make check      link it like the calculator does (output/jetpackjoyride.bin)
#   make host       build/play: the game on a computer, for tests and pictures
#   make test       the rules against the original's numbers, a bot's runs, random keys, saves (with the sanitizers)
#   make data APK=DIR          src/data.h again, from the game's Android package unzipped in DIR (tools/data.py)
#   make art FB=DIR APK=DIR    src/art.h again: FB the PNGs of archive.org's Facebook assets, APK as above
#                              (tools/art.py: Python + Pillow; both are only read)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
L = ../../launcher/src

SRC = src/main.c src/sim.c src/draw.c src/save.c src/plat_eadk.c
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h ../common/np_text.h $(L)/gfx.h $(L)/np.h $(L)/font.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test data art
build: $(BUILD_DIR)/jetpackjoyride.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/jetpackjoyride.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/jetpackjoyride.bin
	@echo "BIN     $(BUILD_DIR)/jetpackjoyride.bin: $$(wc -c < $(BUILD_DIR)/jetpackjoyride.bin) bytes"

$(BUILD_DIR)/jetpackjoyride.nwa: $(SRC) $(HEADERS) $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c runs the game with scripted keys, checks it and saves pictures
HOST_FLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Isrc -DHOST \
  -fsanitize=address,undefined -fno-sanitize-recover=undefined
host: build/play
build/play: $(filter-out src/plat_eadk.c,$(SRC)) tests/play.c $(HEADERS) $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) tests/play.c src/sim.c src/draw.c src/save.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test

data:
	python3 -I tools/data.py $(APK) src/data.h
art:
	python3 -I tools/art.py $(FB) $(APK) src/art.h

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = jetpackjoyride
PORT_NAME = Jetpack Joyride
PORT_SRC = src/main.c src/sim.c src/draw.c src/save.c src/plat_eadk.c ../../launcher/src/gfx.c ../../launcher/src/xfont.c
PORT_FLAGS = -DEPSILON_XL=1
include ../../platform/port.mk
