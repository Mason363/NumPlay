# NumTycoon — build a business empire, from a lemonade stand to a space port.
#   make          build output/tycoon.nwa
#   make run      install on a connected calculator
#   make check    link the app like the calculator does (output/tycoon.bin)
#   make host     build and run the PC test host (needs gcc, Python 3 with Pillow)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
SRC = src/main.c src/libc.c
HEADERS = ../common/epsilon_app.h ../common/epsilon_files.h

# (no -fsingle-precision-constant: the money is a double)
CFLAGS = -std=c11 $(shell $(NWLINK) eadk-cflags-device)
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build run check host clean
build: $(BUILD_DIR)/tycoon.nwa

run: $(BUILD_DIR)/tycoon.nwa
	@echo "INSTALL $<"
	$(Q) $(NWLINK) install-nwa $<

check: $(BUILD_DIR)/tycoon.nwa
	$(Q) $(NWLINK) nwa-bin $< $(BUILD_DIR)/tycoon.bin
	@echo "BIN     $(BUILD_DIR)/tycoon.bin: $$(wc -c < $(BUILD_DIR)/tycoon.bin) bytes"

$(BUILD_DIR)/tycoon.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@
	$(Q) arm-none-eabi-size $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# the real game code on a PC, with a fake screen: test/host.c (see its header)
host:
	$(Q) TT=$(abspath $(BUILD_DIR)/host) test/build.sh
	@echo "built $(BUILD_DIR)/host/host (usage: host SCRIPT OUTDIR)"

clean:
	$(Q) rm -rf $(BUILD_DIR)

# NumPlay launcher: the game as a module (see ../../tools/npmodule.py) for the
# calculator, and as a relocatable object for the simulator build.
.PHONY: module sim-module
module: $(BUILD_DIR)/module.o
$(BUILD_DIR)/module.o: $(SRC) $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel $(SRC) -o $@

sim-module: $(BUILD_DIR)/sim-module.o
$(BUILD_DIR)/sim-module.o: src/main.c $(HEADERS) | $(BUILD_DIR)
	$(Q) mkdir -p $(BUILD_DIR)/sim
	$(Q) cc -std=c11 -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) -Dmain=np_tycoon_main -c src/main.c -o $(BUILD_DIR)/sim/main.o
	$(Q) ld -r $(BUILD_DIR)/sim/main.o -exported_symbol _np_tycoon_main -o $@
