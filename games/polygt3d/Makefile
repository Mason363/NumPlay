# PolyGT 3D — PlayStation 1 style retro 3D racing simulator
#   make          build output/polygt3d.nwa
#   make check    link the app like the calculator does (output/polygt3d.bin)
#   make test     run self-verification unit and physics tests
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
SRC = src/main.c

CFLAGS = -std=c11 $(shell $(NWLINK) eadk-cflags-device)
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant
CFLAGS += -DPLATFORM_DEVICE=1
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level

.PHONY: build check test clean

build: $(BUILD_DIR)/polygt3d.nwa

check: $(BUILD_DIR)/polygt3d.nwa
	$(Q) $(NWLINK) nwa-bin $< $(BUILD_DIR)/polygt3d.bin
	@echo "BIN     $(BUILD_DIR)/polygt3d.bin: $$(wc -c < $(BUILD_DIR)/polygt3d.bin) bytes"

test:
	$(Q) gcc -O2 -Wall -Wextra tests/verify_sim.c -o $(BUILD_DIR)/verify_sim
	$(Q) ./$(BUILD_DIR)/verify_sim

$(BUILD_DIR)/polygt3d.nwa: $(SRC) $(BUILD_DIR)/icon.o | $(BUILD_DIR)
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -o $@
	$(Q) arm-none-eabi-strip --strip-debug $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

clean:
	$(Q) rm -rf $(BUILD_DIR)
