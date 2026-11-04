# Five Nights at Freddy's for the TI-84 Plus CE (the CE C toolchain, v15: its bin folder on the PATH).
#   make         bin/FNAF.8xp and the data, bin/FNAFD0.8xv, FNAFD1.8xv... (from data/, made by tools/pack.py)
#   make -f host.mk test     the same on a computer, tested
NAME = FNAF
ICON = icon.png
DESCRIPTION = "Five Nights at Freddy's"
COMPRESSED = YES
ARCHIVED = YES
CFLAGS = -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Oz
SRCDIR = src
EXTRA_C_SOURCES = engine/pic.c engine/gfx.c engine/ce_calc.c

all: target appvars
include $(shell cedev-config --makefile)

APPVARS = $(patsubst data/%.bin,$(BINDIR)/%.8xv,$(wildcard data/FNAFD*.bin))
.PHONY: all appvars
appvars: $(APPVARS)
$(BINDIR)/%.8xv: data/%.bin
	$(Q)$(call MKDIR,$(@D))
	$(Q)$(CONVBIN) -r -j bin -k 8xv -n $* -i $< -o $@
