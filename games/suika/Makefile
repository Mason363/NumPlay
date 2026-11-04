# Suika Game: drop the fruit, join two of a kind, up to the watermelon.
#   make          build output/suika.nwa
#   make check    link the app like the calculator does (output/suika.bin)
#
# In NumPlay the game draws with the launcher's strips, shapes and fonts and saves through its
# file functions (module.o leaves them to the launcher); on its own it brings them along.
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
SRC = src/main.c
L = ../../launcher/src
HEADERS = ../common/epsilon_app.h ../common/epsilon_files.h ../common/np_text.h $(L)/gfx.h $(L)/np.h $(L)/font.h

CFLAGS = -std=c11 $(shell $(NWLINK) eadk-cflags-device)
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean
build: $(BUILD_DIR)/suika.nwa

check: $(BUILD_DIR)/suika.nwa
	$(Q) $(NWLINK) nwa-bin $< $(BUILD_DIR)/suika.bin
	@echo "BIN     $(BUILD_DIR)/suika.bin: $$(wc -c < $(BUILD_DIR)/suika.bin) bytes"

# compiled, then linked (the link options would upset the compiler)
$(BUILD_DIR)/main.o: $(SRC) $(HEADERS) | $(BUILD_DIR)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) -c $(SRC) -o $@
$(BUILD_DIR)/gfx.o: $(L)/gfx.c $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/icons.o: $(L)/icons.c $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/font_data.o: $(L)/font_data.c $(L)/font.h | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/xfont.o: $(L)/xfont.c $(L)/np_xfont.h $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/suika.nwa: $(BUILD_DIR)/main.o $(BUILD_DIR)/gfx.o $(BUILD_DIR)/icons.o $(BUILD_DIR)/font_data.o \
  $(BUILD_DIR)/xfont.o $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# NumPlay launcher: the app as a module (see ../../tools/npmodule.py) for the
# calculator, and as a relocatable object for the simulator build.
.PHONY: module sim-module
module: $(BUILD_DIR)/module.o
$(BUILD_DIR)/module-main.o: $(SRC) $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -DNP_MODULE=1 -c $(SRC) -o $@
$(BUILD_DIR)/module.o: $(BUILD_DIR)/module-main.o
	$(Q) $(CC) $(CFLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel $< -o $@

sim-module: $(BUILD_DIR)/sim-module.o
$(BUILD_DIR)/sim-module.o: $(SRC) $(HEADERS) | $(BUILD_DIR)
	$(Q) mkdir -p $(BUILD_DIR)/sim
	$(Q) cc -std=c11 -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) -DNP_MODULE=1 -Dmain=np_suika_main \
	  -c $(SRC) -o $(BUILD_DIR)/sim/main.o
	$(Q) ld -r $(BUILD_DIR)/sim/main.o -exported_symbol _np_suika_main -o $@

# on a computer, with the sanitizers: a bot plays 300 games (tests/play.c)
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -DPLATFORM_DEVICE=1 -DHOST \
  $(shell $(NWLINK) eadk-cflags-simulator) -fsanitize=address,undefined -fno-sanitize-recover=undefined
.PHONY: host test
host: build/play
build/play: $(SRC) tests/play.c $(HEADERS)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) tests/play.c $(L)/gfx.c $(L)/icons.c $(L)/font_data.c $(L)/xfont.c -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = suika
PORT_NAME = Suika Game
PORT_SRC = src/main.c ../../launcher/src/gfx.c ../../launcher/src/xfont.c
include ../../platform/port.mk
