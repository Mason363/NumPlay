# Famidash on the NumWorks calculator, an app of its own.
#   make          output/famidash.nwa
#   make check    link it like the calculator does (output/famidash.bin), within the RAM and the app space
#   make host     build/play: the game on a computer, for tests and pictures
#   make test     the physics against the game's numbers, the first level played to its end with the keys,
#                 practice mode, random keys, saves (kept, damaged, lost, from the copy), with the sanitizers
#   make perf     every frame drawn in tools/emu.py, at an N0110's speed
#   make data     src/data.c from Famidash's files (GAMESRC: a checkout of tfdsoft/famidash, see its spec/)
Q ?= @
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
L = ../../launcher/src
PYTHON ?= python3
GAMESRC ?= $(HOME)/Documents/Assets/gamesrc/famidash/repo

SRC = src/main.c src/game.c src/ppu.c src/data.c src/plat_eadk.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
HEADERS = src/fd.h src/data.h src/sprites_fd.h src/plat.h $(L)/gfx.h $(L)/font.h $(L)/np.h ../common/epsilon_app.h \
  ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1 -I$(L) -Isrc
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
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
build: $(BUILD_DIR)/famidash.nwa

check: $(BUILD_DIR)/famidash.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM) $< $(BUILD_DIR)/famidash.bin
	$(Q) n=$$(wc -c < $(BUILD_DIR)/famidash.bin); [ $$n -le $(APP_SPACE) ] || \
	  { echo "famidash.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }; \
	  echo "BIN     $(BUILD_DIR)/famidash.bin: $$n bytes, RAM within $(RAM) bytes"

$(BUILD_DIR)/obj/%.o: src/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/obj/%.o: $(L)/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

OBJ = $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(notdir $(SRC)))
$(BUILD_DIR)/famidash.nwa: $(OBJ) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $^ -lm -o $@
	$(Q) $(STRIP) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# every frame drawn, at an N0110's speed in tools/emu.py: the title, then Stereo Madness with OK held now and then
$(BUILD_DIR)/perf.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	$(Q) $(CC) $(CFLAGS) -DPERF $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -lm -o $@
perf: $(BUILD_DIR)/perf.nwa
	$(PYTHON) ../../tools/emu.py $< --nwlink "$(NWLINK)" --ms 14000 --out $(BUILD_DIR) \
	  --keys "1500-1600:ok,2500-2600:ok,3500-3600:ok,5000-5100:ok,6000-6100:ok,7000-7100:ok,8000-8300:ok,9000-9100:ok,10000-10200:ok,11000-11100:ok,12000-12100:ok"

data:
	$(PYTHON) -I tools/data.py $(GAMESRC) src

# on a computer: tests/play.c checks the game and saves pictures
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -DHOST -I$(L) -Isrc \
  -fsanitize=address,undefined -fno-sanitize-recover=undefined
host: build/play
build/play: tests/play.c src/main.c src/game.c src/ppu.c src/data.c $(HEADERS)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) tests/play.c src/game.c src/ppu.c src/data.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# tests/bot.c: finds the presses for tests/play.c's played level (not part of the tests; macOS only)
bot: build/bot
build/bot: tests/bot.c src/main.c src/game.c src/ppu.c src/data.c $(HEADERS)
	$(Q) mkdir -p build
	$(Q) cc -std=gnu11 -O2 -DHOST -DFD_BOT -I$(L) -Isrc tests/bot.c src/game.c src/ppu.c src/data.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c -lm -o $@
	@echo "HOST    $@"
