# Portal Returns for the NumWorks calculator.
#   make          build output/portal.nwa
#   make run      install on a connected calculator
#   make check    link the app like the calculator does (output/portal.bin)
#   make sim      output/portal.nwb for the Epsilon simulator
#   make assets   regenerate src/assets.h from assets/
Q ?= @
CC = arm-none-eabi-gcc
NWLINK = npx --yes -- nwlink@1.0.0
BUILD_DIR = output
SRC = src/main.c src/game.c src/gfx.c src/inflate.c src/libc.c
HDR = src/portal.h src/assets.h src/inflate.h ../common/np_text.h

CFLAGS = -std=c99 $(shell $(NWLINK) eadk-cflags-device)
CFLAGS += -Os -Wall -fno-math-errno -fno-tree-loop-distribute-patterns -fno-reorder-functions -flto-partition=one
CFLAGS += -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build run check sim assets clean
build: $(BUILD_DIR)/portal.nwa

run: $(BUILD_DIR)/portal.nwa
	@echo "INSTALL $<"
	$(Q) $(NWLINK) install-nwa $<

check: $(BUILD_DIR)/portal.nwa
	$(Q) $(NWLINK) nwa-bin $< $(BUILD_DIR)/portal.bin
	@echo "BIN     $(BUILD_DIR)/portal.bin: $$(wc -c < $(BUILD_DIR)/portal.bin) bytes"

$(BUILD_DIR)/portal.nwa: $(SRC) $(HDR) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@
	$(Q) arm-none-eabi-size $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

assets:
	$(Q) python3 tools/gen_assets.py

sim: $(BUILD_DIR)/portal.nwb
$(BUILD_DIR)/portal.nwb: $(SRC) $(HDR) ../../launcher/sim/sim.c | $(BUILD_DIR)
	$(Q) cc -std=c99 -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) -shared -undefined dynamic_lookup \
	  src/main.c src/game.c src/gfx.c src/inflate.c ../../launcher/sim/sim.c -o $@

clean:
	$(Q) rm -rf $(BUILD_DIR)

# NumPlay launcher: the game as a module (see ../../tools/npmodule.py) for the
# calculator, and as a relocatable object for the simulator build.
.PHONY: module sim-module
module: $(BUILD_DIR)/module.o
$(BUILD_DIR)/module.o: $(SRC) $(HDR) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel $(SRC) -o $@

sim-module: $(BUILD_DIR)/sim-module.o
$(BUILD_DIR)/sim-module.o: $(SRC) $(HDR) | $(BUILD_DIR)
	$(Q) mkdir -p $(BUILD_DIR)/sim
	$(Q) for f in main game gfx inflate; do cc -std=c99 -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) \
	  -Dmain=np_portal_main -c src/$$f.c -o $(BUILD_DIR)/sim/$$f.o || exit 1; done
	$(Q) ld -r $(BUILD_DIR)/sim/*.o -exported_symbol _np_portal_main -o $@

# test/host: the chambers on the computer, frame by frame (tools/compare.py)
host: $(BUILD_DIR)/host
$(BUILD_DIR)/host: $(SRC) $(HDR) test/host.c | $(BUILD_DIR)
	$(Q) cc -std=gnu99 -O2 -w -DPORTAL_TEST $(shell $(NWLINK) eadk-cflags-simulator) -Dmain=portal_main -c src/main.c -o $(BUILD_DIR)/host_main.o
	$(Q) cc -std=gnu99 -O2 -w -DPORTAL_TEST $(shell $(NWLINK) eadk-cflags-simulator) test/host.c src/game.c src/gfx.c src/inflate.c $(BUILD_DIR)/host_main.o -o $@

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = portal
PORT_NAME = Portal Returns
PORT_SRC = src/main.c src/game.c src/gfx.c src/inflate.c
include ../../platform/port.mk
