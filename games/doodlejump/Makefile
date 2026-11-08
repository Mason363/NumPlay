# Doodle Jump on the NumWorks calculator, an app of its own.
#   make          output/doodlejump.nwa
#   make check    link it like the calculator does (output/doodlejump.bin), within the RAM and the app space
#   make host     build/play: the game on a computer, for tests and pictures
#   make test     the rules against the original's numbers (spec/), a long climb, the way up always reachable,
#                 random keys, the screens and saves (old ones too), with the sanitizers
#   make data     src/data.c from Doodle Jump's own pictures (GAMESRC: the Android release, see its spec/)
Q ?= @
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
PYTHON ?= python3
GAMESRC ?= $(HOME)/Documents/Assets/gamesrc/doodlejump

SRC = src/main.c src/game.c src/draw.c src/gfx.c src/data.c src/plat_eadk.c
HEADERS = src/dj.h src/data.h src/gfx.h src/plat.h ../common/epsilon_app.h ../common/epsilon_files.h ../common/np_lz.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1 -Isrc
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
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
build: $(BUILD_DIR)/doodlejump.nwa

check: $(BUILD_DIR)/doodlejump.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM) $< $(BUILD_DIR)/doodlejump.bin
	$(Q) n=$$(wc -c < $(BUILD_DIR)/doodlejump.bin); [ $$n -le $(APP_SPACE) ] || \
	  { echo "doodlejump.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }; \
	  echo "BIN     $(BUILD_DIR)/doodlejump.bin: $$n bytes, RAM within $(RAM) bytes"

$(BUILD_DIR)/obj/%.o: src/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) $(if $(filter data.c main.c,$(notdir $<)),-Os) -c $< -o $@

OBJ = $(patsubst src/%.c,$(BUILD_DIR)/obj/%.o,$(SRC))
$(BUILD_DIR)/doodlejump.nwa: $(OBJ) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
	$(Q) $(STRIP) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

data:
	$(PYTHON) -I tools/pack.py $(GAMESRC) src

# every frame drawn, at an N0110's speed in tools/emu.py: the guide, the title's play, then a climb steered right
$(BUILD_DIR)/perf.nwa: $(OBJ) $(BUILD_DIR)/icon.o
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
perf: $(BUILD_DIR)/perf.nwa
	$(PYTHON) ../../tools/emu.py $< --nwlink "$(NWLINK)" --ms 12000 --out $(BUILD_DIR) --model $(or $(MODEL),n0110) \
	  --keys "1000-1100:ok,1600-1700:ok,3000-4000:right,5000-5600:left,6000-6500:right"

# on a computer: tests/play.c checks the game and saves pictures
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -DHOST -Isrc \
  -fsanitize=address,undefined -fno-sanitize-recover=undefined
HOST_SRC = src/game.c src/draw.c src/gfx.c src/data.c
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
PORT_ID = doodlejump
PORT_NAME = Doodle Jump
PORT_SRC = $(SRC)
PORT_FLAGS = -DEPSILON_XL=1 -Isrc
include ../../platform/port.mk
