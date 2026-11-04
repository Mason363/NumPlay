# NumBlocks CE on a computer (in games/numblocks/ce/host): `make` builds ../build/nbce, the game headless (pictures
# and scripted keys: see plat_host.c); `make shots` takes the README's pictures.
SRC = $(wildcard ../src/*.c) plat_host.c
HDR = $(wildcard ../src/*.h)
CFLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-braces -DHOST -I../src -DGFX_DIR=\"$(abspath ../gfx)\"

../build/nbce: $(SRC) $(HDR) Makefile ../gfx/NBCEDAT.bin ../gfx/NBCETEX.bin
	@mkdir -p ../build
	cc $(CFLAGS) $(SRC) -o $@

../build/nbce-asan: $(SRC) $(HDR) Makefile
	@mkdir -p ../build
	cc $(CFLAGS) -fsanitize=address,undefined $(SRC) -o $@

.PHONY: asan
asan: ../build/nbce-asan
