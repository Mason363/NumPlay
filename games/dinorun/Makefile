# Chrome Dino: output/dinorun.nwa on its own, output/module.o for NumPlay (see ../../tools/npmodule.py).
#
#   make build | check | host-test | sprites SHEET=100-offline-sprite.png (Chromium's, see tools/sprites.py)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
OUT = output
SRC = src/main.c
HEADERS = ../common/epsilon_app.h ../common/epsilon_files.h ../common/np_lz.h ../common/np_text.h src/sprites.h
FLAGS = -std=c11 $(shell $(NWLINK) eadk-cflags-device) -Os -Wall -Wextra -Wno-unused-parameter \
        -fno-math-errno -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects \
        -fwhole-program -fvisibility=internal
LINK = -Wl,--relocatable -nostartfiles --specs=nano.specs -Wl,-e,main \
       -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level \
       -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean module sim-module host-test sprites
build: $(OUT)/dinorun.nwa
$(OUT):
	$(Q) mkdir -p $(OUT)
$(OUT)/icon.o: src/icon.png | $(OUT)
	$(Q) $(NWLINK) png-icon-o $< $@
$(OUT)/dinorun.nwa: $(SRC) $(HEADERS) $(OUT)/icon.o
	$(Q) $(CC) $(FLAGS) $(LINK) $(SRC) $(OUT)/icon.o -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@
check: $(OUT)/dinorun.nwa
	$(Q) $(NWLINK) nwa-bin $< $(OUT)/dinorun.bin
	@echo "Chrome Dino installs as $$(wc -c < $(OUT)/dinorun.bin) bytes"
clean:
	$(Q) rm -rf $(OUT)

module: $(OUT)/module.o
$(OUT)/module.o: $(SRC) $(HEADERS) | $(OUT)
	$(Q) $(CC) $(FLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main \
	  -flinker-output=nolto-rel $(SRC) -o $@
sim-module: $(OUT)/sim-module.o
$(OUT)/sim-module.o: $(SRC) $(HEADERS) | $(OUT)
	$(Q) mkdir -p $(OUT)/sim
	$(Q) cc -std=c11 -O2 -fPIC $(shell $(NWLINK) eadk-cflags-simulator) \
	  -Dmain=np_dinorun_main -c $(SRC) -o $(OUT)/sim/main.o
	$(Q) ld -r $(OUT)/sim/main.o -exported_symbol _np_dinorun_main -o $@

host-test: | $(OUT)
	cc -std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer \
	  -DHOST -DPLATFORM_DEVICE=0 -Itests tests/test_dino.c -lm -o $(OUT)/host-test
	$(OUT)/host-test

sprites:
	python3 tools/sprites.py $(SHEET)

# the other targets (platform/port.mk): make port TARGET=sdl, web, nspire, ti84ce
PORT_ID = dinorun
PORT_NAME = Chrome Dino
PORT_SRC = src/main.c
include ../../platform/port.mk
