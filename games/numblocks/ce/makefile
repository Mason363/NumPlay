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

target: $(BINDIR)/NBCEDAT.8xv $(BINDIR)/NBCETEX.8xv
$(BINDIR)/%.8xv: gfx/%.bin
	@mkdir -p $(BINDIR)
	convbin -j bin -k 8xv -r -n $* -i $< -o $@
# the textures' appvar carries a copy of the inner loops too (flash/rsf.s), run from the archive; built after the
# program, against its map, and run only by the program of the same build (sources and flags hashed)
RSF_BUILD := 0x$(shell (echo '$(NAME) $(CFLAGS)'; cat src/*.c src/*.h src/*.s src/*.inc flash/*.s) | shasum | cut -c1-6)
ASFLAGS += --defsym RSF_BUILD=$(RSF_BUILD)
$(BINDIR)/$(NAME).map: $(BINDIR)/$(TARGET)
$(OBJDIR)/rsf_syms.inc: $(BINDIR)/$(NAME).map tools/rsfsyms.py
	@mkdir -p $(OBJDIR)
	python3 tools/rsfsyms.py $< > $@
$(OBJDIR)/rsf.bin: flash/rsf.s src/rsb.inc src/rsb_list.inc $(OBJDIR)/rsf_syms.inc
	$(AS) -march=ez80+full -I$(OBJDIR) --defsym RSF_BUILD=$(RSF_BUILD) $< -o $(OBJDIR)/rsf.o
	$(OBJCOPY) -O binary -j .text $(OBJDIR)/rsf.o $@
$(BINDIR)/NBCETEX.8xv: gfx/NBCETEX.bin $(OBJDIR)/rsf.bin
	@mkdir -p $(BINDIR)
	cat $^ > $(OBJDIR)/NBCETEX.bin
	convbin -j bin -k 8xv -r -n NBCETEX -i $(OBJDIR)/NBCETEX.bin -o $@
