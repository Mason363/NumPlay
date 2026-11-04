# Retro-Go: Game Boy, Game Boy Color, NES, Master System and Game Gear games on the calculator.
#   make          build output/retrogo.nwa (RetroGo.nwa), its ROM table from ROMS (none: empty)
#   make ROMS="a.gb b.nes"   the same with those games in it, to try them (or ROMS_LIST=file:
#                            one a line, for names with spaces)
#   make check    link the app like the calculator does (output/retrogo.bin)
#   make test     the emulators and the ROM table on this computer
#
# The games are in a table in flash, np_roms (section .rodata.np_roms, made by
# ../../tools/romtable.py), that the customizer website fills; the emulators read them in place.
# In NumPlay the app draws with the launcher's strips and fonts and saves through its file
# functions (module.o leaves them to the launcher, and np_roms to NumPlay's own table); on its own
# it brings them along.
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
PY ?= python3
BUILD_DIR = output
ROMS ?=

GB_SRC = $(addprefix src/gb/,cpu.c hw.c mem.c lcdc.c rtc.c lcd.c gb.c)
NES_SRC = $(addprefix src/nes/,nes6502.c nes_ppu.c nes_mmc.c mmclist.c nes_apu.c nes.c) \
  $(sort $(wildcard src/nes/mappers/*.c))
SMS_SRC = src/sms/z80.c src/sms/sms.c src/sms/coleco_bios.c
GW_SRC = $(addprefix src/gw/,gw.c gw_state.c gw_system.c sm510base.c sm510core.c sm510op.c sm511core.c sm500core.c sm500op.c sm5acore.c)
MD_SRC = $(addprefix src/md/,md.c md_z80.c m68kcpu.c gwenesis_bus.c gwenesis_io.c gwenesis_vdp_mem.c gwenesis_vdp_gfx.c)
CORE_SRC = $(GB_SRC) $(NES_SRC) $(SMS_SRC) $(GW_SRC) $(MD_SRC)
APP_SRC = src/main.c src/romtable.c src/pack.c
CORE_H = $(wildcard src/*.h src/gb/*.h src/nes/*.h src/sms/*.h src/gw/*.h src/md/*.h) $(BUILD_DIR)/m68k_tab.h
HEADERS = $(CORE_H) ../common/epsilon_app.h ../common/epsilon_files.h ../common/np_text.h \
  ../../launcher/src/gfx.h ../../launcher/src/np.h ../../launcher/src/font.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -Isrc/nes -I$(BUILD_DIR)
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-implicit-fallthrough -Wno-missing-field-initializers
# (gnuboy and nofrendo as they were written)
CFLAGS += -Wno-old-style-declaration -Wno-misleading-indentation -Wno-unused-but-set-variable -Wno-maybe-uninitialized
CFLAGS += -fno-math-errno -fsingle-precision-constant -fno-tree-loop-distribute-patterns
CFLAGS += -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
# the emulators run at -O2, the screens at -Os
CORE_OPT = -O2
APP_OPT = -Os
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

CORE_O = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(CORE_SRC))
APP_O = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(APP_SRC))
MOD_CORE_O = $(patsubst src/%.c,$(BUILD_DIR)/mod/%.o,$(CORE_SRC))
MOD_APP_O = $(patsubst src/%.c,$(BUILD_DIR)/mod/%.o,$(APP_SRC))

.PHONY: build check clean test module sim-module FORCE
build: $(BUILD_DIR)/retrogo.nwa

check: $(BUILD_DIR)/retrogo.nwa
	$(Q) $(NWLINK) nwa-bin $< $(BUILD_DIR)/retrogo.bin
	@echo "BIN     $(BUILD_DIR)/retrogo.bin: $$(wc -c < $(BUILD_DIR)/retrogo.bin) bytes"

$(BUILD_DIR)/%.o: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) $(if $(filter $<,$(CORE_SRC)),$(CORE_OPT),$(APP_OPT)) -c $< -o $@
$(BUILD_DIR)/mod/%.o: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(Q) $(CC) $(CFLAGS) -DNP_MODULE=1 $(if $(filter $<,$(CORE_SRC)),$(CORE_OPT),$(APP_OPT)) -c $< -o $@
$(BUILD_DIR)/gfx.o: ../../launcher/src/gfx.c $(HEADERS)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@
$(BUILD_DIR)/font_data.o: ../../launcher/src/font_data.c ../../launcher/src/font.h
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@
$(BUILD_DIR)/xfont.o: ../../launcher/src/xfont.c ../../launcher/src/np_xfont.h $(HEADERS)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@

# the 68000's opcode table (gwenesis' Musashi), made from its list of opcodes
$(BUILD_DIR)/m68k_tab.h: tools/m68ktab.py src/md/m68kops.h
	@mkdir -p $(BUILD_DIR)
	$(Q) $(PY) tools/m68ktab.py src/md/m68kops.h $@
# (gwenesis as it was written; its asserts off, as in Retro-Go)
$(patsubst src/%.c,$(BUILD_DIR)/%.o,$(MD_SRC)) $(patsubst src/%.c,$(BUILD_DIR)/mod/%.o,$(MD_SRC)): CFLAGS += -DNDEBUG \
  -Wno-unused-function -Wno-unused-variable -Wno-parentheses -Wno-unused-value -Wno-return-type -Wno-pointer-sign

# the games: tools/romtable.py makes the table, empty or with ROMS. It runs at every build and leaves
# the file as it is when it wouldn't change, so nothing is linked again for nothing.
$(BUILD_DIR)/roms.o: FORCE
	@mkdir -p $(BUILD_DIR)
	$(Q) $(PY) ../../tools/romtable.py -o $@ $(ROMS) $(if $(ROMS_LIST),--list $(ROMS_LIST))

$(BUILD_DIR)/retrogo-full.nwa: $(CORE_O) $(APP_O) $(BUILD_DIR)/gfx.o $(BUILD_DIR)/font_data.o $(BUILD_DIR)/xfont.o \
  $(BUILD_DIR)/icon.o $(BUILD_DIR)/roms.o src/roms.ld
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) -Wl,-T,src/roms.ld $(filter %.o,$^) -o $@
# (the same, stripped: what goes on the calculator; retrogo-full.nwa keeps its symbols, for tools/emu.py --profile)
$(BUILD_DIR)/retrogo.nwa: $(BUILD_DIR)/retrogo-full.nwa
	$(Q) arm-none-eabi-strip --strip-unneeded -o $@ $<

$(BUILD_DIR)/icon.o: src/icon.png
	@mkdir -p $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

clean:
	$(Q) rm -rf $(BUILD_DIR)

# NumPlay launcher: the app as a module (see ../../tools/npmodule.py); np_roms stays undefined,
# NumPlay links its own table.
module: $(BUILD_DIR)/module.o
$(BUILD_DIR)/module.o: $(MOD_CORE_O) $(MOD_APP_O)
	$(Q) $(CC) $(CFLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel $^ -o $@

SIM_CFLAGS = -std=gnu11 -Isrc/nes -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) -DNP_MODULE=1 -w
sim-module: $(BUILD_DIR)/sim-module.o
$(BUILD_DIR)/sim-module.o: $(CORE_SRC) $(APP_SRC) $(HEADERS)
	$(Q) mkdir -p $(BUILD_DIR)/sim
	$(Q) cc $(SIM_CFLAGS) -Dmain=np_retrogo_main -c src/main.c -o $(BUILD_DIR)/sim/main.o
	$(Q) for f in $(CORE_SRC) src/romtable.c src/pack.c; do cc $(SIM_CFLAGS) -c $$f -o $(BUILD_DIR)/sim/$$(echo $$f | tr / _).o || exit 1; done
	$(Q) $(PY) ../../tools/romtable.py --c $(BUILD_DIR)/sim/roms.c $(ROMS) && cc $(SIM_CFLAGS) -c $(BUILD_DIR)/sim/roms.c -o $(BUILD_DIR)/sim/roms.o
	$(Q) ld -r $(BUILD_DIR)/sim/*.o -exported_symbol _np_retrogo_main -o $@

# ------------------------------------------------------------------ tests on this computer
HOST_CC = cc
HOST_CFLAGS = -std=gnu11 -Isrc/nes -I$(BUILD_DIR) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-sanitize=alignment,shift \
  -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-implicit-fallthrough -Wno-missing-field-initializers
TEST_ROMS ?= $(HOME)/Documents/Assets/gamesrc/retrogo/roms
$(BUILD_DIR)/host/core: tests/host_core.c $(CORE_SRC) $(CORE_H)
	@mkdir -p $(BUILD_DIR)/host
	$(Q) $(HOST_CC) $(HOST_CFLAGS) tests/host_core.c $(CORE_SRC) -o $@
$(BUILD_DIR)/host/romtable: tests/host_romtable.c src/romtable.c src/romtable.h
	@mkdir -p $(BUILD_DIR)/host
	$(Q) $(HOST_CC) $(HOST_CFLAGS) tests/host_romtable.c src/romtable.c -o $@
$(BUILD_DIR)/host/pack: tests/host_pack.c src/pack.c src/pack.h
	@mkdir -p $(BUILD_DIR)/host
	$(Q) $(HOST_CC) $(HOST_CFLAGS) tests/host_pack.c src/pack.c -o $@
# (the Z80 at full speed: zexall runs 46.7 billion clocks)
$(BUILD_DIR)/host/z80: tests/host_z80.c src/sms/z80.c src/sms/z80.h
	@mkdir -p $(BUILD_DIR)/host
	$(Q) $(HOST_CC) -std=gnu11 -O2 -Wall -Wextra tests/host_z80.c src/sms/z80.c -o $@
test: $(BUILD_DIR)/host/core $(BUILD_DIR)/host/romtable $(BUILD_DIR)/host/pack $(BUILD_DIR)/host/z80
	$(Q) $(BUILD_DIR)/host/pack
	$(Q) $(PY) tests/test_romtable.py $(BUILD_DIR)/host/romtable
	$(Q) $(PY) tests/test_cores.py $(BUILD_DIR)/host/core $(TEST_ROMS) $(BUILD_DIR)/host/z80
	$(Q) NWLINK="$(NWLINK)" $(PY) tests/test_app.py

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = retrogo
PORT_NAME = Retro-Go
PORT_SRC = src/gb/cpu.c src/gb/hw.c src/gb/mem.c src/gb/lcdc.c src/gb/rtc.c src/gb/lcd.c src/gb/gb.c src/nes/nes6502.c src/nes/nes_ppu.c src/nes/nes_mmc.c src/nes/mmclist.c src/nes/nes_apu.c src/nes/nes.c src/nes/mappers/map000.c src/nes/mappers/map001.c src/nes/mappers/map002.c src/nes/mappers/map003.c src/nes/mappers/map004.c src/nes/mappers/map005.c src/nes/mappers/map007.c src/nes/mappers/map008.c src/nes/mappers/map009.c src/nes/mappers/map011.c src/nes/mappers/map015.c src/nes/mappers/map016.c src/nes/mappers/map018.c src/nes/mappers/map019.c src/nes/mappers/map024.c src/nes/mappers/map032.c src/nes/mappers/map033.c src/nes/mappers/map034.c src/nes/mappers/map040.c src/nes/mappers/map064.c src/nes/mappers/map065.c src/nes/mappers/map066.c src/nes/mappers/map070.c src/nes/mappers/map075.c src/nes/mappers/map078.c src/nes/mappers/map079.c src/nes/mappers/map085.c src/nes/mappers/map094.c src/nes/mappers/map099.c src/nes/mappers/map231.c src/nes/mappers/mapvrc.c src/sms/z80.c src/sms/sms.c src/sms/coleco_bios.c src/main.c src/romtable.c src/pack.c src/roms_port.c ../../launcher/src/gfx.c ../../launcher/src/xfont.c
PORT_FLAGS = -Isrc/nes
include ../../platform/port.mk
