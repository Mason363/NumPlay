# PolyTrack on the NumWorks calculator.
#   make            output/polytrack.nwa
#   make check      link it like the calculator does (output/polytrack.bin), within the RAM and the app space
#   make host       build/play: the game on a computer, for tests and pictures
#   make test       every official track driven to the finish, the checkpoints, respawns and restarts, the physics,
#                   saves (kept, damaged, storage full), random keys and the same runs to the same hashes on every
#                   computer (with the sanitizers)
#   make armcheck   the same runs on the calculator's processor (in tools/emu.py), to the same hashes
#   make armperf    each track's run drawn at an N0110's speed (in tools/emu.py): the frame rate
#   make data       src/data.bin, src/data.c, src/data.h, src/logo.h and src/icon.png from PolyTrack's web build
#                   (POLYTRACK_WEB=its folder; Python with numpy, DracoPy and Pillow)
Q ?= @
# the ARM toolchain on the PATH, or in TOOLCHAIN=<its bin folder>
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
L = ../../launcher/src
POLYTRACK_WEB ?= $(HOME)/Documents/Assets/gamesrc/polytrack/web
PYTHON ?= python3

# the physics and the picture run every frame at -O2; the screens are small at -Os
HOT = src/car.c src/world.c src/ptmath.c src/render.c
COLD = src/main.c src/draw.c src/save.c src/ghost.c src/data.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
COLD_OBJ = $(patsubst %.c,$(BUILD_DIR)/cold/%.o,$(notdir $(COLD)))
HEADERS = $(wildcard src/*.h) $(L)/gfx.h $(L)/font.h $(L)/np.h ../common/epsilon_app.h ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1 -I$(L) -Isrc
CFLAGS += -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant -ffp-contract=off
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones), and the
# calculator's app space is 2031616 bytes
RAM = 133652
APP_SPACE = 2031616

.PHONY: build check clean host test data armcheck armperf
build: $(BUILD_DIR)/polytrack.nwa

check: $(BUILD_DIR)/polytrack.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM) $< $(BUILD_DIR)/polytrack.bin
	$(Q) n=$$(wc -c < $(BUILD_DIR)/polytrack.bin); [ $$n -le $(APP_SPACE) ] || \
	  { echo "polytrack.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }; \
	  echo "BIN     $(BUILD_DIR)/polytrack.bin: $$n bytes, $$(( $(APP_SPACE) - n )) to spare, RAM within $(RAM) bytes"

$(BUILD_DIR)/data.o: src/data.S src/data.bin | $(BUILD_DIR)
	$(Q) $(CC) -c -DDATA_BIN='"src/data.bin"' $< -o $@

$(BUILD_DIR)/cold/%.o: src/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@
$(BUILD_DIR)/cold/%.o: $(L)/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@

$(BUILD_DIR)/polytrack.nwa: $(HOT) src/plat_eadk.c $(COLD_OBJ) $(HEADERS) $(BUILD_DIR)/icon.o $(BUILD_DIR)/data.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) -O2 $(LDFLAGS) $(HOT) src/plat_eadk.c $(COLD_OBJ) $(BUILD_DIR)/data.o $(BUILD_DIR)/icon.o \
	  -lm -o $@
	$(Q) $(STRIP) $@

# (the physics on its own, flags as the game's)
$(BUILD_DIR)/armcheck.nwa: tests/armcheck.c tests/routes.h src/car.c src/world.c src/ptmath.c src/data.c $(HEADERS) \
                           $(BUILD_DIR)/icon.o $(BUILD_DIR)/data.o
	$(Q) $(CC) $(CFLAGS) -O2 $(LDFLAGS) tests/armcheck.c src/car.c src/world.c src/ptmath.c src/data.c \
	  $(BUILD_DIR)/data.o $(BUILD_DIR)/icon.o -lm -o $@
armcheck: $(BUILD_DIR)/armcheck.nwa
	$(PYTHON) tools/armcheck.py $<
$(BUILD_DIR)/armperf.nwa: tests/armperf.c tests/routes.h $(HOT) $(COLD_OBJ) $(HEADERS) $(BUILD_DIR)/icon.o $(BUILD_DIR)/data.o
	$(Q) $(CC) $(CFLAGS) -O2 $(PERF_CFLAGS) $(LDFLAGS) tests/armperf.c $(HOT) $(filter-out %/main.o,$(COLD_OBJ)) $(BUILD_DIR)/data.o \
	  $(BUILD_DIR)/icon.o -lm -o $@
armperf: $(BUILD_DIR)/armperf.nwa
	$(PYTHON) tools/armperf.py $<

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

data:
	$(PYTHON) tools/pack.py $(POLYTRACK_WEB)
	$(PYTHON) tools/logo.py $(POLYTRACK_WEB)

# on a computer: tests/play.c drives the tracks and the whole app, and saves pictures
HOST_SRC = tests/play.c src/car.c src/world.c src/ptmath.c src/render.c src/draw.c src/save.c src/ghost.c src/data.c \
           $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
HOST_FLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Isrc -I$(L) -DHOST
host: build/play
build/data.o: src/data.S src/data.bin
	$(Q) mkdir -p build
	$(Q) cc -c -DDATA_BIN='"src/data.bin"' $< -o $@
build/play: $(HOST_SRC) src/main.c $(wildcard src/*.h) tests/routes.h build/data.o
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined $(HOST_SRC) build/data.o -lm \
	  -o $@
	@echo "HOST    $@"
test: build/play
	build/play test build

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = polytrack
PORT_NAME = PolyTrack
PORT_SRC = $(HOT) src/plat_eadk.c src/main.c src/draw.c src/save.c src/ghost.c src/data.c ../../launcher/src/gfx.c \
  ../../launcher/src/xfont.c
PORT_ASM = src/data.S
PORT_FLAGS = -DEPSILON_XL=1 -I../../launcher/src -Isrc -DDATA_BIN='"src/data.bin"' -ffp-contract=off
include ../../platform/port.mk
