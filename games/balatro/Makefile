# Balatro for the NumWorks calculator.
#   make          build output/balatro.nwa
#   make check    link it like the calculator does and print the installed size
#   make sim      simulator build (output/balatro.nwb) for tools/record.py
#   make assets   regenerate src/assets.c from assets/ (Python 3, numpy, Pillow)
#   make run      install on a connected calculator
Q ?= @
CC = arm-none-eabi-gcc
NWLINK = npx --yes -- nwlink@1.0.0
PY ?= python3
BUILD_DIR = output
SRC = $(wildcard src/*.c)
HDR = $(wildcard src/*.h)
# NumPlay's language builds (NP_TEXT_EXTRA on in ../common/np_text.h): the names and descriptions,
# translated in src/texts.h, become output/texts.c (tools/mktexts.py) instead of data.c's
ifeq ($(shell grep -q 'define NP_TEXT_EXTRA 1' ../common/np_text.h && echo 1),1)
SRC += $(BUILD_DIR)/texts.c
endif

CFLAGS = -std=c99 -Isrc $(shell $(NWLINK) eadk-cflags-device)
CFLAGS += -Os -Wall -fno-math-errno -fno-tree-loop-distribute-patterns -fno-reorder-functions -flto-partition=one
CFLAGS += -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build run check sim assets clean module sim-module
build: $(BUILD_DIR)/balatro.nwa

run: $(BUILD_DIR)/balatro.nwa
	$(Q) $(NWLINK) install-nwa $<

check: $(BUILD_DIR)/balatro.nwa
	$(Q) $(NWLINK) nwa-bin $< $(BUILD_DIR)/balatro.bin
	@echo "BIN     $(BUILD_DIR)/balatro.bin: $$(wc -c < $(BUILD_DIR)/balatro.bin) bytes"

$(BUILD_DIR)/balatro.nwa: $(SRC) $(HDR) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@
	$(Q) arm-none-eabi-size $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

$(BUILD_DIR)/texts.c: src/texts.h tools/mktexts.py tools/mkdata.py | $(BUILD_DIR)
	$(Q) $(PY) tools/mktexts.py src/texts.h $@

assets:
	$(Q) $(PY) tools/mkassets.py

sim: $(BUILD_DIR)/balatro.nwb
$(BUILD_DIR)/balatro.nwb: $(SRC) $(HDR) | $(BUILD_DIR)
	$(Q) cc -std=c99 -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) -shared -undefined dynamic_lookup \
	  $(filter-out src/libc.c,$(SRC)) ../../launcher/sim/sim.c -o $@

clean:
	$(Q) rm -rf $(BUILD_DIR)

# NumPlay launcher: the game as a module (see ../../tools/npmodule.py) for the
# calculator, and as a relocatable object for the simulator build.
module: $(BUILD_DIR)/module.o
$(BUILD_DIR)/module.o: $(SRC) $(HDR) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel $(SRC) -o $@

sim-module: $(BUILD_DIR)/sim-module.o
$(BUILD_DIR)/sim-module.o: $(SRC) $(HDR) | $(BUILD_DIR)
	$(Q) mkdir -p $(BUILD_DIR)/sim
	$(Q) for f in $(basename $(notdir $(filter-out src/libc.c,$(SRC)))); do cc -std=c99 -O2 -fPIC \
	  $(shell $(NWLINK) eadk-cflags-simulator) -Dmain=np_balatro_main -c src/$$f.c -o $(BUILD_DIR)/sim/$$f.o || exit 1; done
	$(Q) ld -r $(BUILD_DIR)/sim/*.o -exported_symbol _np_balatro_main -o $@

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = balatro
PORT_NAME = Balatro
PORT_SRC = src/art.c src/assets.c src/bg.c src/cons.c src/data.c src/draw.c src/ev.c src/flame.c src/fx.c src/game.c src/gfx.c src/jokers.c src/main.c src/save.c src/score.c src/ui.c src/util.c
PORT_FLAGS = -Isrc
# the TI-84 Plus CE has a version of its own (ce/, a CE toolchain project): make port TARGET=ti84ce builds it
PORT_CE = ce
include ../../platform/port.mk
