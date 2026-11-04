# Hill Climb Racing on the NumWorks calculator (C, and the physics in C++ on Box2D 2.2.1: src/box2d).
#   make            output/hillclimb.nwa
#   make check      link it like the calculator does (output/hillclimb.bin)
#   make host       build/play: the game on a computer, for tests and screenshots
#   make test       the game played on a computer (tests/check.py, with the sanitizers)
#   make data       src/data.bin, src/data.h and src/art.h from the public material tools/fetch.py gathers
#                   (REF=its folder, Python + Pillow + NumPy + pngquant)
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
REF ?=

# the drawing and the ground run every frame at -O2; the rest is small at -Os
HOT = src/gfx.c src/stage.c
COLD = $(filter-out $(HOT) src/plat_host.c src/plat_eadk.c,$(wildcard src/*.c))
COLD_OBJ = $(patsubst src/%.c,$(BUILD_DIR)/cold/%.o,$(COLD))
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h
# the physics: the game's vehicles (car.cpp) on Box2D 2.2.1, in C++ (no exceptions, no RTTI, no runtime)
BOX2D = $(wildcard src/box2d/Box2D/*/*.cpp src/box2d/Box2D/*/*/*.cpp)
BOX2D_H = $(wildcard src/box2d/Box2D/*/*.h src/box2d/Box2D/*/*/*.h)
PHYS = src/car.cpp $(BOX2D)
PHYS_OBJ = $(patsubst src/%.cpp,$(BUILD_DIR)/phys/%.o,$(PHYS))

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1 -DDATA_BIN='"src/data.bin"'
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
CXXFLAGS = $(filter-out -std=gnu11,$(CFLAGS)) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics
CXXFLAGS += -fno-use-cxa-atexit -DNDEBUG -Isrc/box2d -Wno-unused-but-set-variable -Wno-class-memaccess
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test data
build: $(BUILD_DIR)/hillclimb.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones); installed on its
# own, the app must fit the calculator's space for apps (0x90200000 to 0x903F0000 in its flash)
RAM_LENGTH = 133652
APP_SPACE = 2031616
check: $(BUILD_DIR)/hillclimb.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM_LENGTH) $< $(BUILD_DIR)/hillclimb.bin
	@n=$$(wc -c < $(BUILD_DIR)/hillclimb.bin); \
	  echo "BIN     $(BUILD_DIR)/hillclimb.bin: $$n bytes, $$(( $(APP_SPACE) - n )) to spare of the app space"; \
	  [ $$n -le $(APP_SPACE) ] || { echo "hillclimb.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }

$(BUILD_DIR)/data.o: src/data.S src/data.bin | $(BUILD_DIR)
	$(Q) $(CC) -c -DDATA_BIN='"src/data.bin"' $< -o $@

$(BUILD_DIR)/cold/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@

$(BUILD_DIR)/phys/%.o: src/%.cpp $(HEADERS) $(BOX2D_H)
	@mkdir -p $(@D)
	$(Q) $(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/hillclimb.nwa: $(HOT) src/plat_eadk.c $(COLD_OBJ) $(PHYS_OBJ) $(HEADERS) $(BUILD_DIR)/icon.o $(BUILD_DIR)/data.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(HOT) src/plat_eadk.c $(COLD_OBJ) $(PHYS_OBJ) $(BUILD_DIR)/data.o $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) $(SIZE) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c plays it with scripted keys and saves screenshots
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isrc -DHOST -DDATA_BIN='"src/data.bin"'
HOST_CXXFLAGS = -std=gnu++11 -O2 -g -Wall -Wno-unused-parameter -Isrc -Isrc/box2d -DHOST -fno-exceptions -fno-rtti
HOST_SAN = -fsanitize=address,undefined -fno-sanitize-recover=undefined
HOST_SRC = $(filter-out src/plat_eadk.c src/main.c,$(wildcard src/*.c))
HOST_PHYS = $(patsubst src/%.cpp,build/host/%.o,$(PHYS))
host: build/play
build/host/%.o: src/%.cpp $(HEADERS) $(BOX2D_H)
	$(Q) mkdir -p $(@D)
	$(Q) c++ $(HOST_CXXFLAGS) $(HOST_SAN) -c $< -o $@
build/play: $(HOST_SRC) tests/play.c src/data.S src/data.bin $(HEADERS) $(HOST_PHYS)
	$(Q) mkdir -p $(dir $@)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) -c $(HOST_SRC) tests/play.c src/data.S
	$(Q) mv *.o build/host/
	$(Q) c++ $(HOST_SAN) $(addprefix build/host/,$(notdir $(HOST_SRC:.c=.o))) build/host/play.o build/host/data.o \
	  $(HOST_PHYS) -lm -o $@
	@echo "HOST    $@"

test: build/play
	python3 tests/check.py build/play

data:
	@test -n "$(REF)" || { echo "make data REF=<the folder tools/fetch.py filled>"; exit 1; }
	python3 -I tools/pack.py $(REF)

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = hillclimb
PORT_NAME = Hill Climb Racing
PORT_SRC = src/game.c src/main.c src/menu.c src/res.c src/run.c src/save.c src/vehicle.c src/gfx.c src/stage.c src/plat_eadk.c
PORT_ASM = src/data.S
PORT_FLAGS = -DEPSILON_XL=1 -DDATA_BIN='"src/data.bin"'
include ../../platform/port.mk
