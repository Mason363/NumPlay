# NumPowder: The Powder Toy on the NumWorks calculator.
#   make            output/numpowder.nwa
#   make check      link it like the calculator does (output/numpowder.bin)
#   make host       build/play: the game on a computer, for tests and screenshots
#   make test       the simulation, checked on a computer (with the sanitizers)
#   make gen        src/elements.h, src/elements_data.c and the font from the original's source
#                   (TPT=its folder, Python)
Q ?= @
# the ARM toolchain on the PATH, or in TOOLCHAIN=<its bin folder>
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
SIZE = $(BIN)arm-none-eabi-size
BUILD_DIR = output
TPT ?=

# the simulation and the drawing run every frame at -O2; the rest is small at -Os
HOT = $(filter $(wildcard src/sim*.c src/air.c src/render*.c src/el_*.c),$(wildcard src/*.c))
COLD = $(filter-out $(HOT) src/plat_host.c src/plat_eadk.c,$(wildcard src/*.c))
COLD_OBJ = $(patsubst src/%.c,$(BUILD_DIR)/cold/%.o,$(COLD))
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test gen
build: $(BUILD_DIR)/numpowder.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/numpowder.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 133652 $< $(BUILD_DIR)/numpowder.bin
	@echo "BIN     $(BUILD_DIR)/numpowder.bin: $$(wc -c < $(BUILD_DIR)/numpowder.bin) bytes"

$(BUILD_DIR)/cold/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@

$(BUILD_DIR)/numpowder.nwa: $(HOT) src/plat_eadk.c $(COLD_OBJ) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(HOT) src/plat_eadk.c $(COLD_OBJ) $(BUILD_DIR)/icon.o -lm -o $@
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
HOST_SRC = $(filter-out src/plat_eadk.c,$(wildcard src/*.c))
host: build/play
build/play: $(HOST_SRC) tests/play.c $(HEADERS)
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) $(HOST_SRC) tests/play.c -lm -o $@
	@echo "HOST    $@"

# the simulation without the interface (tests/simtest.c)
TEST_SRC = $(filter-out src/plat_eadk.c src/main.c $(wildcard src/ui*.c),$(wildcard src/*.c))
test: build/simtest
	$(Q) ./build/simtest
build/simtest: $(TEST_SRC) tests/simtest.c $(HEADERS)
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) $(TEST_SRC) tests/simtest.c -lm -o $@

gen:
	@test -n "$(TPT)" || { echo "make gen TPT=<the folder of The Powder Toy's source>"; exit 1; }
	python3 -I tools/gen_elements.py $(TPT)
	python3 -I tools/gen_font.py $(TPT)

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = numpowder
PORT_NAME = NumPowder
PORT_SRC = src/canmove_data.c src/elements_data.c src/font.c src/font_data.c src/gol.c src/gravity.c src/input.c src/main.c src/tools.c src/ui.c src/ui_draw.c src/ui_menu.c src/ui_opts.c src/ui_save.c src/walls.c src/air.c src/el_elec.c src/el_fire.c src/el_force.c src/el_gases.c src/el_liquids.c src/el_misc.c src/el_nuclear.c src/el_pipe.c src/el_powders.c src/el_sensors.c src/el_solids.c src/el_special.c src/el_stkm.c src/render.c src/render_el.c src/render_el2.c src/render_ovl.c src/sim.c src/sim_move.c src/simsave.c src/plat_eadk.c
PORT_FLAGS = -DEPSILON_XL=1
include ../../platform/port.mk
