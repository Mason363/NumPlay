# Among Us on the NumWorks calculator (C).
#   make            output/amongus.nwa
#   make check      link it like the calculator does (output/amongus.bin): RAM and the app space
#   make host       build/play: the game on a computer, for tests and screenshots
#   make test       the game played on a computer (tests/check.py, with the sanitizers)
#   make data       src/data.bin, src/data.h and src/world.h from the art taken out of the game
#                   (REF=that folder, Python + Pillow + NumPy)
Q ?= @
# the ARM toolchain on the PATH, or in TOOLCHAIN=<its bin folder>
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
SIZE = $(BIN)arm-none-eabi-size
BUILD_DIR = output
REF ?=

# the drawing runs every frame at -O2; the rest is small at -Os
HOT = src/gfx.c src/view.c src/ship.c
COLD = $(filter-out $(HOT) src/plat_host.c src/plat_eadk.c,$(wildcard src/*.c))
COLD_OBJ = $(patsubst src/%.c,$(BUILD_DIR)/cold/%.o,$(COLD))
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h ../common/np_lz.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1 -DDATA_BIN='"src/data.bin"'
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test data
build: $(BUILD_DIR)/amongus.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones); installed on its
# own, the app must fit the calculator's space for apps (0x90200000 to 0x903F0000 in its flash)
RAM_LENGTH = 133652
APP_SPACE = 2031616
check: $(BUILD_DIR)/amongus.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM_LENGTH) $< $(BUILD_DIR)/amongus.bin
	@n=$$(wc -c < $(BUILD_DIR)/amongus.bin); \
	  echo "BIN     $(BUILD_DIR)/amongus.bin: $$n bytes, $$(( $(APP_SPACE) - n )) to spare of the app space"; \
	  [ $$n -le $(APP_SPACE) ] || { echo "amongus.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }

$(BUILD_DIR)/data.o: src/data.S src/data.bin | $(BUILD_DIR)
	$(Q) $(CC) -c -DDATA_BIN='"src/data.bin"' $< -o $@

$(BUILD_DIR)/cold/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@

$(BUILD_DIR)/amongus.nwa: $(HOT) src/plat_eadk.c $(COLD_OBJ) $(HEADERS) $(BUILD_DIR)/icon.o $(BUILD_DIR)/data.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(HOT) src/plat_eadk.c $(COLD_OBJ) $(BUILD_DIR)/data.o $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) $(SIZE) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c plays it with scripted keys and saves screenshots
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isrc -DHOST -DDATA_BIN='"src/data.bin"'
HOST_SAN = -fsanitize=address,undefined -fno-sanitize-recover=undefined
HOST_SRC = $(filter-out src/plat_eadk.c src/main.c,$(wildcard src/*.c))
host: build/play
build/play: $(HOST_SRC) tests/play.c tests/rules.c src/data.S src/data.bin $(HEADERS)
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) $(HOST_SRC) tests/play.c tests/rules.c src/data.S -lm -o $@
	@echo "HOST    $@"

test: build/play
	python3 tests/check.py build/play

data:
	@test -n "$(REF)" || { echo "make data REF=<the art taken out of the game>"; exit 1; }
	python3 -I tools/pack.py $(REF)

clean:
	$(Q) rm -rf $(BUILD_DIR) build
