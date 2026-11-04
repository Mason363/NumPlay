# ce_emu: TI-84 Plus CE programs run on CEmu's core (github.com/CE-Programming/CEmu, GPL-3.0) with a small OS of
# its own instead of TI's ROM. `make` fetches CEmu at the commit below into CEMU_SRC (outside the repo), builds its
# core with debug support, the flash image (rom.S, with CEDEV's binutils and runtime) and build/ce_emu.
CEMU_URL = https://github.com/CE-Programming/CEmu.git
CEMU_COMMIT = d7679d04c44741359b1f3a0444ff4eea99c34369
CEMU_SRC ?= $(HOME)/Documents/Assets/toolchains/ce-src/cemu
CEDEV ?= $(HOME)/Documents/Assets/toolchains/CEdev
BINUTILS = $(CEDEV)/binutils/bin
OUT = build
CC ?= cc
CFLAGS ?= -O2
CORE_FLAGS = -std=gnu11 -DDEBUG_SUPPORT -w $(CFLAGS)

CORE_SRC = $(filter-out $(CEMU_SRC)/core/os/os-emscripten.c $(CEMU_SRC)/core/os/os-win32.c, \
  $(wildcard $(CEMU_SRC)/core/*.c $(CEMU_SRC)/core/debug/*.c $(CEMU_SRC)/core/os/*.c $(CEMU_SRC)/core/usb/*.c))
CORE_OBJ = $(patsubst $(CEMU_SRC)/core/%.c,$(OUT)/core/%.o,$(CORE_SRC))

all: $(OUT)/ce_emu $(OUT)/rom.bin $(OUT)/ti84pceg.inc

$(CEMU_SRC)/.ce_emu_$(CEMU_COMMIT):
	@if [ ! -d $(CEMU_SRC)/.git ]; then git clone -q $(CEMU_URL) $(CEMU_SRC); fi
	@git -C $(CEMU_SRC) cat-file -e $(CEMU_COMMIT) 2>/dev/null || git -C $(CEMU_SRC) fetch -q origin
	@git -C $(CEMU_SRC) -c advice.detachedHead=false checkout -q $(CEMU_COMMIT)
	@touch $@

$(OUT)/core/%.o: $(CEMU_SRC)/core/%.c $(CEMU_SRC)/.ce_emu_$(CEMU_COMMIT)
	@mkdir -p $(@D)
	$(CC) $(CORE_FLAGS) -c $< -o $@

$(OUT)/libcemucore.a: $(CORE_OBJ)
	rm -f $@
	ar rcs $@ $(CORE_OBJ)

# the home screen's text is drawn with NumPlay's font
FONT = ../../launcher/src/font_data.c
$(OUT)/ce_emu: ce_emu.c os.c ce_emu.h $(FONT) $(OUT)/libcemucore.a
	$(CC) -std=gnu11 -Wall -Wno-unused-function -DDEBUG_SUPPORT $(CFLAGS) -I$(CEMU_SRC)/core ce_emu.c os.c $(FONT) \
	  $(OUT)/libcemucore.a -lz -lm -o $@

# the OS routines' names, for messages (the toolchain's equates)
$(OUT)/ti84pceg.inc:
	@mkdir -p $(OUT)
	curl -fsSL -o $@ https://raw.githubusercontent.com/CE-Programming/toolchain/v15.0/src/include/ti84pceg.inc

$(OUT)/rom.bin: rom.S rom.ld
	@mkdir -p $(OUT)
	$(BINUTILS)/z80-none-elf-as -march=ez80+full rom.S -o $(OUT)/rom.o
	$(BINUTILS)/z80-none-elf-ld -T rom.ld --gc-sections -e 0 -Map $(OUT)/rom.map $(OUT)/rom.o \
	  $(CEDEV)/lib/crt/libcrt.a $(CEDEV)/lib/libc/libc.a -o $(OUT)/rom.elf
	$(BINUTILS)/z80-none-elf-objcopy -O binary $(OUT)/rom.elf $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
