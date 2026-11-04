# Celeste for the TI-84 Plus CE: a CE C toolchain project (CEdev v15).
#   make         bin/CELESTE.8xp and its appvars bin/CEL*.8xv
#   make host    build/host/celeste: the same game on a computer, for the tests and pictures
#   make tests   Madeline against the NumWorks port (tests/physics.py; make host in games/celeste first)
#   make data    data/ and src/gen.h again from the game (CELESTE=<its Content folder>)
all: target appvars

NAME = CELESTE
DESCRIPTION = "Celeste"
COMPRESSED = YES
COMPRESSED_MODE = zx0
ARCHIVED = YES
CFLAGS = -Wall -Wextra -Wno-unused-parameter -Oz $(BENCH)
HAS_PRINTF = NO
override TARGET = $(NAME).8xp   # (not the TARGET=ti84ce `make port` passes down: platform/port.mk)

include $(shell cedev-config --makefile)

APPVARS = $(patsubst data/%.bin,bin/%.8xv,$(wildcard data/*.bin))
appvars: $(APPVARS)
bin/%.8xv: data/%.bin tools/appvar.py
	@mkdir -p bin
	python3 tools/appvar.py bin $<

HOST_SRC = $(filter-out src/plat_ce.c,$(wildcard src/*.c)) host/plat_host.c host/blit.c
HOST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isrc
host: build/host/celeste
build/host/celeste: $(HOST_SRC) $(wildcard src/*.h)
	@mkdir -p $(@D)
	cc $(HOST_FLAGS) -fsanitize=address,undefined $(HOST_SRC) -lz -o $@

tests: build/host/celeste
	python3 tests/physics.py build/host/celeste ../build/play

data:
	python3 tools/pack_ce.py

.PHONY: all appvars host tests data
