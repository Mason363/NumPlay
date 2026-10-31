# NumDrive: a Drive Mad inspired game for the NumWorks calculator
#   make                      -> output/device/numdrive.nwa
#   make PLATFORM=simulator   -> output/simulator/numdrive.nwb (for the Epsilon simulator)
#   make host test            -> host/nd, a build for this computer, and its checks (host/check.py)
Q ?= @
PLATFORM ?= device
NWLINK ?= npx --yes -- nwlink
BUILD_DIR = output/$(PLATFORM)

SOURCES = main.c world.c vm.c physics.c render.c inflate.c gfx.c ui.c save.c font.c platform_eadk.c
OBJECTS = $(addprefix $(BUILD_DIR)/,$(SOURCES:.c=.o)) $(BUILD_DIR)/pack_data.o

CFLAGS = -std=gnu11 -Wall -Wno-unused-function -fsingle-precision-constant

ifeq ($(PLATFORM),device)
CC = arm-none-eabi-gcc
TARGET = $(BUILD_DIR)/numdrive.nwa
CFLAGS += $(shell $(NWLINK) eadk-cflags-device) -Os
CFLAGS += -flto -flto-partition=one -fno-fat-lto-objects -fwhole-program -fvisibility=internal -fno-math-errno -fno-ident
LDFLAGS = $(shell $(NWLINK) eadk-ldflags-device) --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level -Wl,--gc-sections
LDFLAGS += -flinker-output=nolto-rel
EXTRA = $(BUILD_DIR)/icon.o
else
CC = gcc
TARGET = $(BUILD_DIR)/numdrive.nwb
CFLAGS += $(shell $(NWLINK) eadk-cflags-simulator) -O2 -DNO_STORAGE
LDFLAGS = $(shell $(NWLINK) eadk-ldflags-simulator)
EXTRA =
endif

.PHONY: build clean
build: $(TARGET)

$(TARGET): $(OBJECTS) $(EXTRA)
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $^ $(LDFLAGS) -lm -o $@
ifeq ($(PLATFORM),device)
	$(Q) arm-none-eabi-strip --strip-unneeded -R .comment $@
endif

$(BUILD_DIR)/%.o: src/%.c src/*.h ../common/np_text.h | $(BUILD_DIR)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pack_data.o: src/pack_data.S src/pack.bin | $(BUILD_DIR)
	@echo "AS      $<"
	$(Q) $(CC) $(CFLAGS) -Wa,-Isrc -c $< -o $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

clean:
	$(Q) rm -rf output
	$(Q) $(MAKE) -C host clean

# Host checks (make host test): the game built for a computer drives First Gear, and Mobile Home to its finish
.PHONY: host test
host:
	$(Q) $(MAKE) -C host nd
test: host
	python3 host/check.py host/nd

# NumPlay launcher: the game as a module (see ../../tools/npmodule.py) for the
# calculator, and as a relocatable object for the simulator build.
.PHONY: module sim-module
module: output/device/module.o
output/device/module.o: $(addprefix output/device/,$(SOURCES:.c=.o)) output/device/pack_data.o
	@echo "LD      $@"
	$(Q) arm-none-eabi-gcc $(CFLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel $^ -o $@

sim-module: output/sim-module.o
output/sim-module.o: $(addprefix src/,$(SOURCES)) src/pack.bin src/*.h
	@mkdir -p output/npsim
	$(Q) cd src && xxd -i -n pack_bin pack.bin > ../output/npsim/pack_bin.c
	$(Q) for f in $(SOURCES); do cc -std=gnu11 -O2 -fPIC -fsingle-precision-constant -Wno-unused-function \
	  $(shell $(NWLINK) eadk-cflags-simulator) -DNO_STORAGE -Dpack_data=pack_bin -Dmain=np_numdrive_main \
	  -c src/$$f -o output/npsim/$${f%.c}.o || exit 1; done
	$(Q) cc -fPIC -c output/npsim/pack_bin.c -o output/npsim/pack_bin.o
	$(Q) ld -r output/npsim/*.o -exported_symbol _np_numdrive_main -o $@
