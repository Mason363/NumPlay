# Minesweeper — the Windows classic, with a no-guessing option.
#   make          build output/minesweeper.nwa
#   make check    link the app like the calculator does (output/minesweeper.bin)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
SRC = src/main.c
HEADERS = ../common/epsilon_app.h ../common/epsilon_files.h ../common/np_text.h

CFLAGS = -std=c11 $(shell $(NWLINK) eadk-cflags-device)
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean
build: $(BUILD_DIR)/minesweeper.nwa

check: $(BUILD_DIR)/minesweeper.nwa
	$(Q) $(NWLINK) nwa-bin $< $(BUILD_DIR)/minesweeper.bin
	@echo "BIN     $(BUILD_DIR)/minesweeper.bin: $$(wc -c < $(BUILD_DIR)/minesweeper.bin) bytes"

# compiled once (link-time optimization bytecode), then linked for each use
$(BUILD_DIR)/main.o: $(SRC) $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $(SRC) -o $@

$(BUILD_DIR)/minesweeper.nwa: $(BUILD_DIR)/main.o $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $< $(BUILD_DIR)/icon.o -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

clean:
	$(Q) rm -rf $(BUILD_DIR)

# NumPlay launcher: the app as a module (see ../../tools/npmodule.py) for the
# calculator, and as a relocatable object for the simulator build.
.PHONY: module sim-module
module: $(BUILD_DIR)/module.o
$(BUILD_DIR)/module.o: $(BUILD_DIR)/main.o
	$(Q) $(CC) $(CFLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel $< -o $@

sim-module: $(BUILD_DIR)/sim-module.o
$(BUILD_DIR)/sim-module.o: $(SRC) $(HEADERS) | $(BUILD_DIR)
	$(Q) mkdir -p $(BUILD_DIR)/sim
	$(Q) cc -std=c11 -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) -Dmain=np_minesweeper_main -c $(SRC) -o $(BUILD_DIR)/sim/main.o
	$(Q) ld -r $(BUILD_DIR)/sim/main.o -exported_symbol _np_minesweeper_main -o $@

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = minesweeper
PORT_NAME = Minesweeper
PORT_SRC = src/main.c
include ../../platform/port.mk
