# NumVisuals: moving backgrounds with a clock, stopwatch, timer, counter or text.
#   make          build output/numvisuals.nwa
#   make check    link the app like the calculator does (output/numvisuals.bin)
#   make font     src/font_sdf.h again from Nunito (tools/sdf.py)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
SRC = src/main.c src/core.c src/scenes.c
SIM_O = $(addprefix $(BUILD_DIR)/sim/,$(notdir $(SRC:.c=.o)))
HEADERS = src/vis.h src/font_sdf.h ../common/epsilon_app.h ../common/epsilon_files.h ../common/np_text.h

CFLAGS = -std=c11 $(shell $(NWLINK) eadk-cflags-device)
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean
build: $(BUILD_DIR)/numvisuals.nwa

check: $(BUILD_DIR)/numvisuals.nwa
	$(Q) $(NWLINK) nwa-bin $< $(BUILD_DIR)/numvisuals.bin
	@echo "BIN     $(BUILD_DIR)/numvisuals.bin: $$(wc -c < $(BUILD_DIR)/numvisuals.bin) bytes"

$(BUILD_DIR)/numvisuals.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

clean:
	$(Q) rm -rf $(BUILD_DIR)

# NumPlay launcher: the app as a module (see ../../tools/npmodule.py) for the
# calculator, and as a relocatable object for the simulator build.
.PHONY: module sim-module
module: $(BUILD_DIR)/module.o
$(BUILD_DIR)/module.o: $(SRC) $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel $(SRC) -o $@

sim-module: $(BUILD_DIR)/sim-module.o
$(BUILD_DIR)/sim/%.o: src/%.c $(HEADERS) | $(BUILD_DIR)
	$(Q) mkdir -p $(BUILD_DIR)/sim
	$(Q) cc -std=c11 -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) -Dmain=np_numvisuals_main -c $< -o $@
$(BUILD_DIR)/sim-module.o: $(SIM_O)
	$(Q) ld -r $(SIM_O) -exported_symbol _np_numvisuals_main -o $@

.PHONY: font
font:
	python3 tools/sdf.py src/font_sdf.h

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = numvisuals
PORT_NAME = NumVisuals
PORT_SRC = $(SRC)
include ../../platform/port.mk
