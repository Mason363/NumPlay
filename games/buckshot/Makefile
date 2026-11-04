# Buckshot: Buckshot Roulette for the NumWorks calculator (part of NumPlay).
#   make          build output/buckshot.nwa
#   make run      install on a connected calculator
#   make check    link the app like the calculator does, print the installed size
#   make sim      output/buckshot.nwb for the Epsilon simulator
#   make assets   rebuild src/assets.c from assets/ (tools/pack.py)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK = npx --yes -- nwlink@1.0.0
BUILD_DIR = output
SRC = src/main.c src/game.c src/scene.c src/anim.c src/story.c src/gfx.c src/pixcode.c src/assets.c src/font.c src/libc.c
HDR = $(wildcard src/*.h)

CFLAGS = -std=c99 $(shell $(NWLINK) eadk-cflags-device)
CFLAGS += -Os -Wall -fno-math-errno -fno-tree-loop-distribute-patterns -fno-reorder-functions -flto-partition=one
CFLAGS += -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build run check sim assets clean
build: $(BUILD_DIR)/buckshot.nwa

run: $(BUILD_DIR)/buckshot.nwa
	@echo "INSTALL $<"
	$(Q) $(NWLINK) install-nwa $<

check: $(BUILD_DIR)/buckshot.nwa
	$(Q) $(NWLINK) nwa-bin $< $(BUILD_DIR)/buckshot.bin
	@echo "BIN     $(BUILD_DIR)/buckshot.bin: $$(wc -c < $(BUILD_DIR)/buckshot.bin) bytes"

$(BUILD_DIR)/buckshot.nwa: $(SRC) $(HDR) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@
	$(Q) arm-none-eabi-size $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

assets:
	python3 tools/pack.py

sim: $(BUILD_DIR)/buckshot.nwb
$(BUILD_DIR)/buckshot.nwb: $(SRC) $(HDR) ../../launcher/sim/sim.c | $(BUILD_DIR)
	$(Q) cc -std=c99 -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) -shared -undefined dynamic_lookup \
	  $(filter-out src/libc.c,$(SRC)) ../../launcher/sim/sim.c -o $@

clean:
	$(Q) rm -rf $(BUILD_DIR)

# NumPlay launcher: the game as a module (see ../../tools/npmodule.py) for the
# calculator, and as a relocatable object for the simulator build.
.PHONY: module sim-module
module: $(BUILD_DIR)/module.o
$(BUILD_DIR)/module.o: $(SRC) $(HDR) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel $(SRC) -o $@

sim-module: $(BUILD_DIR)/sim-module.o
$(BUILD_DIR)/sim-module.o: $(SRC) $(HDR) | $(BUILD_DIR)
	$(Q) mkdir -p $(BUILD_DIR)/sim
	$(Q) for f in $(basename $(notdir $(filter-out src/libc.c,$(SRC)))); do cc -std=c99 -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) \
	  -Dmain=np_buckshot_main -c src/$$f.c -o $(BUILD_DIR)/sim/$$f.o || exit 1; done
	$(Q) ld -r $(BUILD_DIR)/sim/*.o -exported_symbol _np_buckshot_main -o $@

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = buckshot
PORT_NAME = Buckshot Roulette
PORT_SRC = src/main.c src/game.c src/scene.c src/anim.c src/story.c src/gfx.c src/pixcode.c src/assets.c src/font.c
include ../../platform/port.mk
