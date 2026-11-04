# NumBlocks on the TI-84 Plus CE (CE C toolchain v15): `make` builds bin/NUMBLOCK.8xp, bin/NBCEDAT.8xv (its
# pictures) and bin/NBCETEX.8xv (its textures). On a computer: host/Makefile. The pictures and tables: `python3 tools/pack.py CLIENT_JAR`.
NAME = NUMBLOCK
ICON = icon.png
DESCRIPTION = "NumBlocks"
COMPRESSED = YES
ARCHIVED = YES
CFLAGS = -Wall -Wextra -Wno-unused-parameter -Wno-missing-braces -Oz
HAS_PRINTF = NO
# (the program's name whatever TARGET make port was given)
override TARGET := $(NAME).8xp

include $(shell cedev-config --makefile)

target: bin/NBCEDAT.8xv bin/NBCETEX.8xv
bin/%.8xv: gfx/%.bin
	@mkdir -p bin
	convbin -j bin -k 8xv -r -n $* -i $< -o $@
# the textures' appvar carries the busiest loops too (flash/rsf.s), run from the archive
obj/rsf.bin: flash/rsf.s
	@mkdir -p obj
	$(AS) -march=ez80+full $< -o obj/rsf.o
	$(OBJCOPY) -O binary -j .text obj/rsf.o $@
bin/NBCETEX.8xv: gfx/NBCETEX.bin obj/rsf.bin
	@mkdir -p bin
	cat $^ > obj/NBCETEX.bin
	convbin -j bin -k 8xv -r -n NBCETEX -i obj/NBCETEX.bin -o $@
