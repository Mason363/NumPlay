# Level Devil — run to the door; the level has other plans.
#   make          build output/leveldevil.nwa
#   make check    link the app like the calculator does (output/leveldevil.bin)
#   make test     every room played through on a computer with its solution (tests/play.c,
#                 tests/solutions.txt)
#
# The rooms are the game's own: tools/scenes.json has their tiles and actors (tools/fetch.py takes
# them from the game's web build) and tools/rooms.py their scripts, which it packs into
# output/rooms.inc (Python 3). Its pictures and fonts are the game's own, in src/pictures.h
# (tools/pictures.py takes them from the game's web build); it draws them in the launcher's strips.
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
SRC = src/main.c
ROOMS = $(BUILD_DIR)/rooms.inc
HEADERS = ../common/epsilon_app.h ../common/epsilon_files.h ../common/np_text.h ../../launcher/src/gfx.h \
  ../../launcher/src/np.h ../../launcher/src/font.h

CFLAGS = -std=c11 $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1 -I$(BUILD_DIR)
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test
build: $(BUILD_DIR)/leveldevil.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/leveldevil.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/leveldevil.bin
	@echo "BIN     $(BUILD_DIR)/leveldevil.bin: $$(wc -c < $(BUILD_DIR)/leveldevil.bin) bytes"

$(ROOMS): tools/rooms.py tools/scenes.json | $(BUILD_DIR)
	$(Q) python3 tools/rooms.py $(BUILD_DIR)

# compiled, then linked (the link options would upset the compiler)
$(BUILD_DIR)/main.o: $(SRC) src/pictures.h $(ROOMS) $(HEADERS) | $(BUILD_DIR)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) -c $(SRC) -o $@
$(BUILD_DIR)/gfx.o: ../../launcher/src/gfx.c $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/font_data.o: ../../launcher/src/font_data.c ../../launcher/src/font.h | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/xfont.o: ../../launcher/src/xfont.c ../../launcher/src/np_xfont.h $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/leveldevil.nwa: $(BUILD_DIR)/main.o $(BUILD_DIR)/gfx.o $(BUILD_DIR)/font_data.o $(BUILD_DIR)/xfont.o \
  $(BUILD_DIR)/icon.o
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

# on a computer, with the sanitizers: the rooms played through, and pictures of them
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -DPLATFORM_DEVICE=1 -DHOST -I$(BUILD_DIR) \
  $(shell $(NWLINK) eadk-cflags-simulator) -fsanitize=address,undefined -fno-sanitize-recover=undefined
host: build/play
build/play: $(SRC) src/pictures.h $(ROOMS) tests/play.c $(HEADERS)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) tests/play.c ../../launcher/src/gfx.c ../../launcher/src/font_data.c \
	  ../../launcher/src/xfont.c -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play solve

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = leveldevil
PORT_NAME = Level Devil
PORT_SRC = src/main.c ../../launcher/src/gfx.c ../../launcher/src/xfont.c
PORT_FLAGS = -DEPSILON_XL=1 -I$(BUILD_DIR)
PORT_DEPS = $(ROOMS)
include ../../platform/port.mk
