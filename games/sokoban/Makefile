# Sokoban — push every box onto a goal, with David W. Skinner's Microban.
#   make          build output/sokoban.nwa
#   make check    link the app like the calculator does (output/sokoban.bin)
#   make test     every level checked against Microban and played through on a computer (tests/play.c)
#   make media    the pictures of the README and How to play
#
# tools/levels.py packs levels/microban.txt into output/levels.inc (Python 3; zopfli if installed): all
# 155 levels on its own, the first NUMPLAY_LEVELS inside NumPlay, where room is short.
# In NumPlay the game draws with the launcher's strips, shapes and fonts, saves through its file
# functions and unpacks its levels with its inflater (module.o leaves them to the launcher); on its
# own it brings them along.
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
SRC = src/main.c
LEVELS = $(BUILD_DIR)/levels.inc
NUMPLAY_LEVELS ?= 35
HEADERS = ../common/epsilon_app.h ../common/epsilon_files.h ../common/np_text.h ../../launcher/src/gfx.h \
  ../../launcher/src/np.h ../../launcher/src/font.h ../../launcher/src/inflate.h

CFLAGS = -std=c11 $(shell $(NWLINK) eadk-cflags-device) -I$(BUILD_DIR)
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -fno-schedule-insns2 -fno-reorder-blocks -fno-tree-loop-optimize # (smaller)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test media
build: $(BUILD_DIR)/sokoban.nwa

check: $(BUILD_DIR)/sokoban.nwa
	$(Q) $(NWLINK) nwa-bin $< $(BUILD_DIR)/sokoban.bin
	@echo "BIN     $(BUILD_DIR)/sokoban.bin: $$(wc -c < $(BUILD_DIR)/sokoban.bin) bytes"

$(LEVELS): tools/levels.py levels/microban.txt | $(BUILD_DIR)
	$(Q) python3 tools/levels.py $(BUILD_DIR)
$(BUILD_DIR)/numplay/levels.inc: tools/levels.py levels/microban.txt Makefile | $(BUILD_DIR)
	$(Q) python3 tools/levels.py $(BUILD_DIR)/numplay $(NUMPLAY_LEVELS)

# compiled, then linked (the link options would upset the compiler)
$(BUILD_DIR)/main.o: $(SRC) $(LEVELS) $(HEADERS) | $(BUILD_DIR)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) -c $(SRC) -o $@
$(BUILD_DIR)/gfx.o: ../../launcher/src/gfx.c $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/font_data.o: ../../launcher/src/font_data.c ../../launcher/src/font.h | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/xfont.o: ../../launcher/src/xfont.c ../../launcher/src/np_xfont.h $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/inflate.o: ../../launcher/src/inflate.c ../../launcher/src/inflate.h | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sokoban.nwa: $(BUILD_DIR)/main.o $(BUILD_DIR)/gfx.o $(BUILD_DIR)/font_data.o $(BUILD_DIR)/xfont.o \
  $(BUILD_DIR)/inflate.o $(BUILD_DIR)/icon.o
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

# on a computer, with the sanitizers: every level as published, played through, and pictures
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -DPLATFORM_DEVICE=1 -DHOST -I$(BUILD_DIR) \
  $(shell $(NWLINK) eadk-cflags-simulator) -fsanitize=address,undefined -fno-sanitize-recover=undefined
host: build/play
build/play: $(SRC) $(LEVELS) tests/play.c $(HEADERS)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) tests/play.c ../../launcher/src/gfx.c ../../launcher/src/font_data.c \
	  ../../launcher/src/xfont.c ../../launcher/src/inflate.c -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play check levels/microban.txt tests/solutions.txt
# the README's and How to play's pictures (tools/media.py)
media: build/play
	python3 tools/media.py

# NumPlay launcher: the app as a module (see ../../tools/npmodule.py) for the
# calculator, and as a relocatable object for the simulator build.
.PHONY: module sim-module
module: $(BUILD_DIR)/module.o
$(BUILD_DIR)/module-main.o: $(SRC) $(BUILD_DIR)/numplay/levels.inc $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) -I$(BUILD_DIR)/numplay $(CFLAGS) -DNP_MODULE=1 -c $(SRC) -o $@
$(BUILD_DIR)/module.o: $(BUILD_DIR)/module-main.o
	$(Q) $(CC) $(CFLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel $< -o $@

sim-module: $(BUILD_DIR)/sim-module.o
$(BUILD_DIR)/sim-module.o: $(SRC) $(BUILD_DIR)/numplay/levels.inc $(HEADERS) | $(BUILD_DIR)
	$(Q) mkdir -p $(BUILD_DIR)/sim
	$(Q) cc -std=c11 -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) -I$(BUILD_DIR)/numplay -DNP_MODULE=1 \
	  -Dmain=np_sokoban_main -c $(SRC) -o $(BUILD_DIR)/sim/main.o
	$(Q) ld -r $(BUILD_DIR)/sim/main.o -exported_symbol _np_sokoban_main -o $@

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = sokoban
PORT_NAME = Sokoban
PORT_SRC = src/main.c ../../launcher/src/gfx.c ../../launcher/src/xfont.c ../../launcher/src/inflate.c
PORT_FLAGS = -I$(BUILD_DIR)
include ../../platform/port.mk
