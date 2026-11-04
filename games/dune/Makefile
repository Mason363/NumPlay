# Dune on the NumWorks calculator.
#   make            output/dune.nwa
#   make check      link it like the calculator does (output/dune.bin)
#   make host       build/play: the game on a computer, for tests and pictures
#   make test       the rules against the original's numbers, a rider's runs, random keys, saves (with the
#                   sanitizers); with ORIGINAL=FILE (the web game's js/game.js, kept with the reference), its own
#                   terrain, input and physics code against the port's steps too (tests/oracle.cjs, Node)
#   make data WEB=DIR   src/art.c and src/art.h again, from the web build in DIR (tools/data.py: Python +
#                       Pillow; the build is only read)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output

SRC = src/main.c src/game.c src/draw.c src/save.c src/art.c src/plat_eadk.c
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test data
build: $(BUILD_DIR)/dune.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/dune.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/dune.bin
	@echo "BIN     $(BUILD_DIR)/dune.bin: $$(wc -c < $(BUILD_DIR)/dune.bin) bytes"

$(BUILD_DIR)/dune.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c runs the game with scripted keys, checks it and saves pictures
HOST_SRC = tests/play.c src/draw.c src/save.c src/art.c
HOST_FLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Isrc -DHOST
host: build/play
build/play: $(HOST_SRC) src/main.c src/game.c $(HEADERS)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined $(HOST_SRC) -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test build
	$(if $(ORIGINAL),ORIGINAL=$(ORIGINAL) node tests/oracle.cjs build/cases.json)

data:
	python3 -I tools/data.py $(WEB)

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = dune
PORT_NAME = Dune
PORT_SRC = src/main.c src/game.c src/draw.c src/save.c src/art.c src/plat_eadk.c
PORT_FLAGS = -DEPSILON_XL=1
include ../../platform/port.mk
