# Hollow Knight for the NumWorks calculator.
#   make            output/hollowknight.nwa
#   make check      link it like the calculator does (output/hollowknight.bin)
#   make host       build/play: the game on a computer, for tests and screenshots
#   make test       checks of the host build (tests/check.py)
#   make data       regenerate src/data.bin from the game (HOLLOWKNIGHT=<its Data folder>)
#   XL=1            the bigger game, past Greenpath too (src/data_xl.bin): for 6 MB of app space
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
STRIP ?= arm-none-eabi-strip --strip-unneeded
XL ?=
ifeq ($(XL),1)
DATA = src/data_xl.bin
BUILD_DIR = output/xl
XL_FLAGS = -DHK_XL=1
else
DATA = src/data.bin
BUILD_DIR = output
endif
GAME = src/res.c src/tex.c src/room.c src/gfx.c src/phys.c src/anim.c src/hero.c src/camera.c src/game.c src/world.c src/obj.c src/enemy.c src/hud.c src/spell.c src/text.c src/npc.c src/title.c src/vm.c src/msg.c src/save.c src/death.c src/menu.c src/inv.c src/shop.c src/collect.c src/stag.c src/map.c src/lzma/LzmaDec.c
SRC = $(GAME) src/plat_eadk.c src/main.c
HEADERS = $(wildcard src/*.h) src/enemy_xl.c ../common/epsilon_app.h ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1 -DDATA_BIN='"$(DATA)"' $(XL_FLAGS)
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host data test
build: $(BUILD_DIR)/hollowknight.nwa

# the RAM the calculator gives apps (data + bss) depends on its software: 153676 bytes on 25.2, 149500 on 24.10,
# 148928 on 23.2; checked against the least
check: $(BUILD_DIR)/hollowknight.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 148928 $< $(BUILD_DIR)/hollowknight.bin
	@echo "BIN     $(BUILD_DIR)/hollowknight.bin: $$(wc -c < $(BUILD_DIR)/hollowknight.bin) bytes"

$(BUILD_DIR)/data.o: src/data.S $(DATA) | $(BUILD_DIR)
	$(Q) $(CC) -c -DDATA_BIN='"$(DATA)"' $< -o $@

$(BUILD_DIR)/hollowknight.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o $(BUILD_DIR)/data.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/data.o $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) arm-none-eabi-size $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isrc -DHOST $(XL_FLAGS)
HOST_OUT ?= build/play$(if $(XL_FLAGS),-xl)
host: $(HOST_OUT)
$(HOST_OUT): $(GAME) src/plat_host.c tests/play.c $(HEADERS)
	$(Q) mkdir -p $(dir $@) build/host-saves
	$(Q) cc $(HOST_FLAGS) -fsanitize=address,undefined $(GAME) src/plat_host.c tests/play.c -lm -o $@
	@echo "HOST    $@"

test: $(HOST_OUT)
	python3 tests/check.py $(HOST_OUT) $(DATA)

data:
	cd tools && HK_XL=$(XL) python3 pack.py

clean:
	$(Q) rm -rf $(BUILD_DIR) build
