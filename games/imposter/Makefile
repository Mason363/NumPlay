# Imposter on the NumWorks calculator (C), drawn with NumPlay's launcher (launcher/src/gfx.c and its fonts).
#   make            output/imposter.nwa
#   make check      link it like the calculator does (output/imposter.bin): RAM and the app space
#   make host       build/play: the game on a computer, for tests and screenshots
#   make test       the game played on a computer (tests/check.py, with the sanitizers)
Q ?= @
# the ARM toolchain on the PATH, or in TOOLCHAIN=<its bin folder>
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
SIZE = $(BIN)arm-none-eabi-size
BUILD_DIR = output

LAUNCHER = ../../launcher/src
SRC = $(filter-out src/plat_host.c,$(wildcard src/*.c)) $(LAUNCHER)/gfx.c $(LAUNCHER)/font_data.c $(LAUNCHER)/xfont.c
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h ../common/np_text.h \
  $(LAUNCHER)/gfx.h $(LAUNCHER)/np.h $(LAUNCHER)/font.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device)
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test
build: $(BUILD_DIR)/imposter.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones); installed on its
# own, the app must fit the calculator's space for apps (0x90200000 to 0x903F0000 in its flash)
RAM_LENGTH = 133652
APP_SPACE = 2031616
check: $(BUILD_DIR)/imposter.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM_LENGTH) $< $(BUILD_DIR)/imposter.bin
	@n=$$(wc -c < $(BUILD_DIR)/imposter.bin); \
	  echo "BIN     $(BUILD_DIR)/imposter.bin: $$n bytes, $$(( $(APP_SPACE) - n )) to spare of the app space"; \
	  [ $$n -le $(APP_SPACE) ] || { echo "imposter.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }

# compiled, then linked (the link options would upset the compiler)
OBJ = $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(notdir $(SRC)))
vpath %.c src $(LAUNCHER)
$(BUILD_DIR)/obj/%.o: %.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/imposter.nwa: $(OBJ) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(OBJ) $(BUILD_DIR)/icon.o -o $@
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
HOST_SRC = src/game.c src/ui.c src/words.c src/plat_host.c $(LAUNCHER)/gfx.c $(LAUNCHER)/font_data.c $(LAUNCHER)/xfont.c
host: build/play
build/play: $(HOST_SRC) tests/play.c $(HEADERS)
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) $(HOST_SRC) tests/play.c -lm -o $@
	@echo "HOST    $@"

test: build/play
	python3 tests/check.py build/play

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, ti84ce, nspire, web...
PORT_ID = imposter
PORT_NAME = Imposter
PORT_SRC = $(filter-out src/plat_host.c,$(wildcard src/*.c)) $(LAUNCHER)/gfx.c $(LAUNCHER)/xfont.c
PORT_FLAGS = -Isrc
include ../../platform/port.mk
