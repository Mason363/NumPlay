# The Final Earth 2 on the NumWorks calculator.
#   make            output/finalearth2.nwa
#   make check      link it like the calculator does (output/finalearth2.bin)
#   make host       build/play: the game on a computer, for tests and screenshots
#   make test       the simulation, stories, saves and buildings, checked on a computer (with the sanitizers)
#   make render     build/render: drawing tests, run; images in build/shots
#   make data       src/data.bin and src/data.h from the original's web files (REF=their folder, Python + Pillow)
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
HOT = src/gfx.c src/bg.c
COLD = $(filter-out $(HOT) src/plat_host.c src/plat_eadk.c,$(wildcard src/*.c))
COLD_OBJ = $(patsubst src/%.c,$(BUILD_DIR)/cold/%.o,$(COLD))
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1 -DDATA_BIN='"src/data.bin"'
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test render data
build: $(BUILD_DIR)/finalearth2.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/finalearth2.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/finalearth2.bin
	@echo "BIN     $(BUILD_DIR)/finalearth2.bin: $$(wc -c < $(BUILD_DIR)/finalearth2.bin) bytes"

$(BUILD_DIR)/data.o: src/data.S src/data.bin | $(BUILD_DIR)
	$(Q) $(CC) -c -DDATA_BIN='"src/data.bin"' $< -o $@

$(BUILD_DIR)/cold/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@

$(BUILD_DIR)/finalearth2.nwa: $(HOT) src/plat_eadk.c $(COLD_OBJ) $(HEADERS) $(BUILD_DIR)/icon.o $(BUILD_DIR)/data.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(HOT) src/plat_eadk.c $(COLD_OBJ) $(BUILD_DIR)/data.o $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) $(SIZE) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isrc -DHOST -DDATA_BIN='"src/data.bin"'
HOST_SAN = -fsanitize=address,undefined -fno-sanitize-recover=undefined
HOST_SRC = $(filter-out src/plat_eadk.c,$(wildcard src/*.c))
host: build/play
build/play: $(HOST_SRC) tests/play.c src/data.S src/data.bin $(HEADERS)
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) $(HOST_SRC) tests/play.c src/data.S -lm -o $@
	@echo "HOST    $@"

# drawing tests (tests/render.c): only the renderer and the backdrop
RENDER_SRC = src/gfx.c src/bg.c src/plat_host.c
render: build/render
	$(Q) ./build/render build/shots
build/render: $(RENDER_SRC) tests/render.c src/data.S src/data.bin src/gfx.h src/bg.h src/data.h src/plat.h
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) $(RENDER_SRC) tests/render.c src/data.S -lm -o $@
	@echo "HOST    $@"

# the tests without the interface (tests/noui.c stands in for it); bldtest has its own stand-ins
TEST_SRC = $(filter-out src/plat_eadk.c src/game.c $(wildcard src/ui*.c),$(wildcard src/*.c))
test: build/storytest build/savetest build/simtest build/bldtest
	$(Q) ./build/storytest > /dev/null && ./build/simtest > /dev/null && ./build/savetest build/savetest_out \
	  && ./build/bldtest | tail -1
build/storytest build/savetest build/simtest: build/%: $(TEST_SRC) tests/%.c tests/noui.c src/data.S src/data.bin $(HEADERS)
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) $(TEST_SRC) tests/$*.c tests/noui.c src/data.S -lm -o $@
build/bldtest: $(TEST_SRC) tests/bldtest.c src/data.S src/data.bin $(HEADERS)
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) $(TEST_SRC) tests/bldtest.c src/data.S -lm -o $@

data:
	@test -n "$(REF)" || { echo "make data REF=<the folder of the original's web files>"; exit 1; }
	python3 -I tools/pack.py $(REF)

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = finalearth2
PORT_NAME = The Final Earth 2
PORT_SRC = src/assign.c src/bld.c src/bld_decoration.c src/bld_draw.c src/bld_entertainment.c src/bld_hobbies.c src/bld_houses.c src/bld_production.c src/bld_services.c src/bld_transport.c src/bld_unique.c src/bld_window.c src/bld_world.c src/build.c src/citizen.c src/city.c src/classes.c src/game.c src/input.c src/lang_data.c src/path.c src/save.c src/savecopy.c src/sim.c src/stories_data.c src/story.c src/systems.c src/ui.c src/ui_bwin.c src/ui_city.c src/ui_draw.c src/ui_info.c src/ui_menu.c src/ui_pick.c src/ui_story.c src/ui_win.c src/unlocks.c src/upgrades.c src/upgrades_data.c src/view.c src/gfx.c src/bg.c src/plat_eadk.c
PORT_ASM = src/data.S
PORT_FLAGS = -DEPSILON_XL=1 -DDATA_BIN='"src/data.bin"'
include ../../platform/port.mk
