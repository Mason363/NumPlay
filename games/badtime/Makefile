# Bad Time Simulator on the NumWorks calculator.
#   make            output/badtime.nwa
#   make check      link it like the calculator does (output/badtime.bin), within the RAM and the app space
#   make host       build/play: the game on a computer, for tests and pictures
#   make test       every attack against jcw87's scripts (tests/expect.h), the heart's and the damage's numbers, a
#                   bot through the whole fight to the win, the same fight to the same hash on every computer,
#                   saves (kept, damaged, storage full) and random keys (with the sanitizers)
#   make armperf    the busiest attacks drawn at an N0110's speed (in tools/emu.py): the frame rate
#   make data       src/attacks.c, src/attacks.h and tests/expect.h from jcw87's attack scripts (REF=its folder),
#                   src/art.c and src/art.h from tools/art.py
Q ?= @
# the ARM toolchain on the PATH, or in TOOLCHAIN=<its bin folder>
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
L = ../../launcher/src
REF ?= $(HOME)/Documents/Assets/gamesrc/badtime/ref
PYTHON ?= python3

# the fight and the drawing run every frame at -O2; the screens are small at -Os
HOT = src/sim.c src/draw.c src/fight.c
COLD = src/main.c src/save.c src/text.c src/art.c src/attacks.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
COLD_OBJ = $(patsubst %.c,$(BUILD_DIR)/cold/%.o,$(notdir $(COLD)))
HEADERS = $(wildcard src/*.h) $(L)/gfx.h $(L)/font.h $(L)/np.h ../common/epsilon_app.h ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1 -I$(L) -Isrc
CFLAGS += -Wall -Wextra -Wno-unused-parameter -fno-math-errno -ffp-contract=off
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones), and the
# calculator's app space is 2031616 bytes
RAM = 133652
APP_SPACE = 2031616

.PHONY: build check clean host test data armperf
build: $(BUILD_DIR)/badtime.nwa

check: $(BUILD_DIR)/badtime.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM) $< $(BUILD_DIR)/badtime.bin
	$(Q) n=$$(wc -c < $(BUILD_DIR)/badtime.bin); [ $$n -le $(APP_SPACE) ] || \
	  { echo "badtime.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }; \
	  echo "BIN     $(BUILD_DIR)/badtime.bin: $$n bytes, $$(( $(APP_SPACE) - n )) to spare, RAM within $(RAM) bytes"

$(BUILD_DIR)/cold/%.o: src/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@
$(BUILD_DIR)/cold/%.o: $(L)/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@

$(BUILD_DIR)/badtime.nwa: $(HOT) src/plat_eadk.c $(COLD_OBJ) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) -O2 $(LDFLAGS) $(HOT) src/plat_eadk.c $(COLD_OBJ) $(BUILD_DIR)/icon.o -o $@
	$(Q) $(STRIP) $@

# (the game with tools/armperf.py's hooks)
$(BUILD_DIR)/armperf.nwa: $(HOT) src/plat_eadk.c $(COLD) $(HEADERS) $(BUILD_DIR)/icon.o
	$(Q) $(CC) $(CFLAGS) -O2 -DPERF $(LDFLAGS) $(HOT) src/plat_eadk.c $(COLD) $(BUILD_DIR)/icon.o -o $@
armperf: $(BUILD_DIR)/armperf.nwa
	$(PYTHON) tools/armperf.py $<

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

data:
	$(PYTHON) -I tools/attacks.py $(REF)
	$(PYTHON) tools/art.py

# on a computer: tests/play.c checks the fight and the whole app, and saves pictures
HOST_SRC = tests/play.c src/sim.c src/fight.c src/draw.c src/save.c src/text.c src/art.c src/attacks.c \
           $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
HOST_FLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Isrc -I$(L) -DHOST
host: build/play
build/play: $(HOST_SRC) src/main.c $(wildcard src/*.h) tests/expect.h
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined $(HOST_SRC) -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test build

clean:
	$(Q) rm -rf $(BUILD_DIR) build
