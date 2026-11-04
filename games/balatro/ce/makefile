# Balatro for the TI-84 Plus CE (CE C toolchain v15: cedev-config on the PATH). `make` builds bin/: BALATRO.8xp
# (with BALATRO0... when the program is past 64 KB), the code the loader puts in place (BALCV0, BALCV1, BALCB), the art
# (BALART0...) and tables (BALLUT, BALFIRE, BALBG0, BALBG1). The game's rules and screens are the NumWorks version's
# (../src); src/ is the CE's own: the screen at 8 bits a pixel, the background's colours turning, the keys, the files.
#   make host    the same code on a computer (tools/host.c, SDL 3): bin/host/balatro
NAME = BALATRO
ICON = icon.png
DESCRIPTION = "Balatro"
COMPRESSED = NO
ARCHIVED = YES
LTO = YES
OUTPUT_MAP = YES
GEN = gen
DBL ?= -Xclang -mdouble=64
SHARED = game score jokers cons ev util save ui draw
CEFLAGS = $(DBL) -DNP_PORT=1 -DNP_CE=1 -DNP_APP_ID=\"balatro\" -DNP_APP_NAME=\"Balatro\" \
  -I../src -I../../../platform/include -I$(GEN) -Isrc -Wno-unused-function -Wno-unused-parameter
# (no inlining: 11 KB smaller)
CFLAGS = -Wall -Oz -fno-inline-functions $(XFLAGS) $(CEFLAGS)
EXTRA_C_SOURCES = $(SHARED:%=../src/%.c) $(GEN)/main.c $(GEN)/data.c $(GEN)/ce_art.c
DEPS = $(GEN)/ce_art.h $(GEN)/main.c $(GEN)/data.c
LINKER_SCRIPT = ce.ld
# (the toolchain's makefile builds bin/BALATRO.all: everything linked; the rules below cut it up)
TARGET = $(NAME).all
PY ?= python3

.DEFAULT_GOAL := all
include $(shell cedev-config --makefile)

CODEVARS = BALCV0 BALCV1 BALCP BALCB
ARTVARS = $(shell cat $(GEN)/appvars.txt 2>/dev/null)

.PHONY: all host check
all: $(BINDIR)/$(NAME).8xp $(CODEVARS:%=$(BINDIR)/%.8xv) $(BINDIR)/art.stamp

$(GEN)/main.c: ../src/main.c tools/cekeys.py
	$(Q)mkdir -p $(GEN)
	$(Q)$(PY) tools/cekeys.py $< $@

$(GEN)/data.c: ../src/data.c tools/cedata.py
	$(Q)mkdir -p $(GEN)
	$(Q)$(PY) tools/cedata.py $< $@

$(GEN)/ce_art.h: ../src/assets.c ../src/assets.h ../src/data.c ../tools/rc.py tools/mkart.py tools/artsrc.py \
  tools/palette.py tools/cedata.py
	$(Q)mkdir -p $(GEN)
	$(Q)echo [mkart] $(GEN)
	$(Q)$(PY) tools/mkart.py $(GEN) $(CONVBIN)
$(GEN)/ce_art.c $(GEN)/appvars.txt: $(GEN)/ce_art.h

# where the code goes (tools/place.py), before the link
$(GEN)/place.ld: $(OBJDIR)/$(TARGETTMP) tools/place.py
	$(Q)$(PY) tools/place.py $(BINUTILS_BIN)/z80-none-elf-objdump $< $@
$(BINDIR)/$(TARGETOBJ): $(GEN)/place.ld ce.ld

# the program: everything but the code that goes elsewhere
$(BINDIR)/prgm.elf: $(BINDIR)/$(TARGETOBJ)
	$(Q)$(OBJCOPY) --remove-section=.vram --remove-section=.pshadow --remove-section=.bssx $< $@
$(BINDIR)/$(NAME).8xp: $(BINDIR)/prgm.elf
	$(Q)$(CONVBIN) $(CONVBINFLAGS) -j elf -i $< -o $@

# the code that the loader copies: the screen memory's half (in two appvars), the pixel shadow, after the data
$(BINDIR)/code.stamp: $(BINDIR)/$(TARGETOBJ)
	$(Q)for s in vram pshadow bssx; do $(OBJCOPY) -O binary --only-section=.$$s $< $(BINDIR)/$$s.bin || exit 1; done
	$(Q)$(PY) -c "import sys; d=open('$(BINDIR)/vram.bin','rb').read(); h=(len(d)+1)//2; \
	  open('$(BINDIR)/BALCV0.bin','wb').write(d[:h]); open('$(BINDIR)/BALCV1.bin','wb').write(d[h:])"
	$(Q)cp $(BINDIR)/pshadow.bin $(BINDIR)/BALCP.bin
	$(Q)cp $(BINDIR)/bssx.bin $(BINDIR)/BALCB.bin
	$(Q)for v in $(CODEVARS); do $(CONVBIN) -j bin -i $(BINDIR)/$$v.bin -k 8xv -r -n $$v -o $(BINDIR)/$$v.8xv -l 1 || exit 1; done
	$(Q)touch $@
$(CODEVARS:%=$(BINDIR)/%.8xv): $(BINDIR)/code.stamp

# the art and the tables
$(BINDIR)/art.stamp: $(GEN)/ce_art.h
	$(Q)mkdir -p $(BINDIR)
	$(Q)for v in $(ARTVARS); do $(CONVBIN) -j bin -i $(GEN)/$$v.bin -k 8xv -r -n $$v -o $(BINDIR)/$$v.8xv -l 1 || exit 1; done
	$(Q)touch $@

EXTRA_CLEAN = rm -rf $(GEN)

# the computer build
HOSTCC ?= cc
HOST_SRC = $(wildcard src/*.c) $(EXTRA_C_SOURCES) tools/host.c
host: $(BINDIR)/host/balatro
$(BINDIR)/host/balatro: $(HOST_SRC) $(wildcard src/*.h) $(DEPS) $(BINDIR)/art.stamp
	$(Q)mkdir -p $(@D)
	$(Q)$(HOSTCC) -std=gnu11 -O1 -g -Wall -DNP_HOST=1 $(filter-out -Xclang -mdouble=64,$(CEFLAGS)) \
	  $(shell pkg-config --cflags sdl3 2>/dev/null) $(HOST_SRC) $(shell pkg-config --libs sdl3 2>/dev/null) -lm -o $@
	$(Q)echo [host] $@
