# Universal Paperclips on the NumWorks calculator.
#   make            output/paperclips.nwa
#   make check      link it like the calculator does (output/paperclips.bin)
#   make host       build/play: the game on a computer, for tests and pictures
#   make test       the rules against the web build's numbers, a bot playing the whole game to its end through the
#                   keys, saves (kept, damaged), random keys, the same game twice to the same hash (with the
#                   sanitizers)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
L = ../../launcher/src

SRC = src/main.c src/sim.c src/projects.c src/num.c src/ui.c src/save.c src/plat_eadk.c src/font_data.c $(L)/gfx.c \
      $(L)/xfont.c
HEADERS = $(wildcard src/*.h) $(L)/gfx.h $(L)/font.h $(L)/np.h ../common/epsilon_app.h ../common/epsilon_files.h

# (no -fsingle-precision-constant: the game's numbers are doubles, as in JavaScript)
CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1 -I$(L) -Isrc
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -ffp-contract=off
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test
build: $(BUILD_DIR)/paperclips.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/paperclips.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/paperclips.bin
	@echo "BIN     $(BUILD_DIR)/paperclips.bin: $$(wc -c < $(BUILD_DIR)/paperclips.bin) bytes"

$(BUILD_DIR)/paperclips.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c checks the rules, plays the whole game and random keys, and saves pictures
HOST_SRC = tests/play.c src/sim.c src/projects.c src/num.c src/ui.c src/save.c src/font_data.c $(L)/gfx.c $(L)/xfont.c
HOST_FLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Isrc -I$(L) -DHOST
host: build/play
build/play: $(HOST_SRC) src/main.c $(wildcard src/*.h) $(wildcard tests/*.h)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined $(HOST_SRC) -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test build

clean:
	$(Q) rm -rf $(BUILD_DIR) build
