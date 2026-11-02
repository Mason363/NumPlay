# NumPico, PICO-8 carts on the NumWorks calculator: an app of its own.
#   make          output/numpico.nwa with the carts in CARTS (none: it says how to add some), and
#                 output/module.nwa, the same without carts, for NumPlay Studio's kit
#   make CARTS="a.p8 b.p8.png"   with those carts in it (or CARTS_LIST=file: one a line)
#   make check    link it like the calculator does (output/numpico.bin), within the RAM and the app space
#   make test     the carts table, then carts played on this computer with random keys, with the sanitizers
#   make perf     carts in tools/emu.py at an N0110's speed: frames a second and Lua's memory
Q ?= @
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
STRIP ?= $(BIN)arm-none-eabi-strip --strip-unneeded
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
L = ../../launcher/src
PYTHON ?= python3
CARTS ?=
# carts to try (not in NumPlay): PICO-8's BBS carts and femto8's, in gamesrc
TEST_CARTS ?= $(HOME)/Documents/Assets/gamesrc/numpico/carts

LUA = lapi lauxlib lbaselib lcode lcorolib lctype ldebug ldo lfunc lgc linit llex lmem lobject lopcodes \
  lparser lpico8lib lstate lstring lstrlib ltable ltablib ltm lvm lzio
CORE_SRC = $(foreach f,$(LUA),src/femto8/lua/$(f).c) src/femto8/p8_lua.c src/pico.c src/heap.c
APP_SRC = src/main.c src/carts.c src/plat_eadk.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
HEADERS = $(wildcard src/*.h src/femto8/*.h src/femto8/lua/*.h) $(L)/gfx.h $(L)/font.h $(L)/np.h \
  ../common/epsilon_app.h ../common/epsilon_files.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1 -DNDEBUG -I$(L) -Isrc \
  -Isrc/femto8 -Isrc/femto8/lua
CFLAGS += -Wall -Wno-unused-parameter -Wno-unused-function -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
# Lua and the drawing at -O2, the screens at -Os
CORE_OPT = -O2
APP_OPT = -Os
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel $(LDEXTRA)

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones), and the
# calculator's app space is 2031616 bytes
RAM = 133652
APP_SPACE = 2031616

.PHONY: build check clean test perf FORCE
build: $(BUILD_DIR)/numpico.nwa $(BUILD_DIR)/module.nwa

check: $(BUILD_DIR)/numpico.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM) $< $(BUILD_DIR)/numpico.bin
	$(Q) n=$$(wc -c < $(BUILD_DIR)/numpico.bin); [ $$n -le $(APP_SPACE) ] || \
	  { echo "numpico.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }; \
	  echo "BIN     $(BUILD_DIR)/numpico.bin: $$n bytes, RAM within $(RAM) bytes"

OBJ = $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(notdir $(CORE_SRC) $(APP_SRC)))
vpath %.c src src/femto8 src/femto8/lua $(L)
$(BUILD_DIR)/obj/%.o: %.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) $(if $(filter $<,$(CORE_SRC)),$(CORE_OPT),$(APP_OPT)) -c $< -o $@

# the carts: tools/p8cart.py makes the table (it leaves the file alone when it would be the same)
$(BUILD_DIR)/carts.o: FORCE | $(BUILD_DIR)
	$(Q) $(PYTHON) -I tools/p8cart.py -o $@ $(CARTS) $(if $(CARTS_LIST),--list $(CARTS_LIST))

$(BUILD_DIR)/numpico-full.nwa: $(OBJ) $(BUILD_DIR)/icon.o $(BUILD_DIR)/carts.o src/carts.ld
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(APP_OPT) $(LDFLAGS) -Wl,-T,src/carts.ld $(OBJ) $(BUILD_DIR)/icon.o $(BUILD_DIR)/carts.o -lm -o $@
# (stripped: what goes on the calculator; numpico-full.nwa keeps its symbols, for tools/emu.py --profile)
$(BUILD_DIR)/numpico.nwa: $(BUILD_DIR)/numpico-full.nwa
	$(Q) $(STRIP) -o $@ $<
# NumPlay Studio's kit: no carts, np_carts and the block's end (np_numpico_end) left to NumPlay's link
$(BUILD_DIR)/module.nwa: $(OBJ) $(BUILD_DIR)/icon.o
	$(Q) $(CC) $(CFLAGS) $(APP_OPT) $(LDFLAGS) $(OBJ) $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# ------------------------------------------------------------------ on this computer
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wno-unused-parameter -Wno-unused-function -Wno-deprecated-declarations -DHOST \
  -I$(L) -Isrc -Isrc/femto8 -Isrc/femto8/lua -fsanitize=address,undefined -fno-sanitize=shift-base \
  -fno-sanitize-recover=undefined
HOST_SRC = tests/host.c src/main.c src/carts.c $(CORE_SRC) $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c
build/host: $(HOST_SRC) $(HEADERS)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) $(HOST_SRC) -lm -o $@
	@echo "HOST    $@"
test: build/host
	$(Q) $(PYTHON) -I tests/test_p8cart.py
	$(Q) $(PYTHON) tests/test_carts.py build/host $(TEST_CARTS)

# the carts in TEST_CARTS on the emulator, one at a time (PERF_CART): frames a second, Lua's memory at its most
$(BUILD_DIR)/perf-carts.o: FORCE | $(BUILD_DIR)
	$(Q) $(PYTHON) -I tools/p8cart.py -o $@ $(PERF_CART)
$(BUILD_DIR)/perf.nwa: $(OBJ) $(BUILD_DIR)/icon.o $(BUILD_DIR)/perf-carts.o src/carts.ld
	$(Q) $(CC) $(CFLAGS) $(APP_OPT) $(LDFLAGS) -Wl,-T,src/carts.ld $(OBJ) $(BUILD_DIR)/icon.o $(BUILD_DIR)/perf-carts.o -lm -o $@
perf: $(OBJ)
	$(Q) NWLINK="$(NWLINK)" $(PYTHON) tests/perf.py $(TEST_CARTS)
