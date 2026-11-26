# NumView: one file of yours (a picture, a PDF, a video, a book) as an app of its own.
#   make FILE=photo.jpg      output/<its name>.nwa: the file, its name and an icon made from it
#   make FILE=clip.mp4 PACK="--start 50 --length 12"    (tools/mediapack.py's options)
#   make                     output/numview.nwa, with no file (it says so)
#   make check               link it like the calculator does, within the RAM and the app space
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
PYTHON ?= python3
BUILD_DIR = output
L = ../../launcher/src
FILE ?=
PACK ?=
NAME := $(if $(FILE),$(shell $(PYTHON) -c "import sys,re,pathlib; s=re.sub(r'[^A-Za-z0-9]+','',pathlib.Path(sys.argv[1]).stem); print(s or 'file')" "$(FILE)"),numview)

SRC = src/main.c src/jpeg.c src/read_font.c $(L)/gfx.c $(L)/font_data.c $(L)/xfont.c $(L)/inflate.c
HEADERS = $(wildcard src/*.h) $(L)/gfx.h $(L)/font.h $(L)/np.h $(L)/inflate.h ../common/epsilon_app.h ../common/np_lz.h

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device) -DNDEBUG -I$(L) -Isrc
CFLAGS += -O2 -Wall -Wno-unused-parameter -Wno-unused-function -fno-math-errno -fsingle-precision-constant
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

RAM = 133652
APP_SPACE = 2031616

.PHONY: build check clean app FORCE
build: $(BUILD_DIR)/$(NAME).nwa

OBJ = $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(notdir $(SRC)))
vpath %.c src $(L)
$(BUILD_DIR)/obj/%.o: %.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

# the file: its table and the app's name (media.o), and its icon
$(BUILD_DIR)/$(NAME)/media.o: FORCE | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/$(NAME)
	$(Q) $(PYTHON) tools/mediapack.py $(if $(FILE),"$(FILE)",--empty) $(PACK) -o $(BUILD_DIR)/$(NAME)/media.o \
	  --icon $(BUILD_DIR)/$(NAME)/icon.png
$(BUILD_DIR)/$(NAME)/icon.png: $(BUILD_DIR)/$(NAME)/media.o
	@true
$(BUILD_DIR)/$(NAME)/icon.o: $(BUILD_DIR)/$(NAME)/icon.png
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR)/$(NAME).nwa: $(OBJ) $(BUILD_DIR)/$(NAME)/media.o $(BUILD_DIR)/$(NAME)/icon.o src/media.ld
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) -Wl,-T,src/media.ld $(OBJ) $(BUILD_DIR)/$(NAME)/icon.o \
	  $(BUILD_DIR)/$(NAME)/media.o -lm -o $(BUILD_DIR)/$(NAME)-full.nwa
	$(Q) arm-none-eabi-strip --strip-unneeded -o $@ $(BUILD_DIR)/$(NAME)-full.nwa
	@echo "NWA     $@: $$(wc -c < $@ | tr -d ' ') bytes"

# NumPlay Studio's kit: the viewer for each kind of file, pictures and documents (1), video (2), books (4), np_media
# and its end left to Studio's link (ld -r -T src/media.ld numview-<kind>-app.o media.o icon.o)
KIND_pictures = 1
KIND_video = 2
KIND_book = 4
define KIND_RULES
$(BUILD_DIR)/$(1)/%.o: %.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $$(@D)
	$(Q) $(CC) $(CFLAGS) -DNV_KINDS=$(KIND_$(1)) -c $$< -o $$@
$(BUILD_DIR)/numview-$(1)-full.o: $(patsubst %.c,$(BUILD_DIR)/$(1)/%.o,$(notdir $(SRC)))
	@echo "LD      $$@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $$^ -o $$@
$(BUILD_DIR)/numview-$(1)-app.o: $(BUILD_DIR)/numview-$(1)-full.o
	$(Q) arm-none-eabi-strip --strip-unneeded -K np_media -K np_numview_end -o $$@ $$<
endef
$(foreach k,pictures video book,$(eval $(call KIND_RULES,$(k))))
app: $(BUILD_DIR)/numview-pictures-app.o $(BUILD_DIR)/numview-video-app.o $(BUILD_DIR)/numview-book-app.o

check: $(BUILD_DIR)/$(NAME).nwa
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM) $< $(BUILD_DIR)/$(NAME).bin
	$(Q) n=$$(wc -c < $(BUILD_DIR)/$(NAME).bin); [ $$n -le $(APP_SPACE) ] || \
	  { echo "$(NAME).nwa is bigger than the calculator's app space ($(APP_SPACE) bytes): it needs Nwagra"; exit 1; }; \
	  echo "BIN     $(BUILD_DIR)/$(NAME).bin: $$n bytes, RAM within $(RAM) bytes"

$(BUILD_DIR):
	$(Q) mkdir -p $@

clean:
	$(Q) rm -rf output
