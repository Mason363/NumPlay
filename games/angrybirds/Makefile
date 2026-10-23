# AngryBirds: native standalone arcade adaptation by Bobydeluxe.
.DEFAULT_GOAL := build
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
PYTHON ?= python3
HOST_CC ?= cc
ARM_LD ?= arm-none-eabi-ld
OBJCOPY ?= arm-none-eabi-objcopy
SRC = src/main.c src/game.c src/physics.c src/inflate.c src/save.c src/render.c src/font.c src/data.c
OBJECTS = $(patsubst src/%.c,output/%.o,$(SRC))
HEADERS = $(wildcard src/*.h) ../common/epsilon_app.h ../common/epsilon_files.h ../../launcher/src/font.h ../../launcher/src/font_data.c
PACKER = tools/compact_nwa.py tools/standalone.ld
ASSET_FILES = $(wildcard assets/*.png assets/*.json assets/*.bin assets/*.zlib)
src/data.c: tools/assets.py $(ASSET_FILES)
	$(PYTHON) tools/assets.py
src/ids.h: src/data.c
CFLAGS = -std=c11 $(shell $(NWLINK) eadk-cflags-device) -Os -Wall -Wextra -Werror -Wno-misleading-indentation
CFLAGS += -DEPSILON_XL=1 -ffunction-sections -fdata-sections -fstack-usage -fno-tree-loop-distribute-patterns
CFLAGS += -flto -fno-fat-lto-objects -fvisibility=internal
LINK = -nostartfiles --specs=nano.specs -Wl,--relocatable,--gc-sections,-e,main -flinker-output=nolto-rel
.PHONY: build check test test-arm test-assets assets clean
build: output/angrybirds.nwa
output:
	mkdir -p $@
output/%.o: src/%.c $(HEADERS) | output
	$(CC) $(CFLAGS) -c $< -o $@
output/icon.o: src/icon.png | output
	$(NWLINK) png-icon-o $< $@
output/icon-compact.o: output/icon.o $(PACKER)
	$(PYTHON) tools/compact_nwa.py icon $< $@ --objcopy $(OBJCOPY)
output/angrybirds-debug.nwa: $(OBJECTS) output/icon-compact.o $(PACKER)
	$(CC) $(CFLAGS) $(LINK) -Wl,-u,eadk_app_name,-u,eadk_app_icon,-u,eadk_api_level $(OBJECTS) output/icon-compact.o -lm -o output/standalone.o
	$(PYTHON) tools/compact_nwa.py app output/standalone.o $@ --ld $(ARM_LD)
output/angrybirds.nwa: output/angrybirds-debug.nwa $(PACKER)
	$(PYTHON) tools/compact_nwa.py release $< $@ --objcopy $(OBJCOPY)
check: build
	$(NWLINK) nwa-bin output/angrybirds.nwa output/angrybirds.bin --ram-length 107674
	$(NWLINK) nwa-bin output/angrybirds-debug.nwa output/angrybirds-debug.bin --ram-length 107674
	$(PYTHON) -c "from pathlib import Path; assert Path('output/angrybirds.bin').read_bytes() == Path('output/angrybirds-debug.bin').read_bytes()"
test: | output
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -DANGRY_TEST -Isrc tests/test.c src/game.c src/save.c src/render.c src/font.c src/physics.c src/inflate.c src/data.c -lm -o output/test
	cd output && ./test
test-arm:
	$(PYTHON) tests/test_arm.py --nwlink "$(NWLINK)"
test-assets:
	$(PYTHON) tests/test_assets.py
assets:
	$(PYTHON) tools/assets.py
clean:
	rm -rf output
