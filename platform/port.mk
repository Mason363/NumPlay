# A game on NumPlay's other targets. A game's Makefile sets, then includes this file:
#   PORT_ID     the game's id (games.json), also its files' name
#   PORT_NAME   its title
#   PORT_SRC    its C files (with its plat_eadk.c: EADK is what every target implements)
#   PORT_ASM    its .S files (data.S...)
#   PORT_FLAGS  its own compiler flags (-D..., -I...)
# then `make port TARGET=sdl` builds build/sdl/<id>. TARGET: sdl (a computer, a Raspberry Pi).
TARGET ?= sdl
NP_PLATFORM := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
NP_ROOT := $(NP_PLATFORM)/..
PORT_OUT = build/$(TARGET)
PORT_COMMON = $(NP_PLATFORM)/common/eadk_text.c $(NP_ROOT)/launcher/src/font_data.c
PORT_CFLAGS = -std=gnu11 -O2 -g -Wall -Wno-unused-parameter -Wno-unused-function -DNP_PORT=1 \
  -DNP_APP_ID='"$(PORT_ID)"' -DNP_APP_NAME='"$(PORT_NAME)"' -I$(NP_PLATFORM)/include $(PORT_FLAGS)

.PHONY: port
ifeq ($(TARGET),sdl)
SDL_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl3 2>/dev/null)
port: $(PORT_OUT)/$(PORT_ID)
$(PORT_OUT)/$(PORT_ID): $(PORT_SRC) $(PORT_ASM) $(wildcard $(NP_PLATFORM)/include/*.h) $(NP_PLATFORM)/sdl/np_sdl.c
	@mkdir -p $(@D)
	$(Q) cc $(PORT_CFLAGS) $(SDL_CFLAGS) $(PORT_SRC) $(PORT_ASM) $(NP_PLATFORM)/sdl/np_sdl.c $(PORT_COMMON) \
	  $(SDL_LIBS) -lm -o $@
	@echo "SDL     $@"
else
port:
	@echo "TARGET=$(TARGET): not a target yet (platform/README.md lists them)"; exit 1
endif
