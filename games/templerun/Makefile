# Temple Run on the NumWorks calculator.
#   make            output/templerun.nwa
#   make check      link it like the calculator does (output/templerun.bin)
#   make host       build/play: the game on a computer, for tests and screenshots
#   make test       the game played on a computer (tests/check.py, with the sanitizers)
#   make bench      how fast it draws a run at an N0110's speed (tools/bench.py, on NumPlay's emulator tools/emu.py)
#   make data DATA=DIR   src/data.c and src/data.h again: the game's own models, from its Android package (DIR: its
#                   assets/bin/Data; tools/apk.py: UnityPy, NumPy, Pillow; the package is only read)
#   make models     src/data.c and src/data.h as the port's stand-ins instead (tools/models.py: Python)
#   make hud DATA=DIR   src/hud.c and src/hud.h again: the game's interface pictures and golden lettering (tools/hud.py)
#   make floor DATA=DIR   src/floor.c and src/floor.h again: each path piece's ground and obstacles, from the game's
#                   Android package (DIR: its assets/bin/Data; tools/floor.py with tools/apk.py: UnityPy, NumPy;
#                   the package is only read). Then make models (the pieces' stand-ins follow their ground).
Q ?= @
# the ARM toolchain on the PATH, or in TOOLCHAIN=<its bin folder>
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
SIZE = $(BIN)arm-none-eabi-size
BUILD_DIR = output

# the drawing runs every frame at -O2; the rest is small at -Os (each file says so)
SRC = $(filter-out src/plat_host.c,$(wildcard src/*.c))
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test bench models floor data hud
build: $(BUILD_DIR)/templerun.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/templerun.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/templerun.bin
	@echo "BIN     $(BUILD_DIR)/templerun.bin: $$(wc -c < $(BUILD_DIR)/templerun.bin) bytes"

$(BUILD_DIR)/templerun.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) $(SIZE) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c plays it with scripted keys and saves screenshots
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isrc -DHOST
HOST_SAN = -fsanitize=address,undefined -fno-sanitize-recover=undefined
HOST_SRC = $(filter-out src/plat_eadk.c src/main.c,$(wildcard src/*.c))
host: build/play
build/play: $(HOST_SRC) tests/play.c $(HEADERS)
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) $(HOST_SRC) tests/play.c -lm -o $@
	@echo "HOST    $@"

test: build/play
	python3 tests/check.py build/play

bench:
	$(Q) $(MAKE) --no-print-directory build BUILD_DIR=build/bench EXTRA=-DTR_BENCH NWLINK="$(NWLINK)" TOOLCHAIN="$(TOOLCHAIN)"
	python3 tools/bench.py build/bench/templerun.nwa

models:
	python3 tools/models.py src

floor:
	python3 -I tools/floor.py $(DATA) src

data:
	python3 -I tools/apk.py $(DATA) src

hud:
	python3 -I tools/hud.py $(DATA) src

clean:
	$(Q) rm -rf $(BUILD_DIR) build
