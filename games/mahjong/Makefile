# Mahjong: Mahjong solitaire on the NumWorks calculator, an app of its own.
#   make          output/mahjong.nwa
#   make check    link it like the calculator does (output/mahjong.bin), within the RAM and the app space
#   make host     build/play: the game on a computer, for tests and pictures
#   make test     the rules, deals that can be won (every layout, many seeds), shuffles, a game won with the keys,
#                 saves (kept, damaged, from the copy) and random keys, with the sanitizers
#   make data     src/art.h and src/layouts.h (FONT: Fusion Pixel 12px's traditional Chinese BDF; LAYOUTS:
#                 KMahjongg's layouts folder)
Q ?= @
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
L = ../../launcher/src
PYTHON ?= python3
FONT ?= $(HOME)/Documents/Assets/gamesrc/mahjong/fusion/x/fusion-pixel-12px-monospaced-zh_hant.bdf
LAYOUTS ?= $(HOME)/Documents/Assets/gamesrc/mahjong/kmahjongg

SRC = src/main.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
HEADERS = src/art.h src/layouts.h $(L)/gfx.h $(L)/font.h $(L)/np.h ../common/epsilon_app.h ../common/epsilon_files.h \
  ../common/np_text.h ../common/np_lz.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1 -I$(L)
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
build: $(BUILD_DIR)/mahjong.nwa

check: $(BUILD_DIR)/mahjong.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM) $< $(BUILD_DIR)/mahjong.bin
	$(Q) n=$$(wc -c < $(BUILD_DIR)/mahjong.bin); [ $$n -le $(APP_SPACE) ] || \
	  { echo "mahjong.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }; \
	  echo "BIN     $(BUILD_DIR)/mahjong.bin: $$n bytes, RAM within $(RAM) bytes"

$(BUILD_DIR)/obj/%.o: src/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/obj/%.o: $(L)/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

OBJ = $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(notdir $(SRC)))
$(BUILD_DIR)/mahjong.nwa: $(OBJ) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
	$(Q) $(STRIP) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# every frame drawn, at an N0110's speed in tools/emu.py: how long one takes (a game, the cursor going round)
$(BUILD_DIR)/perf.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	$(Q) $(CC) $(CFLAGS) -DPERF $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -o $@
perf: $(BUILD_DIR)/perf.nwa
	$(PYTHON) ../../tools/emu.py $< --nwlink "$(NWLINK)" --ms 9000 --out $(BUILD_DIR) \
	  --keys "600-700:ok,1200-1300:ok,2000-2100:shift,2500-4500:right,4600-6500:down,6600-8000:left,8100-8200:shift"

data:
	$(PYTHON) tools/art.py $(FONT)
	$(PYTHON) -I tools/layouts.py $(LAYOUTS)

# on a computer: tests/play.c checks the game and saves pictures
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -DPLATFORM_DEVICE=1 -DHOST -I$(L) \
  $(shell $(NWLINK) eadk-cflags-simulator) -fsanitize=address,undefined -fno-sanitize-recover=undefined
host: build/play
build/play: tests/play.c $(SRC) $(HEADERS)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) tests/play.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test

clean:
	$(Q) rm -rf $(BUILD_DIR) build
