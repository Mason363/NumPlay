# Doom on the NumWorks calculator: id Software's game, the shareware episode, through RP2040 Doom's engine.
#   make            output/doom.nwa
#   make check      link it like the calculator does (output/doom.bin)
#   make host       build/play: the game on a computer, for tests and screenshots
#   make test       the game played on a computer (tests/check.py, with the sanitizers)
#   make media      the pictures of docs/ and the animations of the repository's docs/media (tools/media.py)
#   make data       src/doom1.whx and src/doom1.mip from the shareware DOOM1.WAD (tools/fetch.py downloads it;
#                   WAD=its path)
#   make WAD=path/to/doom2.wad [PWAD="maps.wad ..."] [MAPS="MAP01-MAP04"]
#                   output/doom.nwa from another WAD (the README's "Doom from your own WAD"); host, test and check too
Q ?= @
# the ARM toolchain on the PATH, or in TOOLCHAIN=<its bin folder>
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
CXX = $(BIN)arm-none-eabi-g++
NWLINK ?= npx --yes -- nwlink@1.0.0
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
SIZE = $(BIN)arm-none-eabi-size
BUILD_DIR = output
WAD ?=

# the packed WAD: the shareware one, or one made from WAD= (below); and its flats made smaller (tools/mips.py)
WHX = src/doom1.whx
MIP = src/doom1.mip
ifneq ($(WAD),)
ifeq ($(filter data,$(MAKECMDGOALS)),)
WHX = build/wad/doom.whx
MIP = build/wad/doom.mip
endif
endif

# the engine (Chocolate Doom by way of RP2040 Doom), and the calculator's side of it (src/nw)
ENGINE = $(wildcard src/*.c) $(wildcard src/doom/*.c)
NW = src/nw/i_system.c src/nw/i_video.c src/nw/i_input.c src/nw/i_sound.c src/nw/saves.c src/nw/main.c src/nw/scan.c
HEADERS = $(wildcard src/*.h src/doom/*.h src/nw/*.h src/nw/*.inc src/compat/*.h src/compat/*/*.h) ../common/epsilon_app.h \
  ../common/epsilon_files.h
INC = -include src/nw/doomconf.h -Isrc -Isrc/doom -Isrc/compat -Isrc/nw
# (the engine's own code has warnings of its own, left as they are; the calculator's side has none)
ENGINE_W = -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-sign-compare \
  -Wno-missing-field-initializers -Wno-unused-parameter -Wno-implicit-fallthrough -Wno-format-truncation \
  -Wno-maybe-uninitialized -Wno-type-limits -Wno-missing-braces -Wno-misleading-indentation \
  -Wno-cast-function-type -Wno-pointer-sign -Wno-char-subscripts -Wno-parentheses -Wno-address -Wno-unused-value \
  -Wno-int-in-bool-context -Wno-array-bounds -Wno-stringop-overflow -Wno-absolute-value -Wno-dangling-pointer

ARCH = $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1
COMMON = $(ARCH) $(INC) -Os -fno-math-errno -fno-unwind-tables -fno-asynchronous-unwind-tables -fsingle-precision-constant -fno-tree-loop-distribute-patterns \
  -ffunction-sections -fdata-sections -fwrapv -flto -fno-fat-lto-objects -fvisibility=internal -DNDEBUG $(EXTRA)
CFLAGS = -std=gnu11 $(COMMON)
CXXFLAGS = -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics $(COMMON)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel $(if $(MAP),-Wl$(comma)-Map$(comma)$(MAP))
comma = ,

OBJ = $(patsubst src/%.c,$(BUILD_DIR)/obj/%.o,$(ENGINE) $(NW) src/nw/plat_eadk.c) $(BUILD_DIR)/obj/pd_render.o

.PHONY: build check clean host test data media
build: $(BUILD_DIR)/doom.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/doom.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/doom.bin
	@echo "BIN     $(BUILD_DIR)/doom.bin: $$(wc -c < $(BUILD_DIR)/doom.bin) bytes"
	@test $$(wc -c < $(BUILD_DIR)/doom.bin) -le 2031616 || { echo "too big for the calculator's 2031616 bytes \
	(MAPS= fewer maps)"; exit 1; }

$(BUILD_DIR)/obj/nw/%.o: src/nw/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Wall -Wextra -Wno-unused-parameter -c $< -o $@
$(BUILD_DIR)/obj/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Wall -Wextra $(ENGINE_W) -c $< -o $@
$(BUILD_DIR)/obj/pd_render.o: src/pd_render.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(Q) $(CXX) $(CXXFLAGS) -Wall -Wextra $(ENGINE_W) -c $< -o $@
$(BUILD_DIR)/data.o: src/nw/data.S $(WHX) $(MIP) $(BUILD_DIR)/whx | $(BUILD_DIR)
	$(Q) $(CC) -c -DDOOM_WHX='"$(WHX)"' -DDOOM_MIP='"$(MIP)"' $< -o $@
$(BUILD_DIR)/whx: FORCE | $(BUILD_DIR)
	@echo '$(WHX)' | cmp -s - $@ || echo '$(WHX)' > $@

$(BUILD_DIR)/doom.nwa: $(OBJ) $(BUILD_DIR)/data.o $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $^ -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) $(SIZE) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c plays it with scripted keys and saves screenshots
HOST_FLAGS = -O1 -g -fwrapv -DHOST=1 $(INC) -I../common $(HOST_EXTRA)
HOST_SAN = -fsanitize=address,undefined -fno-sanitize=shift -fno-sanitize-recover=undefined
HOST_OBJ = $(patsubst src/%.c,build/host/%.o,$(ENGINE) $(NW) src/nw/plat_host.c) build/host/pd_render.o \
  build/host/play.o build/host/data.o
host: build/play
build/host/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) cc -std=gnu11 $(HOST_FLAGS) $(HOST_SAN) -w -c $< -o $@
build/host/pd_render.o: src/pd_render.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(Q) c++ -std=gnu++17 -fno-exceptions -fno-rtti $(HOST_FLAGS) $(HOST_SAN) -w -c $< -o $@
build/host/play.o: tests/play.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) cc -std=gnu11 $(HOST_FLAGS) $(HOST_SAN) -Wall -Wextra -c $< -o $@
build/host/data.o: src/nw/data.S $(WHX) $(MIP) build/host/whx
	@mkdir -p $(@D)
	$(Q) cc -c -DDOOM_WHX='"$(WHX)"' -DDOOM_MIP='"$(MIP)"' $< -o $@
build/host/whx: FORCE
	@mkdir -p $(@D)
	@echo '$(WHX)' | cmp -s - $@ || echo '$(WHX)' > $@
build/play: $(HOST_OBJ)
	$(Q) c++ $(HOST_SAN) $^ -lm -o $@
	@echo "HOST    $@"

test: build/play
	python3 tests/check.py build/play $(if $(filter build/wad/%,$(WHX)),--wad)

media: build/play
	python3 tools/media.py build/play

# the packed WAD: whd_gen (RP2040 Doom's converter) built on the computer, without the sounds and music
WHD_GEN = build/whd_gen
WHD_GEN_C = tools/whd_gen/musx_decoder.c tools/whd_gen/adpcm-lib.c src/tiny_huff.c src/image_decoder.c
WHD_GEN_OBJ = $(patsubst %.cpp,build/whd_gen_obj/%.o,$(notdir $(wildcard tools/whd_gen/*.cpp))) \
  $(patsubst %.c,build/whd_gen_obj/%.o,$(notdir $(WHD_GEN_C)))
WHD_GEN_FLAGS = -O2 -DIS_WHD_GEN=1 -Itools/whd_gen -Isrc -Isrc/doom -w
build/whd_gen_obj/%.o: tools/whd_gen/%.cpp $(wildcard tools/whd_gen/*.h) src/whddata.h
	@mkdir -p $(@D)
	$(Q) c++ -std=gnu++17 $(WHD_GEN_FLAGS) -c $< -o $@
build/whd_gen_obj/%.o: tools/whd_gen/%.c
	@mkdir -p $(@D)
	$(Q) cc $(WHD_GEN_FLAGS) -c $< -o $@
build/whd_gen_obj/%.o: src/%.c
	@mkdir -p $(@D)
	$(Q) cc $(WHD_GEN_FLAGS) -c $< -o $@
$(WHD_GEN): $(WHD_GEN_OBJ)
	$(Q) c++ $^ -o $@
data: $(WHD_GEN)
	@test -n "$(WAD)" || { echo "make data WAD=<the shareware DOOM1.WAD> (python3 tools/fetch.py downloads it)"; exit 1; }
	$(WHD_GEN) -silent -lossless $(WAD) src/doom1.whx
	python3 tools/mips.py $(WAD) src/doom1.mip

# another WAD: its chosen maps and what they show (tools/pick.py), packed as the calculator reads it
PICKED = build/wad/$(shell echo '$(notdir $(WAD))' | tr A-Z a-z)
build/wad/args: FORCE
	@mkdir -p $(@D)
	@echo '$(abspath $(WAD) $(PWAD)) $(MAPS)' | cmp -s - $@ || echo '$(abspath $(WAD) $(PWAD)) $(MAPS)' > $@
build/wad/doom.whx: build/wad/args $(WHD_GEN) tools/pick.py $(WAD) $(PWAD)
	python3 tools/pick.py $(WAD) $(PWAD) --maps "$(MAPS)" --out build/wad
	$(Q) $(WHD_GEN) -silent -lossless -drop-unused $(PICKED) $@ > build/wad/whd_gen.log || \
	  { tail -3 build/wad/whd_gen.log; rm -f $@; exit 1; }
	@echo "WHX     $@: $$(wc -c < $@) bytes"
build/wad/doom.mip: build/wad/doom.whx tools/mips.py
	python3 tools/mips.py $(PICKED) $@
.PHONY: FORCE

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = doom
PORT_NAME = Doom
PORT_SRC = src/d_event.c src/d_iwad.c src/d_loop.c src/d_mode.c src/deh_str.c src/image_decoder.c src/m_argv.c src/m_bbox.c src/m_cheat.c src/m_config.c src/m_controls.c src/m_fixed.c src/m_misc.c src/memio.c src/tables.c src/tiny_huff.c src/v_diskicon.c src/v_video.c src/w_checksum.c src/w_file.c src/w_file_memory.c src/w_main.c src/w_merge.c src/w_wad.c src/z_zone.c src/doom/am_map.c src/doom/d_items.c src/doom/d_main.c src/doom/d_net.c src/doom/doomdef.c src/doom/doomstat.c src/doom/dstrings.c src/doom/f_finale.c src/doom/f_wipe.c src/doom/g_game.c src/doom/hu_lib.c src/doom/hu_stuff.c src/doom/info.c src/doom/m_menu.c src/doom/m_random.c src/doom/p_ceilng.c src/doom/p_doors.c src/doom/p_enemy.c src/doom/p_floor.c src/doom/p_inter.c src/doom/p_lights.c src/doom/p_map.c src/doom/p_maputl.c src/doom/p_mobj.c src/doom/p_plats.c src/doom/p_pspr.c src/doom/p_saveg.c src/doom/p_setup.c src/doom/p_sight.c src/doom/p_spec.c src/doom/p_switch.c src/doom/p_telept.c src/doom/p_tick.c src/doom/p_user.c src/doom/r_bsp.c src/doom/r_data.c src/doom/r_data_whd.c src/doom/r_draw.c src/doom/r_main.c src/doom/r_plane.c src/doom/r_segs.c src/doom/r_sky.c src/doom/r_things.c src/doom/s_sound.c src/doom/sounds.c src/doom/st_lib.c src/doom/st_stuff.c src/doom/statdump.c src/doom/wi_stuff.c src/nw/i_system.c src/nw/i_video.c src/nw/i_input.c src/nw/i_sound.c src/nw/saves.c src/nw/main.c src/nw/scan.c src/nw/plat_eadk.c
PORT_ASM = src/nw/data.S
PORT_CXX = src/pd_render.cpp
PORT_CXXFLAGS = -std=gnu++17
PORT_FLAGS = -DEPSILON_XL=1 $(INC) -fwrapv -DNDEBUG -DDOOM_WHX='"$(WHX)"' -DDOOM_MIP='"$(MIP)"' -w
include ../../platform/port.mk
