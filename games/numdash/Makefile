ARM_CC ?= arm-none-eabi-gcc
CC = cc
PYTHON ?= python3
NWLINK = node node_modules/nwlink/bin/nwlink
WARN = -Wall -Wextra -Werror -Wno-misleading-indentation
# Like the calculator's build: no fused multiply-add, so replays play out the same.
HOST_FP = -ffp-contract=off
CORE = src/game.c src/trig.c src/level.c src/inflate.c src/leveldata.c src/objdefs.c src/assets.c src/assets_lv.c src/gfx.c src/scene.c src/fx.c src/save.c
COMMON = $(CORE) src/ui.c src/app.c src/menu.c src/play.c src/editor.c
DEVICE_SOURCES = $(COMMON) src/platform_eadk.c src/main.c
DEVICE_OBJECTS = $(patsubst src/%.c,build/arm/%.o,$(DEVICE_SOURCES))
HEADERS = $(wildcard src/*.h) ../common/np_text.h
ARM_FLAGS = -std=c11 -Os -fno-math-errno $(WARN) -ffunction-sections -fdata-sections -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal $(shell $(NWLINK) eadk-cflags-device 2>/dev/null)
ARM_LINK = -Wl,--relocatable -nostartfiles --specs=nano.specs -Wl,--gc-sections -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level -flinker-output=nolto-rel
.PHONY: all build simulator test check clean levels assets run epsilon-app
all: build
build: build/numdash.nwa
node_modules/nwlink/bin/nwlink: package.json package-lock.json
	npm ci
	touch $@
# Regenerates the level data and artwork (needs Python 3 with numpy and Pillow).
levels:
	$(PYTHON) tools/levels.py
assets:
	$(PYTHON) tools/assets.py
	$(PYTHON) tools/assets_lv.py
build/.stamp:
	mkdir -p build
	touch $@
build/icon.o: assets/icon.png node_modules/nwlink/bin/nwlink | build/.stamp
	$(NWLINK) png-icon-o $< $@
build/arm/%.o: src/%.c $(HEADERS) node_modules/nwlink/bin/nwlink | build/.stamp
	mkdir -p build/arm
	$(ARM_CC) $(ARM_FLAGS) -c $< -o $@
build/numdash.nwa: $(DEVICE_OBJECTS) build/icon.o | build/.stamp
	$(ARM_CC) $(ARM_FLAGS) $(ARM_LINK) $(DEVICE_OBJECTS) build/icon.o -lm -o $@
check: build/numdash.nwa
	$(NWLINK) nwa-bin $< build/numdash.bin
simulator: build/numdash-sim
build/numdash-sim: $(COMMON) src/platform_sdl.c src/main.c $(HEADERS) | build/.stamp
	$(CC) -std=c11 -O2 $(HOST_FP) $(WARN) -Isrc $$(sdl2-config --cflags) $(COMMON) src/platform_sdl.c src/main.c $$(sdl2-config --libs) -lm -o $@
build/tests: $(COMMON) tests/test.c $(HEADERS) | build/.stamp
	$(CC) -std=c11 -O1 -g $(HOST_FP) $(WARN) -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc $(COMMON) tests/test.c -lm -o $@
test: build/tests
	./build/tests
	@for i in 1 2 3 4 5 6 7 8 9 10 11 12; do ./build/tests --replay $$((i-1)) tests/replays/level$$i.txt || exit 1; done
	./build/tests --monkey 1 60000
	./build/tests --monkey 2 60000
run: simulator
	./build/numdash-sim
# Native module for the official Epsilon simulator (nwlink >= 0.0.19).
epsilon-app: | build/.stamp
	$(CC) -std=c11 -O2 $(HOST_FP) -Isrc $(shell $(NWLINK) eadk-cflags-simulator) $(COMMON) src/platform_eadk.c src/main.c $(shell $(NWLINK) eadk-ldflags-simulator) -Wl,-undefined,dynamic_lookup -o build/numdash.nwb
clean:
	rm -rf build

# NumPlay launcher: the game as a module (see ../../tools/npmodule.py) for the
# calculator, and as a relocatable object for the simulator build.
.PHONY: module sim-module
module: build/module.o
build/module.o: $(DEVICE_OBJECTS) | build/.stamp
	$(ARM_CC) $(ARM_FLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel \
	  $(DEVICE_OBJECTS) -o $@

sim-module: build/sim-module.o
build/sim-module.o: $(COMMON) src/platform_eadk.c src/main.c $(HEADERS) node_modules/nwlink/bin/nwlink | build/.stamp
	mkdir -p build/sim
	for f in $(COMMON) src/platform_eadk.c src/main.c; do cc -std=c11 -O2 -fPIC -Isrc \
	  $(shell $(NWLINK) eadk-cflags-simulator) -Dmain=np_numdash_main -c $$f -o build/sim/$$(basename $$f .c).o || exit 1; done
	ld -r build/sim/*.o -exported_symbol _np_numdash_main -o $@

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = numdash
PORT_NAME = NumDash
PORT_SRC = src/game.c src/trig.c src/level.c src/inflate.c src/leveldata.c src/objdefs.c src/assets.c src/assets_lv.c src/gfx.c src/scene.c src/fx.c src/save.c src/ui.c src/app.c src/menu.c src/play.c src/editor.c src/platform_eadk.c src/main.c
include ../../platform/port.mk
