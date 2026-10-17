# Dino Run: stand-alone app; NumPlay module target is ready when there's flash room.
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
OUT = output
SRC = src/main.c
HEADERS = ../common/epsilon_app.h ../common/epsilon_files.h src/dino_sprites.h
FLAGS = -std=c11 $(shell $(NWLINK) eadk-cflags-device) -Os -Wall -Wextra -Wno-unused-parameter \
        -fno-math-errno -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects \
        -fwhole-program -fvisibility=internal
LINK = -Wl,--relocatable -nostartfiles --specs=nano.specs -Wl,-e,main \
       -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level \
       -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean module sim-module host-test
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
	@echo "Dino Run installable size: $$(wc -c < $(OUT)/dinorun.bin) bytes"
clean:
	$(Q) rm -rf $(OUT)

# NumPlay's module linker can use this when the combined app has enough flash space.
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

host-test:
	cc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -DHOST -DPLATFORM_DEVICE=0 \
	  -Itests tests/test_dino.c -o /tmp/dinorun-host-test
	/tmp/dinorun-host-test
