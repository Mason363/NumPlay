# Flappy Dunk on the NumWorks calculator, an app of its own.
#   make          output/flappydunk.nwa
#   make check    link it like the calculator does (output/flappydunk.bin), within the RAM and the app space
#   make host     build/play: the game on a computer, for tests and pictures
#   make test     the rules against the original's numbers (spec/), scripted runs through the challenges, random
#                 keys, saves (kept, damaged, lost, from the copy, from the first version's), with the sanitizers
#   make data     src/data.c from Flappy Dunk's own files (GAMESRC: what was taken out of its Android build)
#   make perf     every frame drawn in tools/emu.py at an N0110's speed, its frame times
Q ?= @
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
L = ../../launcher/src
PYTHON ?= python3
GAMESRC ?= $(HOME)/Documents/Assets/gamesrc/flappydunk

# the physics and the drawing run every frame at -O2; the rest is small at -Os
HOT = src/game.c src/draw.c
COLD = src/main.c src/data.c src/plat_eadk.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c $(L)/icons.c
HEADERS = src/fd.h src/data.h src/draw.h src/plat.h $(L)/gfx.h $(L)/font.h $(L)/np.h ../common/epsilon_app.h \
  ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1 -I$(L) -Isrc
CFLAGS += -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones), and the
# calculator's app space is 2031616 bytes
RAM = 133652
APP_SPACE = 2031616

.PHONY: build check clean host test data perf
build: $(BUILD_DIR)/flappydunk.nwa

check: $(BUILD_DIR)/flappydunk.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM) $< $(BUILD_DIR)/flappydunk.bin
	$(Q) n=$$(wc -c < $(BUILD_DIR)/flappydunk.bin); [ $$n -le $(APP_SPACE) ] || \
	  { echo "flappydunk.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }; \
	  echo "BIN     $(BUILD_DIR)/flappydunk.bin: $$n bytes, $$(( $(APP_SPACE) - n )) to spare, RAM within $(RAM) bytes"

$(BUILD_DIR)/hot/%.o: src/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) -O2 -c $< -o $@
$(BUILD_DIR)/cold/%.o: src/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@
$(BUILD_DIR)/cold/%.o: $(L)/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@

OBJ = $(patsubst %.c,$(BUILD_DIR)/hot/%.o,$(notdir $(HOT))) $(patsubst %.c,$(BUILD_DIR)/cold/%.o,$(notdir $(COLD)))
$(BUILD_DIR)/flappydunk.nwa: $(OBJ) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) -Os $(LDFLAGS) $^ -lm -o $@
	$(Q) $(STRIP) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

data:
	$(PYTHON) -I tools/data.py $(GAMESRC) src

# every frame drawn, at an N0110's speed in tools/emu.py: How to play closed, then an endless run, flapping
$(BUILD_DIR)/perf.nwa: $(OBJ) $(BUILD_DIR)/icon.o
	$(Q) $(CC) $(CFLAGS) -Os $(LDFLAGS) $^ -lm -o $@
perf: $(BUILD_DIR)/perf.nwa
	$(PYTHON) ../../tools/emu.py $< --nwlink "$(NWLINK)" --ms 9000 --out $(BUILD_DIR) --model n0110 \
	  --keys "400-480:ok,1500-1580:ok,2200-2280:ok,2700-2780:ok,3200-3280:ok,3700-3780:ok,4200-4280:ok,4700-4780:ok"

# on a computer: tests/play.c checks the game and saves pictures
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -DHOST -I$(L) -Isrc \
  -fsanitize=address,undefined -fno-sanitize-recover=undefined
HOST_SRC = src/game.c src/draw.c src/data.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c $(L)/icons.c
host: build/play
build/play: tests/play.c src/main.c $(HOST_SRC) $(HEADERS)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) tests/play.c $(HOST_SRC) -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, casio-cg
PORT_ID = flappydunk
PORT_NAME = Flappy Dunk
PORT_SRC = src/main.c src/game.c src/draw.c src/data.c src/plat_eadk.c ../../launcher/src/gfx.c ../../launcher/src/xfont.c ../../launcher/src/icons.c
PORT_FLAGS = -DEPSILON_XL=1 -I../../launcher/src -Isrc
include ../../platform/port.mk
