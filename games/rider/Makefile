# Rider on the NumWorks calculator, an app of its own (C, and the physics in C++ on the game's own engine:
# LiquidFun 1.1.0, Box2D 2.3.0, src/Box2D).
#   make          output/rider.nwa
#   make check    link it like the calculator does (output/rider.bin), within the RAM and the app space
#   make host     build/play: the game on a computer, for tests and pictures
#   make test     the bike against the game's numbers, every level ridden to its end with the keys, a crash, the
#                 endless run, saves (kept, damaged, from the copy) and random keys, with the sanitizers
#   make perf     every frame drawn in tools/emu.py, at an N0110's speed
#   make data     src/data.h from Rider's files (GAMESRC: the unpacked app, see its spec/)
Q ?= @
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
CXX = $(BIN)arm-none-eabi-g++
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
L = ../../launcher/src
PYTHON ?= python3
GAMESRC ?= $(HOME)/Documents/Assets/gamesrc/rider

SRC = src/main.c src/game.c src/draw.c src/plat_eadk.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
HEADERS = src/rider.h src/data.h src/plat.h src/phys.h $(L)/gfx.h $(L)/font.h $(L)/np.h ../common/epsilon_app.h \
  ../common/epsilon_files.h ../common/np_text.h
BOX2D = $(wildcard src/Box2D/*/*.cpp src/Box2D/*/*/*.cpp)
BOX2D_H = $(wildcard src/Box2D/*/*.h src/Box2D/*/*/*.h)
PHYS = src/phys.cpp $(BOX2D)
# the calculator's memory: Box2D's arena, its tree made once, small chunks and stack
B2 = -DB2_TREE_NODES=320 -DB2_STACK_SIZE=4096 -DB2_CHUNK_SIZE=1024 -DB2_CHUNK_ARRAY_INCREMENT=64
# (a computer's pointers are twice as big: room for that)
DEVICE_ARENA = -DPH_ARENA=102400
HOST_ARENA = -DPH_ARENA=163840 -DPH_DEVICE_ARENA=102400 -DB2_BLOCK_HOOK

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1 -I$(L) -Isrc
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(B2) $(DEVICE_ARENA)
CXXFLAGS = $(filter-out -std=gnu11 -Os,$(CFLAGS)) -std=gnu++11 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics
CXXFLAGS += -fno-use-cxa-atexit -Wno-unused-but-set-variable -Wno-class-memaccess -Wno-unused-variable
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones), and the
# calculator's app space is 2031616 bytes
RAM = 133652
APP_SPACE = 2031616

.PHONY: build check clean host test data perf
build: $(BUILD_DIR)/rider.nwa

check: $(BUILD_DIR)/rider.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM) $< $(BUILD_DIR)/rider.bin
	$(Q) n=$$(wc -c < $(BUILD_DIR)/rider.bin); [ $$n -le $(APP_SPACE) ] || \
	  { echo "rider.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }; \
	  echo "BIN     $(BUILD_DIR)/rider.bin: $$n bytes, RAM within $(RAM) bytes"

$(BUILD_DIR)/obj/%.o: src/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/obj/%.o: $(L)/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/phys/%.o: src/%.cpp src/phys.h $(BOX2D_H) | $(BUILD_DIR)
	@mkdir -p $(@D)
	$(Q) $(CXX) $(CXXFLAGS) -c $< -o $@

OBJ = $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(notdir $(SRC)))
PHYS_OBJ = $(patsubst src/%.cpp,$(BUILD_DIR)/phys/%.o,$(PHYS))
$(BUILD_DIR)/rider.nwa: $(OBJ) $(PHYS_OBJ) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $^ -lm -o $@
	$(Q) $(STRIP) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# every frame drawn, at an N0110's speed in tools/emu.py: the endless run with OK held now and then
$(BUILD_DIR)/perf.nwa: $(SRC) $(HEADERS) $(PHYS_OBJ) $(BUILD_DIR)/icon.o
	$(Q) $(CC) $(CFLAGS) -DPERF $(LDFLAGS) $(SRC) $(PHYS_OBJ) $(BUILD_DIR)/icon.o -lm -o $@
perf: $(BUILD_DIR)/perf.nwa
	$(PYTHON) ../../tools/emu.py $< --nwlink "$(NWLINK)" --ms 12000 --out $(BUILD_DIR) \
	  --keys "500-600:ok,1500-1600:ok,2000-5000:ok,5600-6600:ok,7000-9000:ok,9500-11500:ok"

data:
	$(PYTHON) -I tools/data.py $(GAMESRC)

# on a computer: tests/play.c checks the game and saves pictures
HOST_SAN = -fsanitize=address,undefined -fno-sanitize-recover=undefined
HOST_FLAGS = -std=gnu11 -O1 -g -ffp-contract=off -Wall -Wextra -Wno-unused-parameter -DHOST -I$(L) -Isrc $(B2) $(HOST_ARENA) $(HOST_SAN)
HOST_CXXFLAGS = -std=gnu++11 -O2 -g -ffp-contract=off -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable \
  -Wno-unused-private-field -DHOST -Isrc -fno-exceptions -fno-rtti $(B2) $(HOST_ARENA) $(HOST_SAN)
HOST_PHYS = $(patsubst src/%.cpp,build/host/%.o,$(PHYS))
host: build/play
build/host/%.o: src/%.cpp src/phys.h $(BOX2D_H)
	$(Q) mkdir -p $(@D)
	$(Q) c++ $(HOST_CXXFLAGS) -c $< -o $@
build/play: tests/play.c src/main.c src/game.c src/draw.c $(HEADERS) $(HOST_PHYS)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) -c tests/play.c -o build/play.o
	$(Q) cc $(HOST_FLAGS) -c src/game.c -o build/game.o
	$(Q) cc $(HOST_FLAGS) -c src/draw.c -o build/draw.o
	$(Q) cc $(HOST_FLAGS) -c $(L)/gfx.c -o build/gfx.o
	$(Q) cc $(HOST_FLAGS) -c $(L)/font_data.c -o build/font_data.o
	$(Q) cc $(HOST_FLAGS) -c $(L)/xfont.c -o build/xfont.o
	$(Q) c++ $(HOST_SAN) build/play.o build/game.o build/draw.o build/gfx.o build/font_data.o build/xfont.o \
	  $(HOST_PHYS) -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test

clean:
	$(Q) rm -rf $(BUILD_DIR) build
