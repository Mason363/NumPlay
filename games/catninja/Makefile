# Cat Ninja on the NumWorks calculator, an app of its own.
#   make          output/catninja.nwa
#   make check    link it like the calculator does (output/catninja.bin), within the RAM and the app space
#   make host     build/play: the game on a computer, for tests and pictures
#   make test     the cat's physics against the original's numbers (spec/), the first levels played with the keys,
#                 every level loaded, random keys, saves (kept, damaged, lost, from the copy), with the sanitizers
#   make data     src/data.c from Cat Ninja's own files (GAMESRC: what JPEXS took out of CatNinja.swf, see its spec/)
Q ?= @
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
L = ../../launcher/src
PYTHON ?= python3
GAMESRC ?= $(HOME)/Documents/Assets/gamesrc/catninja

SRC = src/main.c src/game.c src/env.c src/draw.c src/data.c src/plat_eadk.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c \
  $(L)/inflate.c
HEADERS = src/cn.h src/data.h src/plat.h $(L)/gfx.h $(L)/font.h $(L)/np.h $(L)/inflate.h ../common/epsilon_app.h \
  ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1 -I$(L) -Isrc
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones), and the
# calculator's app space is 2031616 bytes
RAM = 133652
APP_SPACE = 2031616

.PHONY: build check clean host test data perf
build: $(BUILD_DIR)/catninja.nwa

check: $(BUILD_DIR)/catninja.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM) $< $(BUILD_DIR)/catninja.bin
	$(Q) n=$$(wc -c < $(BUILD_DIR)/catninja.bin); [ $$n -le $(APP_SPACE) ] || \
	  { echo "catninja.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }; \
	  echo "BIN     $(BUILD_DIR)/catninja.bin: $$n bytes, RAM within $(RAM) bytes"

$(BUILD_DIR)/obj/%.o: src/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/obj/%.o: $(L)/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

OBJ = $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(notdir $(SRC)))
$(BUILD_DIR)/catninja.nwa: $(OBJ) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $^ -lm -o $@
	$(Q) $(STRIP) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

data:
	$(PYTHON) -I tools/data.py $(GAMESRC) src

# every frame drawn, at an N0110's speed in tools/emu.py: the How to play page closed, then the title map run
# through into the first level
$(BUILD_DIR)/perf.nwa: $(OBJ) $(BUILD_DIR)/icon.o
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $^ -lm -o $@
perf: $(BUILD_DIR)/perf.nwa
	$(PYTHON) ../../tools/emu.py $< --nwlink "$(NWLINK)" --ms 16000 --out $(BUILD_DIR) --model n0110 \
	  --keys "400-500:ok,1500-15000:right,6000-6300:up,9000-9200:up,12000-12300:up"

# on a computer: tests/play.c checks the game and saves pictures
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -DHOST -I$(L) -Isrc \
  -fsanitize=address,undefined -fno-sanitize-recover=undefined
HOST_SRC = src/game.c src/env.c src/draw.c src/data.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c $(L)/inflate.c
host: build/play
build/play: tests/play.c src/main.c $(HOST_SRC) $(HEADERS)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) tests/play.c $(HOST_SRC) -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test

clean:
	$(Q) rm -rf $(BUILD_DIR) build
