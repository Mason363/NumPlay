# Flappy Bird, with the settings and random events of All the Apps.
#   make          build output/flappybird.nwa
#   make check    link the app like the calculator does (output/flappybird.bin)
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
build: $(BUILD_DIR)/flappybird.nwa

check: $(BUILD_DIR)/flappybird.nwa
	$(Q) $(NWLINK) nwa-bin $< $(BUILD_DIR)/flappybird.bin
	@echo "BIN     $(BUILD_DIR)/flappybird.bin: $$(wc -c < $(BUILD_DIR)/flappybird.bin) bytes"

$(BUILD_DIR)/flappybird.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -o $@
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
$(BUILD_DIR)/module.o: $(SRC) $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel $(SRC) -o $@

sim-module: $(BUILD_DIR)/sim-module.o
$(BUILD_DIR)/sim-module.o: $(SRC) $(HEADERS) | $(BUILD_DIR)
	$(Q) mkdir -p $(BUILD_DIR)/sim
	$(Q) cc -std=c11 -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) -Dmain=np_flappybird_main -c $(SRC) -o $(BUILD_DIR)/sim/main.o
	$(Q) ld -r $(BUILD_DIR)/sim/main.o -exported_symbol _np_flappybird_main -o $@

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = flappybird
PORT_NAME = Flappy Bird
PORT_SRC = src/main.c
include ../../platform/port.mk
