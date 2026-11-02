# Universal Paperclips on the NumWorks calculator.
#   make            output/paperclips.nwa, and output/paperclips-fr.nwa in French (lang/fr.txt, tools/french.py)
#   make check      link it like the calculator does (output/paperclips.bin)
#   make host       build/play: the game on a computer, for tests and pictures
#   make test       the rules against the web build's numbers, a bot playing the whole game to its end through the
#                   keys, saves (kept, damaged), random keys, the same game twice to the same hash (with the
#                   sanitizers)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
L = ../../launcher/src

SRC = src/main.c src/sim.c src/projects.c src/num.c src/ui.c src/save.c src/plat_eadk.c src/font_data.c $(L)/gfx.c \
      $(L)/xfont.c
HEADERS = $(wildcard src/*.h) $(L)/gfx.h $(L)/font.h $(L)/np.h ../common/epsilon_app.h ../common/epsilon_files.h

# (no -fsingle-precision-constant: the game's numbers are doubles, as in JavaScript)
CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DPLATFORM_DEVICE=1 -DEPSILON_XL=1 -I$(L) -Isrc
CFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno -ffp-contract=off
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host host-fr test
build: $(BUILD_DIR)/paperclips.nwa $(BUILD_DIR)/paperclips-fr.nwa

# apps get 133652 bytes of RAM on custom builds of Epsilon 25.2 (148928 or more on official ones)
check: $(BUILD_DIR)/paperclips.nwa $(BUILD_DIR)/paperclips-fr.nwa
	$(Q) for a in paperclips paperclips-fr; do $(NWLINK) nwa-bin --ram-length 133652 $(BUILD_DIR)/$$a.nwa $(BUILD_DIR)/$$a.bin && \
	  echo "BIN     $(BUILD_DIR)/$$a.bin: $$(wc -c < $(BUILD_DIR)/$$a.bin) bytes" || exit 1; done

$(BUILD_DIR)/paperclips.nwa: $(SRC) $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@

# in French: the sources translated into $(BUILD_DIR)/fr, with the French font
FR_SRC = $(patsubst src/%,$(BUILD_DIR)/fr/%,$(filter src/%,$(SRC))) $(filter-out src/%,$(SRC))
$(BUILD_DIR)/fr/main.c: $(wildcard src/*) lang/fr.txt tools/french.py
	$(Q) python3 tools/french.py $(BUILD_DIR)/fr
$(BUILD_DIR)/paperclips-fr.nwa: $(BUILD_DIR)/fr/main.c $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) -DPC_FR=1 $(LDFLAGS) $(FR_SRC) $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) arm-none-eabi-strip --strip-unneeded $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

# on a computer: tests/play.c checks the rules, plays the whole game and random keys, and saves pictures
HOST_SRC = tests/play.c src/sim.c src/projects.c src/num.c src/ui.c src/save.c src/font_data.c $(L)/gfx.c $(L)/xfont.c
HOST_FLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Isrc -I$(L) -DHOST
host: build/play
build/play: $(HOST_SRC) src/main.c $(wildcard src/*.h) $(wildcard tests/*.h)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined $(HOST_SRC) -lm -o $@
	@echo "HOST    $@"
test: build/play
	build/play test build

# the French on a computer: build/fr/play (its bot and pictures; its numbers are written the French way)
host-fr: build/fr/play
build/fr/play: $(HOST_SRC) $(wildcard src/*) lang/fr.txt tools/french.py
	$(Q) python3 tools/french.py build/fr/src && mkdir -p build/fr/tests && cp tests/*.c tests/*.h build/fr/tests/ 2>/dev/null; true
	$(Q) cc $(HOST_FLAGS) -DPC_FR=1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	  $(patsubst tests/%,build/fr/tests/%,$(patsubst src/%,build/fr/src/%,$(HOST_SRC))) -lm -o $@
	@echo "HOST    $@"

clean:
	$(Q) rm -rf $(BUILD_DIR) build
