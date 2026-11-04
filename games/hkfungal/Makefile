# Hollow Knight: Fungal Wastes, the second part of Hollow Knight, sharing its saves. Its code and data are
# Hollow Knight's (games/hollowknight, PART=2); this builds it.
#   make            output/hkfungal.nwa
#   make check      link it like the calculator does (output/hkfungal.bin)
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output

.PHONY: build check clean
build check:
	$(MAKE) -C ../hollowknight $@ PART=2 NWLINK="$(NWLINK)" BUILD_DIR=$(abspath $(BUILD_DIR))

clean:
	rm -rf $(BUILD_DIR)

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce; its files are Hollow Knight's
HK = ../hollowknight/src
PORT_ID = hkfungal
PORT_NAME = Hollow Knight: Fungal Wastes
PORT_SRC = $(addprefix $(HK)/,res.c tex.c room.c gfx.c phys.c anim.c hero.c camera.c game.c world.c obj.c enemy.c hud.c spell.c text.c npc.c title.c vm.c msg.c save.c death.c menu.c inv.c shop.c collect.c stag.c map.c lzma/LzmaDec.c plat_eadk.c main.c)
PORT_ASM = $(HK)/data.S
PORT_FLAGS = -DDATA_BIN='"$(HK)/data_fw.bin"' -DEPSILON_XL=1 -DHK_FW=1 -DNP_FILES_ID='"hollowknight"'
PORT_ICON = $(HK)/icon_fw.png
include ../../platform/port.mk
