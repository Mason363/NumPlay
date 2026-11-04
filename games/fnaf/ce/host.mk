# The CE version of Five Nights at Freddy's on a computer, for tests (the calculator's build: makefile).
#   make -f host.mk          build/play (tests/play.c), with the sanitizers
#   make -f host.mk test     tests/check.py
Q ?= @
FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -DHOST \
  -fsanitize=address,undefined -fno-sanitize-recover=undefined
SRC = src/main.c src/scenes.c src/view.c src/save.c src/rules.c engine/pic.c engine/gfx.c engine/ce_host.c
HEADERS = $(wildcard src/*.h engine/*.h ../src/*.h) ../src/game.c ../src/night.c ../src/calls.c

.PHONY: all test test-ce clean
all: build/play build/pics

build/play: $(SRC) tests/play.c $(HEADERS)
	$(Q)mkdir -p build
	$(Q)cc $(FLAGS) $(SRC) tests/play.c -lm -o $@
	@echo "HOST    $@"

build/pics: engine/pic.c engine/gfx.c engine/ce_host.c tests/pics.c $(HEADERS)
	$(Q)mkdir -p build
	$(Q)cc $(FLAGS) engine/pic.c engine/gfx.c engine/ce_host.c tests/pics.c -o $@

test: build/play build/pics
	python3 tests/check.py build/play build/pics

# (with the calculator's decoder in tools/ce_emu too: the CE toolchain on PATH)
test-ce: build/play build/pics
	python3 tests/check.py build/play build/pics --ce

clean:
	$(Q)rm -rf build
