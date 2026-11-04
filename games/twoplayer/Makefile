# 2 Players: Pong, Air Hockey, Tanks, Light Cycles, Quick Draw and Tug of War, for two on one calculator.
#   make          build output/twoplayer.nwa
#   make check    link the app like the calculator does (output/twoplayer.bin)
#   make test     the tests on a computer (tests/play.c)
#
# In NumPlay the game draws with the launcher's strips, shapes and fonts and saves through its
# file functions (module.o leaves them to the launcher); on its own it brings them along.
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
SRC = src/main.c
HEADERS = ../common/epsilon_app.h ../common/epsilon_files.h ../common/np_text.h ../../launcher/src/gfx.h \
  ../../launcher/src/np.h ../../launcher/src/font.h

CFLAGS = -std=c11 $(shell $(NWLINK) eadk-cflags-device)
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
# (smaller still: NumPlay has little room left)
CFLAGS += -fno-schedule-insns2 -fno-reorder-blocks -fno-tree-loop-optimize
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test
build: $(BUILD_DIR)/twoplayer.nwa

check: $(BUILD_DIR)/twoplayer.nwa
	$(Q) $(NWLINK) nwa-bin $< $(BUILD_DIR)/twoplayer.bin
	@echo "BIN     $(BUILD_DIR)/twoplayer.bin: $$(wc -c < $(BUILD_DIR)/twoplayer.bin) bytes"

# compiled, then linked (the link options would upset the compiler)
$(BUILD_DIR)/main.o: $(SRC) $(HEADERS) | $(BUILD_DIR)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) -c $(SRC) -o $@
$(BUILD_DIR)/gfx.o: ../../launcher/src/gfx.c $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/font_data.o: ../../launcher/src/font_data.c ../../launcher/src/font.h | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/xfont.o: ../../launcher/src/xfont.c ../../launcher/src/np_xfont.h $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/twoplayer.nwa: $(BUILD_DIR)/main.o $(BUILD_DIR)/gfx.o $(BUILD_DIR)/font_data.o $(BUILD_DIR)/xfont.o \
  $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@

# on a computer, with the sanitizers: the rules, bots against bots, random sessions, old saves (tests/play.c)
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -DPLATFORM_DEVICE=1 -DHOST -ffp-contract=off \
  $(shell $(NWLINK) eadk-cflags-simulator) -fsanitize=address,undefined -fno-sanitize-recover=undefined
host: output/play
output/play: $(SRC) tests/play.c $(HEADERS)
	$(Q) mkdir -p output
	$(Q) cc $(HOST_FLAGS) tests/play.c ../../launcher/src/gfx.c ../../launcher/src/font_data.c \
	  ../../launcher/src/xfont.c -lm -o $@
	@echo "HOST    $@"
test: output/play
	output/play test

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
$(BUILD_DIR)/module-main.o: $(SRC) $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -DNP_MODULE=1 -c $(SRC) -o $@
$(BUILD_DIR)/module.o: $(BUILD_DIR)/module-main.o
	$(Q) $(CC) $(CFLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel $< -o $@

sim-module: $(BUILD_DIR)/sim-module.o
$(BUILD_DIR)/sim-module.o: $(SRC) $(HEADERS) | $(BUILD_DIR)
	$(Q) mkdir -p $(BUILD_DIR)/sim
	$(Q) cc -std=c11 -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) -DNP_MODULE=1 -Dmain=np_twoplayer_main \
	  -c $(SRC) -o $(BUILD_DIR)/sim/main.o
	$(Q) ld -r $(BUILD_DIR)/sim/main.o -exported_symbol _np_twoplayer_main -o $@

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = twoplayer
PORT_NAME = 2 Players
PORT_SRC = src/main.c ../../launcher/src/gfx.c ../../launcher/src/xfont.c
include ../../platform/port.mk
