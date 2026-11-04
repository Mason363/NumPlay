# Champion Island: the Doodle Champion Island Games on the NumWorks calculator.
#   make            output/championisland.nwa
#   make check      link it like the calculator does (output/championisland.bin)
#   make host       build/play: the game on a computer, for tests and screenshots
#   make test       checks of the host build (tests/check.py)
#   make data       regenerate src/data.bin (needs the doodle: DOODLE=path, Node + Playwright, Python + Pillow)
#   make live       fetch the files of the doodle's update (build/live), which make data reads too
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
STRIP ?= arm-none-eabi-strip --strip-unneeded   # STRIP=true keeps symbols (tools/emu.py --profile)
BUILD_DIR = output
DOODLE ?= ../../../potherca-blog/google-doodle-champion-island
# the doodle as Google serves it now (its build rc7, the archive is rc6): two more side quests
LIVE ?= build/live
LIVE_URL = https://www.google.com/logos/2020/kitsune/rc7
LIVE_FILES = kitsune20.js kitsune_compiled_deferred_module.js messages.en-GB.nocache.json \
  $(addsuffix -sprite.png,preload shared cutscene overworld archery climbing marathon pingpong rugby skate swim)
GAME = $(HOT) $(COLD)
# the drawing and the island run every frame at -O2; menus, dialogue and the
# sports' rules are small at -Os (the app has to fit in the calculator)
HOT = src/res.c src/spr.c src/gfx.c src/font.c src/font_data.c src/node.c src/ent.c src/phys.c src/game.c \
  src/world.c src/lzma/LzmaDec.c
COLD = src/store.c src/menus.c src/dialog.c src/hud.c src/film.c src/names.c $(SPORTS)
SPORTS ?= $(wildcard src/sport_*.c)
SRC = $(GAME) src/plat_eadk.c src/main.c
COLD_OBJ = $(patsubst src/%.c,$(BUILD_DIR)/cold/%.o,$(COLD))
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1 -DDATA_BIN='"src/data.bin"'
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host data live test
build: $(BUILD_DIR)/championisland.nwa

# the calculator gives apps 153676 bytes of RAM (data + bss)
check: $(BUILD_DIR)/championisland.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 153676 $< $(BUILD_DIR)/championisland.bin
	@echo "BIN     $(BUILD_DIR)/championisland.bin: $$(wc -c < $(BUILD_DIR)/championisland.bin) bytes"

$(BUILD_DIR)/data.o: src/data.S src/data.bin | $(BUILD_DIR)
	$(Q) $(CC) -c -DDATA_BIN='"src/data.bin"' $< -o $@

$(BUILD_DIR)/cold/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@

$(BUILD_DIR)/championisland.nwa: $(HOT) src/plat_eadk.c src/main.c $(COLD_OBJ) $(HEADERS) $(BUILD_DIR)/icon.o $(BUILD_DIR)/data.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(HOT) src/plat_eadk.c src/main.c $(COLD_OBJ) $(BUILD_DIR)/data.o $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) arm-none-eabi-size $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isrc -DHOST -DDATA_BIN='"src/data.bin"'
HOST_OUT ?= build/play
host: $(HOST_OUT)
$(HOST_OUT): $(GAME) src/plat_host.c tests/play.c src/main.c src/data.S src/data.bin $(HEADERS)
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) -fsanitize=address,undefined $(GAME) src/plat_host.c src/main.c tests/play.c src/data.S -lm -o $@
	@echo "HOST    $@"

# the host build in scenes that once went wrong (tests/check.py)
test: $(HOST_OUT)
	python3 tests/check.py $(HOST_OUT)

data:
	cd tools && node extract.js $(DOODLE) ../build/ex
	cd tools && node extract.js $(DOODLE) ../build/ex7 $(abspath $(LIVE))
	python3 tools/pack.py $(DOODLE) $(LIVE)
	python3 tools/font.py $(DOODLE)

live:
	mkdir -p $(LIVE)
	for f in $(LIVE_FILES); do curl -sSf -o $(LIVE)/$$f $(LIVE_URL)/$$f || exit 1; done

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = championisland
PORT_NAME = Champion Island
PORT_SRC = src/store.c src/menus.c src/dialog.c src/hud.c src/film.c src/names.c src/sport_archery.c src/sport_climbing.c src/sport_marathon.c src/sport_pingpong.c src/sport_rugby.c src/sport_skate.c src/sport_swim.c src/res.c src/spr.c src/gfx.c src/font.c src/font_data.c src/node.c src/ent.c src/phys.c src/game.c src/world.c src/lzma/LzmaDec.c src/plat_eadk.c src/main.c
PORT_ASM = src/data.S
PORT_FLAGS = -DEPSILON_XL=1 -DDATA_BIN='"src/data.bin"'
include ../../platform/port.mk
