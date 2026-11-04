# A game on NumPlay's other targets. A game's Makefile sets, then includes this file:
#   PORT_ID     the game's id (games.json), also its files' name
#   PORT_NAME   its title
#   PORT_SRC    its C files (with its plat_eadk.c: EADK is what every target implements)
#   PORT_ASM    its .S files (data.S...)
#   PORT_FLAGS  its own compiler flags (-D..., -I...)
#   PORT_FONT   its own copy of the launcher's font_data.c, if it has one (not in PORT_SRC then)
#   PORT_CXX    its C++ files (a physics engine...), and PORT_CXXFLAGS their own flags: no exceptions, no RTTI
# then `make port TARGET=sdl` builds build/sdl/<id>. TARGET: sdl (a computer, a Raspberry Pi).
TARGET ?= sdl
NP_PLATFORM := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
NP_ROOT := $(NP_PLATFORM)/..
PORT_OUT = build/$(TARGET)
# the font eadk_display_draw_string draws with: the launcher's (a game with its own copy of it names that instead)
PORT_FONT ?= $(NP_ROOT)/launcher/src/font_data.c
PORT_COMMON = $(NP_PLATFORM)/common/eadk_text.c $(PORT_FONT) \
  $(if $(filter %launcher/src/gfx.c,$(PORT_SRC)),$(NP_PLATFORM)/common/np_alloc.c)
PORT_CFLAGS = -std=gnu11 -O2 -g -Wall -Wno-unused-parameter -Wno-unused-function -DNP_PORT=1 \
  -DNP_APP_ID='"$(PORT_ID)"' -DNP_APP_NAME='"$(PORT_NAME)"' -I$(NP_PLATFORM)/include $(PORT_FLAGS)
# C++ files: each built on its own by the target's C++ compiler (NP_CXX), then linked with the rest
NP_CXXFLAGS = $(filter-out -std=gnu11,$(PORT_CFLAGS)) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics \
  $(PORT_CXXFLAGS)
PORT_CXX_OBJ = $(patsubst %.cpp,$(PORT_OUT)/cxx/%.o,$(PORT_CXX))
$(PORT_OUT)/cxx/%.o: %.cpp
	@mkdir -p $(@D)
	$(Q) $(NP_CXX) $(NP_CXXFLAGS) -c $< -o $@

.PHONY: port
ifeq ($(TARGET),sdl)
SDL_CFLAGS = $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL_LIBS = $(shell pkg-config --libs sdl3 2>/dev/null)
NP_CXX = c++
NP_CXXLIB = $(if $(PORT_CXX),$(if $(filter Darwin,$(shell uname -s)),-lc++,-lstdc++))
port: $(PORT_OUT)/$(PORT_ID)
$(PORT_OUT)/$(PORT_ID): $(PORT_SRC) $(PORT_ASM) $(PORT_CXX_OBJ) $(wildcard $(NP_PLATFORM)/include/*.h) $(NP_PLATFORM)/sdl/np_sdl.c
	@mkdir -p $(@D)
	$(Q) cc $(PORT_CFLAGS) $(SDL_CFLAGS) $(PORT_SRC) $(PORT_ASM) $(NP_PLATFORM)/sdl/np_sdl.c $(PORT_COMMON) \
	  $(PORT_CXX_OBJ) $(SDL_LIBS) $(NP_CXXLIB) -lm -o $@
	@echo "SDL     $@"
else ifeq ($(TARGET),ti84ce)
# the CE C toolchain (CEDEV: its folder) builds a folder of its own: one small file per source, each including the
# game's (by its full path, so the game's own relative #includes still work), and its makefile
CEDEV ?= $(HOME)/Documents/Assets/toolchains/CEdev
CE_NAME = $(shell echo '$(PORT_ID)' | tr a-z A-Z | tr -cd 'A-Z0-9' | cut -c1-8)
CE_DIR = $(PORT_OUT)/$(PORT_ID)
CE_ALL = $(abspath $(PORT_SRC) $(NP_PLATFORM)/ti84ce/np_ce.c $(PORT_COMMON))
port: $(CE_DIR)/bin/$(CE_NAME).8xp
$(CE_DIR)/bin/$(CE_NAME).8xp: $(PORT_SRC) $(wildcard $(NP_PLATFORM)/include/*.h) $(NP_PLATFORM)/ti84ce/np_ce.c
	@rm -rf $(CE_DIR)/src && mkdir -p $(CE_DIR)/src
	@n=0; for f in $(CE_ALL); do n=$$((n+1)); echo "#include \"$$f\"" > $(CE_DIR)/src/f$$n.c; done
	@printf 'NAME = %s\nDESCRIPTION = "%s"\nCOMPRESSED = YES\nARCHIVED = YES\nCFLAGS = -Wall -Oz -Werror=shift-count-overflow -Wno-unused-command-line-argument %s\ninclude $$(shell cedev-config --makefile)\n' \
	  '$(CE_NAME)' '$(PORT_NAME)' '-DNP_PORT=1 -DNP_APP_ID="\"$(PORT_ID)\"" -DNP_APP_NAME="\"$(PORT_NAME)\"" -I$(abspath $(NP_PLATFORM)/include) $(PORT_CE_FLAGS)' \
	  > $(CE_DIR)/makefile
	$(Q) PATH="$(CEDEV)/bin:$$PATH" $(MAKE) -C $(CE_DIR) --no-print-directory TARGET=$(CE_NAME).8xp
	@echo "CE      $@"
else ifeq ($(TARGET),nspire)
# Ndless's SDK (NDLESS_SDK: its ndless-sdk folder, with its toolchain built in toolchain/install)
NDLESS_SDK ?= $(HOME)/Documents/Assets/toolchains/ndless-src/ndless-sdk
NS_PATH = $(NDLESS_SDK)/bin:$(NDLESS_SDK)/toolchain/install/bin
NS_DIR = $(PORT_OUT)/$(PORT_ID)
NS_SRC = $(PORT_SRC) $(NP_PLATFORM)/nspire/np_nspire.c $(PORT_COMMON)
NP_CXX = PATH="$(NS_PATH):$$PATH" nspire-g++ -Os -marm -ffunction-sections -fdata-sections
port: $(PORT_OUT)/$(PORT_ID).tns
$(PORT_OUT)/$(PORT_ID).tns: $(NS_SRC) $(PORT_ASM) $(PORT_CXX_OBJ) $(wildcard $(NP_PLATFORM)/include/*.h)
	@rm -rf $(NS_DIR) && mkdir -p $(NS_DIR)
	$(Q) n=0; for f in $(NS_SRC) $(PORT_ASM); do n=$$((n+1)); \
	  PATH="$(NS_PATH):$$PATH" nspire-gcc $(PORT_CFLAGS) -Os -marm -ffunction-sections -fdata-sections \
	    -c $$f -o $(NS_DIR)/f$$n.o || exit 1; done
	$(Q) PATH="$(NS_PATH):$$PATH" nspire-ld $(NS_DIR)/*.o $(PORT_CXX_OBJ) -Wl,--gc-sections -lm -o $(NS_DIR)/$(PORT_ID).elf
	$(Q) PATH="$(NS_PATH):$$PATH" genzehn --input $(NS_DIR)/$(PORT_ID).elf --output $(NS_DIR)/$(PORT_ID).zehn \
	  --name "$(PORT_NAME)" --240x320-support true --uses-lcd-blit true
	$(Q) PATH="$(NS_PATH):$$PATH" make-prg $(NS_DIR)/$(PORT_ID).zehn $@
	@echo "NSPIRE  $@"
else ifeq ($(TARGET),web)
# Emscripten: the computer build (sdl/np_sdl.c) in a page; Asyncify lets the game's endless loop pause for the page.
# The data files of PORT_ASM come in as C (platform/embed.py: #embed), WebAssembly having no assembler for them.
WEB_DATA = $(if $(PORT_ASM),$(PORT_OUT)/data_embed.c)
NP_CXX = em++ -g0 -O2
port: $(PORT_OUT)/$(PORT_ID).html
$(PORT_OUT)/data_embed.c: $(PORT_ASM) $(NP_PLATFORM)/embed.py
	@mkdir -p $(@D)
	$(Q) python3 $(NP_PLATFORM)/embed.py $@ $(filter -D% -I%,$(PORT_FLAGS)) $(PORT_ASM)
$(PORT_OUT)/$(PORT_ID).html: $(PORT_SRC) $(WEB_DATA) $(PORT_CXX_OBJ) $(wildcard $(NP_PLATFORM)/include/*.h) \
  $(NP_PLATFORM)/sdl/np_sdl.c $(NP_PLATFORM)/sdl/shell.html
	@mkdir -p $(@D)
	$(Q) emcc $(PORT_CFLAGS) -g0 -O2 --use-port=sdl3 -sASYNCIFY -sALLOW_MEMORY_GROWTH -sENVIRONMENT=web \
	  --shell-file $(NP_PLATFORM)/sdl/shell.html \
	  $(PORT_SRC) $(WEB_DATA) $(PORT_CXX_OBJ) $(NP_PLATFORM)/sdl/np_sdl.c $(PORT_COMMON) \
	  $(if $(PORT_CXX),$(NP_PLATFORM)/common/np_cxx_rt.c) -o $@
	@echo "WEB     $@"
else ifeq ($(TARGET),casio-cg)
# the fx-CG50 / Graph 90+E: GCC for sh3eb-elf and libfxcg (FXCG: their folder, see platform/casio-cg/README.md)
FXCG ?= $(HOME)/Documents/Assets/toolchains/casio
CG_PATH = $(FXCG)/sh3eb/bin
CG_DIR = $(PORT_OUT)/$(PORT_ID)
CG_SRC = $(PORT_SRC) $(NP_PLATFORM)/casio-cg/np_casio_cg.c $(PORT_COMMON)
CG_LIBC = $(FXCG)/newlib/sh-elf
CG_FLAGS = $(PORT_CFLAGS) -Os -mb -m4a-nofpu -mhitachi -nostdlib -ffunction-sections -fdata-sections \
  -isystem $(CG_LIBC)/include -idirafter $(FXCG)/libfxcg/include
NP_CXX = PATH="$(CG_PATH):$$PATH" sh3eb-elf-g++ -Os -mb -m4a-nofpu -mhitachi -nostdlib -ffunction-sections \
  -fdata-sections -isystem $(CG_LIBC)/include -idirafter $(FXCG)/libfxcg/include
CG_CXX = $(if $(PORT_CXX),$(NP_PLATFORM)/casio-cg/np_cxx.cpp)
port: $(PORT_OUT)/$(PORT_ID).g3a
$(PORT_OUT)/$(PORT_ID).g3a: $(CG_SRC) $(PORT_ASM) $(PORT_CXX_OBJ) $(wildcard $(NP_PLATFORM)/include/*.h) \
  $(NP_PLATFORM)/casio-cg/addin.ld $(NP_PLATFORM)/casio-cg/g3a.py
	@mkdir -p $(CG_DIR)
	$(Q) n=0; objs=; for f in $(CG_SRC) $(PORT_ASM); do n=$$((n+1)); objs="$$objs $(CG_DIR)/f$$n.o"; \
	  PATH="$(CG_PATH):$$PATH" sh3eb-elf-gcc $(CG_FLAGS) -c $$f -o $(CG_DIR)/f$$n.o || exit 1; \
	  case $$f in *.S) PATH="$(CG_PATH):$$PATH" sh3eb-elf-objcopy --prefix-symbols=_ $(CG_DIR)/f$$n.o;; esac; done; \
	  for f in $(CG_CXX); do n=$$((n+1)); objs="$$objs $(CG_DIR)/f$$n.o"; \
	  $(NP_CXX) $(NP_CXXFLAGS) -c $$f -o $(CG_DIR)/f$$n.o || exit 1; done; \
	  PATH="$(CG_PATH):$$PATH" sh3eb-elf-gcc -mb -m4a-nofpu -mhitachi -nostdlib -T$(NP_PLATFORM)/casio-cg/addin.ld \
	  $$objs $(PORT_CXX_OBJ) -Wl,--gc-sections -Wl,-u,initialize -L$(CG_LIBC)/lib -L$(FXCG)/sdk/lib -lc -lm -lgcc -lfxcg -lc -lgcc \
	  -o $(CG_DIR)/$(PORT_ID).elf
	$(Q) PATH="$(CG_PATH):$$PATH" sh3eb-elf-objcopy -O binary -R .ilram $(CG_DIR)/$(PORT_ID).elf $(CG_DIR)/$(PORT_ID).bin
	$(Q) python3 $(NP_PLATFORM)/casio-cg/g3a.py $(CG_DIR)/$(PORT_ID).bin $@ --name "$(PORT_NAME)" --icon src/icon.png
	@echo "CASIO   $@"
else
port:
	@echo "TARGET=$(TARGET): not a target yet (platform/README.md lists them)"; exit 1
endif
