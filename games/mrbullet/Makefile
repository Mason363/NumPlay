# Mr Bullet on the NumWorks calculator (C, and the physics in C++ on Box2D 2.3.1: src/box2d).
#   make            output/mrbullet.nwa
#   make check      link it like the calculator does (output/mrbullet.bin): it must fit the app space and
#                   133652 bytes of RAM
#   make host       build/play: the game on a computer, for tests and pictures
#   make test       physics checks, the first levels solved, every level loaded and played with random keys, saves
#                   (tests/play.c, with the sanitizers)
#   make data       src/data.bin and src/data.h from the game's own files (REF=the APK's assets folder; Pillow)
Q ?= @
# the ARM toolchain on the PATH, or in TOOLCHAIN=<its bin folder>
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
CXX = $(BIN)arm-none-eabi-g++
NWLINK ?= npx --yes -- nwlink@1.0.0
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
BUILD_DIR = output
L = ../../launcher/src
REF ?=

# drawing runs every frame at -O2; the rest is small at -Os
HOT = src/draw.c
COLD = src/main.c src/ui.c src/game.c src/res.c src/save.c src/inflate.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
COLD_OBJ = $(patsubst %.c,$(BUILD_DIR)/cold/%.o,$(notdir $(COLD)))
HEADERS = $(wildcard src/*.h) $(L)/gfx.h $(L)/font.h $(L)/np.h ../common/epsilon_app.h ../common/epsilon_files.h
BOX2D = $(wildcard src/box2d/Box2D/*/*.cpp src/box2d/Box2D/*/*/*.cpp)
BOX2D_H = $(wildcard src/box2d/Box2D/*/*.h src/box2d/Box2D/*/*/*.h)
PHYS = src/world.cpp $(BOX2D)
PHYS_OBJ = $(patsubst src/%.cpp,$(BUILD_DIR)/phys/%.o,$(PHYS))

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DEPSILON_XL=1 -I$(L) -Isrc
CFLAGS += -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
CXXFLAGS = $(filter-out -std=gnu11,$(CFLAGS)) -std=gnu++11 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics
CXXFLAGS += -fno-use-cxa-atexit -DNDEBUG -Isrc/box2d -Wno-unused-but-set-variable -Wno-class-memaccess -fno-strict-aliasing
CXXFLAGS += -Wno-unused-variable
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host test data
build: $(BUILD_DIR)/mrbullet.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones); installed on its
# own, the app must fit the calculator's space for apps (0x90200000 to 0x903F0000)
RAM_LENGTH = 133652
APP_SPACE = 2031616
check: $(BUILD_DIR)/mrbullet.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM_LENGTH) $< $(BUILD_DIR)/mrbullet.bin
	@n=$$(wc -c < $(BUILD_DIR)/mrbullet.bin); \
	  echo "BIN     $(BUILD_DIR)/mrbullet.bin: $$n bytes, $$(( $(APP_SPACE) - n )) to spare of the app space"; \
	  [ $$n -le $(APP_SPACE) ] || { echo "mrbullet.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }

$(BUILD_DIR)/data.o: src/data.S src/data.bin | $(BUILD_DIR)
	$(Q) $(CC) -c -DDATA_BIN='"src/data.bin"' $< -o $@

$(BUILD_DIR)/cold/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@
$(BUILD_DIR)/cold/%.o: $(L)/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -Os -c $< -o $@

$(BUILD_DIR)/phys/%.o: src/%.cpp $(HEADERS) $(BOX2D_H)
	@mkdir -p $(@D)
	$(Q) $(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/mrbullet.nwa: $(HOT) src/plat_eadk.c $(COLD_OBJ) $(PHYS_OBJ) $(HEADERS) $(BUILD_DIR)/icon.o $(BUILD_DIR)/data.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) -O2 $(LDFLAGS) $(HOT) src/plat_eadk.c $(COLD_OBJ) $(PHYS_OBJ) $(BUILD_DIR)/data.o \
	  $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c checks the physics, solves the first levels, plays every level with random keys, and
# saves pictures
HOST_FLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Isrc -I$(L) -DHOST
HOST_CXXFLAGS = -std=gnu++11 -O2 -g -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable \
  -Wno-unused-private-field -ffp-contract=off -Isrc -Isrc/box2d -DHOST -DNDEBUG -fno-exceptions -fno-rtti
HOST_SAN = -fsanitize=address,undefined -fno-sanitize-recover=undefined
HOST_SRC = tests/play.c src/ui.c src/draw.c src/game.c src/res.c src/save.c src/inflate.c
HOST_LSRC = gfx.c font_data.c xfont.c
HOST_COBJ = $(patsubst %.c,build/host/c/%.o,$(HOST_SRC)) $(patsubst %.c,build/host/l/%.o,$(HOST_LSRC))
HOST_PHYS = $(patsubst src/%.cpp,build/host/%.o,$(PHYS))
host: build/play
build/host/%.o: src/%.cpp $(HEADERS) $(BOX2D_H)
	$(Q) mkdir -p $(@D)
	$(Q) c++ $(HOST_CXXFLAGS) $(HOST_SAN) -c $< -o $@
# (the C files with cc: g++ takes them for C++ even after -x c)
build/host/c/%.o: %.c $(HEADERS)
	$(Q) mkdir -p $(@D)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) -c $< -o $@
build/host/l/%.o: $(L)/%.c $(HEADERS)
	$(Q) mkdir -p $(@D)
	$(Q) cc $(HOST_FLAGS) $(HOST_SAN) -c $< -o $@
build/host/data.o: src/data.S src/data.bin
	$(Q) mkdir -p $(@D)
	$(Q) cc -c -DDATA_BIN='"src/data.bin"' $< -o $@
build/play: $(HOST_COBJ) $(HOST_PHYS) build/host/data.o
	$(Q) c++ $(HOST_SAN) $(HOST_COBJ) $(HOST_PHYS) build/host/data.o -lm -o $@
	@echo "HOST    $@"

test: build/play
	build/play test build

data:
	@test -n "$(REF)" || { echo "make data REF=<the APK's assets folder>"; exit 1; }
	python3 -I tools/pack.py $(REF)

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = mrbullet
PORT_NAME = Mr Bullet
PORT_SRC = src/draw.c src/plat_eadk.c src/main.c src/ui.c src/game.c src/res.c src/save.c src/inflate.c ../../launcher/src/gfx.c ../../launcher/src/xfont.c
PORT_ASM = src/data.S
PORT_CXX = $(PHYS)
PORT_CXXFLAGS = -DNDEBUG -Isrc/box2d -fno-strict-aliasing
PORT_FLAGS = -DEPSILON_XL=1 -I../../launcher/src -Isrc -DDATA_BIN='"src/data.bin"'
include ../../platform/port.mk
