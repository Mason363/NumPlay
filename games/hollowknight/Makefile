# Hollow Knight for the NumWorks calculator.
#   make            output/hollowknight.nwa
#   make check      link it like the calculator does (output/hollowknight.bin)
#   make host       build/play: the game on a computer, for tests and screenshots
#   make test       checks of the host build (tests/check.py)
#   make data       regenerate src/data.bin from the game (HOLLOWKNIGHT=<its Data folder>)
#   PART=2          the second app, Hollow Knight: Fungal Wastes (src/data_fw.bin, build/fw/hkfungal.nwa; games/hkfungal
#                   builds it): Hallownest in parts,
#                   each an app of the calculator's usual size, sharing the saves
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
STRIP ?= arm-none-eabi-strip --strip-unneeded
PART ?= 1
ifeq ($(PART),2)
DATA = src/data_fw.bin
BUILD_DIR = build/fw
NAME = hkfungal
ICON = src/icon_fw.png
PART_FLAGS = -DHK_FW=1
else
DATA = src/data.bin
BUILD_DIR = output
NAME = hollowknight
ICON = src/icon.png
endif
GAME = src/res.c src/tex.c src/room.c src/gfx.c src/phys.c src/anim.c src/hero.c src/camera.c src/game.c src/world.c src/obj.c src/enemy.c src/hud.c src/spell.c src/text.c src/npc.c src/title.c src/vm.c src/msg.c src/save.c src/death.c src/menu.c src/inv.c src/shop.c src/collect.c src/stag.c src/map.c src/lzma/LzmaDec.c
SRC = $(GAME) src/plat_eadk.c src/main.c
HEADERS = $(wildcard src/*.h) src/enemy_fw.c ../common/epsilon_app.h ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1 -DDATA_BIN='"$(DATA)"' $(PART_FLAGS)
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host data test
build: $(BUILD_DIR)/$(NAME).nwa

# the RAM the calculator gives apps (data + bss) depends on its software: 153676 bytes on 25.2, 149500 on 24.10,
# 148928 on 23.2; checked against the least
check: $(BUILD_DIR)/$(NAME).nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 148928 $< $(BUILD_DIR)/$(NAME).bin
	@echo "BIN     $(BUILD_DIR)/$(NAME).bin: $$(wc -c < $(BUILD_DIR)/$(NAME).bin) bytes"

$(BUILD_DIR)/data.o: src/data.S $(DATA) | $(BUILD_DIR)
	$(Q) $(CC) -c -DDATA_BIN='"$(DATA)"' $< -o $@

$(BUILD_DIR)/$(NAME).nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o $(BUILD_DIR)/data.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/data.o $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) arm-none-eabi-size $@

$(BUILD_DIR)/icon.o: $(ICON) | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isrc -DHOST $(PART_FLAGS)
HOST_OUT ?= build/play$(if $(PART_FLAGS),-fw)
host: $(HOST_OUT)
$(HOST_OUT): $(GAME) src/plat_host.c tests/play.c $(HEADERS)
	$(Q) mkdir -p $(dir $@) build/host-saves
	$(Q) cc $(HOST_FLAGS) -fsanitize=address,undefined $(GAME) src/plat_host.c tests/play.c -lm -o $@
	@echo "HOST    $@"

test: $(HOST_OUT)
	python3 tests/check.py $(HOST_OUT) $(DATA)

data:
	cd tools && HK_PART=$(PART) python3 pack.py

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = hollowknight
PORT_NAME = Hollow Knight
PORT_SRC = src/res.c src/tex.c src/room.c src/gfx.c src/phys.c src/anim.c src/hero.c src/camera.c src/game.c src/world.c src/obj.c src/enemy.c src/hud.c src/spell.c src/text.c src/npc.c src/title.c src/vm.c src/msg.c src/save.c src/death.c src/menu.c src/inv.c src/shop.c src/collect.c src/stag.c src/map.c src/lzma/LzmaDec.c src/plat_eadk.c src/main.c
PORT_ASM = src/data.S
# (on the Casio, division by a loop instead of libgcc's table: 700 bytes less, to fit the add-in's 2 MB)
PORT_FLAGS = -DDATA_BIN='"src/data.bin"' -DEPSILON_XL=1 $(if $(filter casio-cg,$(TARGET)),-mdiv=call-div1)
include ../../platform/port.mk
