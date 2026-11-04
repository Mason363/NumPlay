# NumChess — chess for the NumWorks calculator.
#   make          build output/chess.nwa
#   make run      install on a connected calculator
#   make check    link the app like the calculator does (output/chess.bin)
#   make test     run engine tests on the host
Q ?= @
CC = arm-none-eabi-gcc
NWLINK = npx --yes -- nwlink@1.0.0
BUILD_DIR = output
SRC = src/main.c src/chess.c src/libc.c

CFLAGS = -std=c99 $(shell $(NWLINK) eadk-cflags-device)
CFLAGS += -Os -Wall -fno-math-errno -fno-tree-loop-distribute-patterns -fno-reorder-functions -flto-partition=one
CFLAGS += -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build run check test clean
build: $(BUILD_DIR)/chess.nwa

run: $(BUILD_DIR)/chess.nwa
	@echo "INSTALL $<"
	$(Q) $(NWLINK) install-nwa $<

check: $(BUILD_DIR)/chess.nwa
	$(Q) $(NWLINK) nwa-bin $< $(BUILD_DIR)/chess.bin
	@echo "BIN     $(BUILD_DIR)/chess.bin: $$(wc -c < $(BUILD_DIR)/chess.bin) bytes"

$(BUILD_DIR)/chess.nwa: $(SRC) src/chess.h src/sprites.h $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@
	$(Q) arm-none-eabi-size $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

test: | $(BUILD_DIR)
	$(Q) cc -O2 -DPLATFORM_DEVICE=1 $(shell $(NWLINK) eadk-cflags-simulator) test/perft.c -o $(BUILD_DIR)/perft
	$(Q) $(BUILD_DIR)/perft

clean:
	$(Q) rm -rf $(BUILD_DIR)

# NumPlay launcher: the game as a module (see ../../tools/npmodule.py) for the
# calculator, and as a relocatable object for the simulator build.
.PHONY: module sim-module
module: $(BUILD_DIR)/module.o
$(BUILD_DIR)/module.o: $(SRC) src/chess.h src/sprites.h | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel $(SRC) -o $@

sim-module: $(BUILD_DIR)/sim-module.o
$(BUILD_DIR)/sim-module.o: src/main.c src/chess.c src/chess.h src/sprites.h | $(BUILD_DIR)
	$(Q) mkdir -p $(BUILD_DIR)/sim
	$(Q) for f in main chess; do cc -std=c99 -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) \
	  -Dmain=np_chess_main -c src/$$f.c -o $(BUILD_DIR)/sim/$$f.o || exit 1; done
	$(Q) ld -r $(BUILD_DIR)/sim/*.o -exported_symbol _np_chess_main -o $@

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = chess
PORT_NAME = Chess
PORT_SRC = src/main.c src/chess.c
include ../../platform/port.mk
