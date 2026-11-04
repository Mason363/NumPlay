# Celeste for the NumWorks calculator.
#   make            output/celeste.nwa
#   make check      link it like the calculator does (output/celeste.bin)
#   make host       build/play: the game on a computer, for tests and screenshots
#   make test       checks of the host build (tests/check.py)
#   make data       regenerate src/data.bin from the game (CELESTE=<its Content folder>)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
STRIP ?= arm-none-eabi-strip --strip-unneeded
BUILD_DIR = output
GAME = src/res.c src/gfx.c src/util.c src/ent.c src/level.c src/tiles.c src/style.c src/player.c src/entities.c \
  src/fx.c src/ptypes.c src/font.c src/light.c src/spinner.c src/berry.c src/save.c src/text.c src/wipe.c src/cutscene.c src/npc.c src/talk.c src/heart.c src/cassette.c src/dust.c src/breath.c src/pic.c src/menu.c $(wildcard src/ch*.c) src/game.c \
  src/lzma/LzmaDec.c
SRC = $(GAME) src/plat_eadk.c src/main.c
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1 -DDATA_BIN='"src/data.bin"'
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host data test
build: $(BUILD_DIR)/celeste.nwa

# the RAM the calculator gives apps (data + bss) depends on its software: 153676 bytes on 25.2, 149500 on 24.10,
# 148928 on 23.2; checked against the least
check: $(BUILD_DIR)/celeste.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 148928 $< $(BUILD_DIR)/celeste.bin
	@n=$$(wc -c < $(BUILD_DIR)/celeste.bin); echo "BIN     $(BUILD_DIR)/celeste.bin: $$n bytes ($$((2031616 - n)) to spare)"; \
	  [ $$n -le 2031616 ] || { echo "Celeste is bigger than the calculator's app space (2031616 bytes)"; exit 1; }

$(BUILD_DIR)/data.o: src/data.S src/data.bin | $(BUILD_DIR)
	$(Q) $(CC) -c -DDATA_BIN='"src/data.bin"' $< -o $@

$(BUILD_DIR)/celeste.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o $(BUILD_DIR)/data.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/data.o $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) arm-none-eabi-size $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isrc -DHOST
HOST_OUT ?= build/play
host: $(HOST_OUT)
$(HOST_OUT): $(GAME) src/plat_host.c tests/play.c $(HEADERS)
	$(Q) mkdir -p $(dir $@) build/host-saves
	$(Q) cc $(HOST_FLAGS) -fsanitize=address,undefined $(GAME) src/plat_host.c tests/play.c -lm -o $@
	@echo "HOST    $@"

test: $(HOST_OUT)
	python3 tests/check.py $(HOST_OUT) src/data.bin

data:
	cd tools && python3 pack.py

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = celeste
PORT_NAME = Celeste
PORT_SRC = src/res.c src/gfx.c src/util.c src/ent.c src/level.c src/tiles.c src/style.c src/player.c src/entities.c src/fx.c src/ptypes.c src/font.c src/light.c src/spinner.c src/berry.c src/save.c src/text.c src/wipe.c src/cutscene.c src/npc.c src/talk.c src/heart.c src/cassette.c src/dust.c src/breath.c src/pic.c src/menu.c src/ch0.c src/ch1.c src/ch2.c src/ch3.c src/ch4.c src/ch5.c src/ch6.c src/ch7.c src/ch8.c src/ch9.c src/game.c src/lzma/LzmaDec.c src/plat_eadk.c src/main.c
PORT_ASM = src/data.S
PORT_FLAGS = -DDATA_BIN='"src/data.bin"' -DEPSILON_XL=1
# the TI-84 Plus CE has a version of its own (ce/: the game's pictures and rooms, its own drawing and RAM)
PORT_CE = ce
include ../../platform/port.mk
