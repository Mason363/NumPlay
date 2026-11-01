# Snow Rider 3D on the NumWorks calculator.
#   make            output/snowrider.nwa
#   make check      link it like the calculator does (output/snowrider.bin)
#   make host       build/play: the game on a computer, scripted keys and pictures (tests/play.c)
#   make test       the slope made far ahead, two minutes down it, the walls (and their colliders whole), the
#                   snowballs' colliders, a jump, saves (kept, from the copy,
#                   damaged), buying a sled, 300 sessions of random keys (with the sanitizers)
#   make bench      how fast it draws a ride at an N0110's speed (tools/bench.py, on the emulator: NumPlay's tools/emu.py)
#   make data UNITY=DIR   src/data.c and src/data.h again, from Snow Rider 3D's Unity project's Assets folder in DIR
#                   (tools/gen.py: Python 3 with PyYAML, NumPy and Pillow; the project is only read)
Q ?= @
# the ARM toolchain on the PATH, or in TOOLCHAIN=<its bin folder>
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
SIZE = $(BIN)arm-none-eabi-size
BUILD_DIR = output

L = ../../launcher/src
SRC = $(filter-out src/plat_host.c,$(wildcard src/*.c)) $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h $(L)/gfx.h $(L)/font.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1 -I$(L)
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test data bench
build: $(BUILD_DIR)/snowrider.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/snowrider.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/snowrider.bin
	@echo "BIN     $(BUILD_DIR)/snowrider.bin: $$(wc -c < $(BUILD_DIR)/snowrider.bin) bytes"

$(BUILD_DIR)/snowrider.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) $(SIZE) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c plays it with scripted keys and saves pictures
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Isrc -I$(L) -DHOST
HOST_SAN = -fsanitize=address,undefined -fno-sanitize-recover=undefined
HOST_SRC = $(filter-out src/plat_eadk.c,$(wildcard src/*.c)) $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
host: build/play
build/play: $(HOST_SRC) tests/play.c $(HEADERS)
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) $(HOST_SRC) tests/play.c -lm -o $@
	@echo "HOST    $@"

test: build/play
	$(Q) mkdir -p build/test
	build/play test build/test

bench:
	$(Q) $(MAKE) --no-print-directory build BUILD_DIR=build/bench EXTRA=-DSR_BENCH NWLINK="$(NWLINK)" TOOLCHAIN="$(TOOLCHAIN)"
	python3 tools/bench.py build/bench/snowrider.nwa

data:
	python3 -I tools/gen.py $(UNITY) src
	python3 -I tools/logo.py $(UNITY) src

clean:
	$(Q) rm -rf $(BUILD_DIR) build
