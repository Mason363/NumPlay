# The calculator's picture decoder checked (src/main.c): make, then tests/check.py runs bin/PICTEST.8xp in ce_emu
NAME = PICTEST
COMPRESSED = YES
ARCHIVED = NO
CFLAGS = -Wall -Oz -DCE_ASM
SRCDIR = src
EXTRA_C_SOURCES = ../../engine/pic.c ../../engine/gfx.c ../../engine/ce_calc.c
EXTRA_ASM_SOURCES = ../../engine/pic_ez80.s ../../engine/draw_ez80.s
include $(shell cedev-config --makefile)
