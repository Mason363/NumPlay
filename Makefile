# NumPlay: one launcher app with every game, plus each game on its own.
#
#   make            build/data/NumPlay*.nwa and build/data/apps/*.nwa (python3 tools/build.py also puts the
#                   finished ones in build/compiled: the NumPlays, and each game in build/compiled/games)
#   make check      link the NumPlay apps like the calculator does, print sizes, check RAM
#   make sim        build/data/NumPlay.nwb for the Epsilon simulator
#   make emu        run NumPlay.nwa in the ARM emulator (tools/emu.py)
#   make clean
#
# Needs arm-none-eabi-gcc (with newlib), Node.js for nwlink, Python 3 with
# Pillow, and Rust with the thumbv7em-none-eabihf target for Tetris.

NWLINK ?= npx --yes -- nwlink@1.0.0
ARM_CC = arm-none-eabi-gcc
ARM_LD = arm-none-eabi-ld
ARM_OBJCOPY = arm-none-eabi-objcopy
PY ?= python3
CARGO ?= cargo
B = build/data
# Conventional games honor a configurable output directory; special layouts are kept local.
GAMES_REDIRECT = balatro blockblast breakout buckshot celeste championisland chess connectfour cookieclicker coreball dinorun donkeykong doodlejump doom dune finalearth2 flappybird flappydunk fnaf g2048 hillclimb hollowknight invaders isaac jetpackjoyride leveldevil minesweeper monkeymart numblocks numpowder numvisuals pacman portal pvz snake sokoban solitaire sonic stickmanhook subway sudoku suika tombofthemask towerblock twoplayer undertale uttt
GAMES_OUT = dinorun
GAME_REDIRECT_ARG = $(if $(filter $(1),$(GAMES_OUT)),OUT=../../$(B)/games/$(1),BUILD_DIR=../../$(B)/games/$(1))
# the RAM the oldest calculator software that runs apps gives them (23.2: 148928 bytes; 25.2 gives 153676).
# NumPlay itself takes less: a game goes on past it into the RAM the software has left (see
# tools/gen_games.py), so it installs with less too, custom builds of the software say, where a game that
# needs more says so. The check: every game fits in RAM_LIMIT.
RAM_LIMIT = 148928
# custom builds of Epsilon 25.2's source give apps less: 133652 bytes; every game fits there too, in NumPlay and on
# its own (their big buffers are on the stack, 32 KB apart from that RAM, or in the RAM the software has left)
CUSTOM_RAM = 133652
BIG_APPS = Celeste HollowKnight Undertale ChampionIsland FinalEarth2 NumPowder NumBlocks NumDrive SubwaySurfers FiveNightsAtFreddys Doom BindingOfIsaac HillClimbRacing PlantsVsZombies SonicTheHedgehog CookieClicker LevelDevil JetpackJoyride StickmanHook DonkeyKong Dune MonkeyMart TombOfTheMask AngryBirds
# the calculator's space for installed apps: from the end of its software (64 KiB aligned) to 64 KiB before the
# end of its 4 MiB slot of flash, 0x903F0000. The software's size sets the start: 0x90200000 is the latest a
# NumPlay counts on (1.94 MB, APP_SPACE); Epsilon 26.4 on an N0120 ends at 0x90180000 (2.44 MB, the
# installer's 2496 KB)
APP_START = 0x90200000
APP_START_26 = 0x90180000
APP_END = 0x903F0000
APP_SPACE = 2031616
# with Enlarged Extra Apps (nwagyu's Nwagra), the app loader is told the space goes on to the end of the
# external flash (0x90800000), over the second copy of the calculator's software: a NumPlay of the big
# games (games.json "xl", made with the customizer website from `make kit`) is for that: 6 MB, or 6.5 MB
# from 0x90180000 (the installer's 6656 KB)
XL_END = 0x90800000
XL_APP_SPACE = 6291456
# the spaces the customizer website offers (kit.json "spaces"; tools/kit.py links a NumPlay at each)
SPACES = std:$(APP_START):$(APP_END) e26:$(APP_START_26):$(APP_END) xl:$(APP_START):$(XL_END) xl26:$(APP_START_26):$(XL_END)
# big games that must install on their own without Enlarged Extra Apps: make check holds them to APP_SPACE
APP_SPACE_APPS = HillClimbRacing AngryBirds
# Upsilon, the N0110 and N0115's custom software, gives .nwa apps less RAM: NumPlay-Upsilon.nwa has
# the games that fit it (NumPlay draws their text there: see launcher/src/compat.c)
UPSILON_RAM = 107674
UPSILON_GAMES = crossyroad blockblast suika portal tetris dinorun doodlejump chess flappybird flappydunk towerblock coreball pacman \
  snake connectfour solitaire g2048 minesweeper breakout twoplayer invaders sudoku uttt sokoban

# NumPlay and its discreet versions: the same app, with another name and icon
# on the calculator's home screen. Each one is a release file.
VARIANTS = NumPlay NumPlay-Invisible NumPlay-Matrices
NAME_NumPlay = "NumPlay"
ICON_NumPlay = launcher/assets/icon.png
NAME_NumPlay-Invisible = " "
ICON_NumPlay-Invisible = launcher/assets/icon-invisible.png
NAME_NumPlay-Matrices = "Matrices"
ICON_NumPlay-Matrices = launcher/assets/icon-matrices.png

EADK_CFLAGS := $(shell $(NWLINK) eadk-cflags-device)
EADK_SIM_CFLAGS := $(shell $(NWLINK) eadk-cflags-simulator)

.SECONDEXPANSION:

# Per-game facts (order, screenshots) come from games/games.json.
$(B)/games.mk: games/games.json tools/games_mk.py | $(B)
	$(PY) tools/games_mk.py $< > $@
-include $(B)/games.mk

LAUNCHER_SRC = $(wildcard launcher/src/*.c)
LAUNCHER_H = $(wildcard launcher/src/*.h games/common/*.h)
# the release version, shown in NumPlay's settings (a release is tagged v$(VERSION))
VERSION := $(shell cat VERSION)
ARM_CFLAGS = -std=gnu11 $(EADK_CFLAGS) -DNP_VERSION='"$(VERSION)"' -Os -Wall -Wextra -Wno-unused-parameter -fno-math-errno \
  -fno-tree-loop-distribute-patterns -ffunction-sections -fdata-sections -Ilauncher/src
ARM_LINK = -nostartfiles --specs=nano.specs -Wl,--relocatable -Wl,--gc-sections -Wl,-e,main \
  -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level

.PHONY: all nwa apps check sim emu clean lang kit kit-langs FORCE tetris-app
all: nwa apps
nwa: $(foreach v,$(VARIANTS),$(B)/$(v).nwa) $(B)/NumPlay-Upsilon.nwa

$(B):
	mkdir -p $(B)/modules $(B)/gen $(B)/arm $(B)/apps $(B)/sim

# ------------------------------------------------------------------ modules
# Each game builds itself as a partially linked object (its own Makefile's
# `module` target), then becomes a NumPlay module: one block of flash, its RAM
# moved into the shared arena, `main` renamed np_<game>_main.
MOD_numdash = games/numdash/build/module.o
MOD_crossyroad = games/crossyroad/output/module.o
MOD_numdrive = games/numdrive/output/device/module.o
MOD_balatro = $(B)/games/balatro/module.o
MOD_buckshot = $(B)/games/buckshot/module.o
MOD_portal = $(B)/games/portal/module.o
MOD_chess = $(B)/games/chess/module.o
MOD_tetris = $(B)/tetris/raw.o
ENTRY = main
ENTRY_tetris = np_tetris_main

$(B)/tetris/raw.o: FORCE | $(B)
	mkdir -p $(B)/tetris
	cd games/tetris/tetris && NWLINK="$(NWLINK)" $(CARGO) build --release --quiet
	$(ARM_LD) -r -z noexecstack --gc-sections -e np_tetris_main -u np_tetris_main \
	  games/tetris/tetris/target/thumbv7em-none-eabihf/release/libtetris.a -o $@

# A big game (XL_GAMES) becomes one from its own .nwa, which has its libraries in it. Its heap is the
# RAM after its own in the arena: its _heap_start is np_<game>_heap and its malloc's _sbrk the launcher's.
$(B)/modules/%.o: FORCE | $(B)
	@if [ "$*" = tetris ]; then $(MAKE) --no-print-directory $(MOD_tetris); \
	elif [ -n "$(filter $*,$(XL_GAMES))" ]; then $(MAKE) --no-print-directory -C games/$* build NWLINK="$(NWLINK)" $(if $(filter $*,$(GAMES_REDIRECT)),$(call GAME_REDIRECT_ARG,$*),); \
	else $(MAKE) --no-print-directory -C games/$* module NWLINK="$(NWLINK)" $(if $(filter $*,$(GAMES_REDIRECT)),$(call GAME_REDIRECT_ARG,$*),); fi
	$(ARM_LD) -r -d -z noexecstack --force-group-allocation -T tools/module.ld $(MOD_$*) -o $(B)/modules/$*.1.o
	$(ARM_OBJCOPY) --keep-global-symbol=$(or $(ENTRY_$*),$(ENTRY)) \
	  --redefine-sym eadk_keyboard_scan=np_keyboard_scan --redefine-sym eadk_event_get=np_event_get \
	  --redefine-sym eadk_display_draw_string=np_display_draw_string \
	  --redefine-sym _heap_start=np_$*_heap --redefine-sym _sbrk=np_sbrk \
	  $(B)/modules/$*.1.o $(B)/modules/$*.2.o
	$(PY) tools/npmodule.py $(B)/modules/$*.2.o $@ --game $* --index $(INDEX_$*) \
	  --entry $(or $(ENTRY_$*),$(ENTRY)) --json $(B)/modules/$*.json

# A language's tree (make kit-langs) takes the big games' modules from the English build (XL_FROM) as they are:
# they're in English, and call nothing of NumPlay's but the launcher (KIT_API)
ifneq ($(XL_FROM),)
$(foreach g,$(XL_GAMES),$(B)/modules/$(g).o): $(B)/modules/%.o: $(XL_FROM)/$(B)/modules/%.o | $(B)
	cp $< $@
	cp $(XL_FROM)/$(B)/modules/$*.json $(B)/modules/$*.json
endif

# games in the standard layout (see tools/games_mk.py)
$(foreach g,$(STD_GAMES),$(eval MOD_$(g) = $(if $(filter $(g),$(GAMES_REDIRECT)),$(B)/games/$(g)/module.o,games/$(g)/output/module.o))$(eval SIM_$(g) = $(if $(filter $(g),$(GAMES_REDIRECT)),$(B)/games/$(g)/sim-module.o,games/$(g)/output/sim-module.o)))

MODULES = $(foreach g,$(GAMES),$(B)/modules/$(g).o)

# ------------------------------------------------------------------ generated glue
$(B)/gen/numplay.ld: games/games.json tools/gen_games.py $(foreach g,$(GAMES) $(OPTIONAL_GAMES),$(B)/modules/$(g).o)
# (and the big games' modules, which take minutes: only for the kit)
$(B)/gen/numplay.ld: $(if $(filter kit kit-langs,$(MAKECMDGOALS)),$(foreach g,$(XL_GAMES),$(B)/modules/$(g).o))
	$(PY) tools/gen_games.py games/games.json $(B)/modules $(B)/gen

$(B)/gen/shots_%.c: games/games.json tools/shots.py $$(SHOTS_$$*) | $(B)
	cd games/$* && $(PY) ../../tools/shots.py --id $* --index $(INDEX_$*) $(COLORS_$*) ../../$@ $(SHOTS_REL_$*)

# Retro-Go's table of games (tools/romtable.py): empty, or ROMS="a.gb b.nes" to try some games
# in a build of one's own; the customizer website makes its own table. It goes with retrogo's
# module (numplay.ld puts it in retrogo's block of flash).
ROMS ?=
$(B)/arm/roms.o: FORCE | $(B)
	$(PY) tools/romtable.py -o $@ $(ROMS)

# ------------------------------------------------------------------ the kit
# NumPlay.nwa is linked from a kit, the same pieces the customizer website links in the browser
# for a NumPlay with the games you pick (site/): base.o, the launcher with every library function
# a game calls; arena.o, the RAM the games share; the app's name and icon; and one object per game
# with its module, its entry in the launcher's table, its screenshots and the marks of its block.
# Games marked "default": false in games.json (OPTIONAL_GAMES) are only in the kit, and so are the big
# games (XL_GAMES, "xl": true), for calculators with Enlarged Extra Apps: a NumPlay with several of them.
KIT = $(B)/kit
KIT_ALL = $(GAMES) $(OPTIONAL_GAMES) $(XL_GAMES)
LAUNCHER_OBJS = $(patsubst launcher/src/%.c,$(B)/arm/%.o,$(LAUNCHER_SRC))
KIT_GAMES = $(foreach g,$(KIT_ALL),$(KIT)/games/$(g).o)
NP_GAMES = $(foreach g,$(GAMES),$(KIT)/games/$(g).o)
KIT_LINK = -r --gc-sections -z noexecstack -e main -u eadk_app_name -u eadk_app_icon -u eadk_api_level

$(B)/arm/%.o: launcher/src/%.c $(LAUNCHER_H) VERSION | $(B)
	$(ARM_CC) $(ARM_CFLAGS) -flto -c $< -o $@
$(B)/arm/shots_%.o: $(B)/gen/shots_%.c launcher/src/np.h
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@
$(B)/arm/entry_%.o: $(B)/gen/numplay.ld launcher/src/np.h
	$(ARM_CC) $(ARM_CFLAGS) -c $(B)/gen/entry_$*.c -o $@
$(B)/arm/marks_%.o: $(B)/gen/numplay.ld
	$(ARM_CC) $(EADK_CFLAGS) -c $(B)/gen/marks_$*.s -o $@
$(B)/arm/arena.o: $(B)/gen/numplay.ld
	$(ARM_CC) $(EADK_CFLAGS) -c $(B)/gen/arena.s -o $@
$(KIT)/games/%.o: $(B)/modules/%.o $(B)/arm/entry_%.o $(B)/arm/shots_%.o $(B)/arm/marks_%.o
	@mkdir -p $(KIT)/games
	$(ARM_LD) -r -z noexecstack $^ -o $@
# Every symbol a game needs from the C library, libm or libgcc goes in base.o; each stays a
# section of its own (tools/kit_base.ld), so the final link keeps only those its games call.
# The big games bring their libraries along: they only call the launcher (KIT_API, which tools/kit.py
# checks), so base.o is the same without them.
KIT_API = numplay_launcher np_keyboard_scan np_event_get np_display_draw_string np_sbrk
KIT_SMALL = $(foreach g,$(GAMES) $(OPTIONAL_GAMES),$(KIT)/games/$(g).o)
$(KIT)/base.o: $(LAUNCHER_OBJS) $(KIT_SMALL) tools/kit_base.ld
	$(ARM_CC) $(ARM_CFLAGS) -nostartfiles --specs=nano.specs -Wl,--relocatable -Wl,--unique -Wl,--gc-sections \
	  -Wl,-e,main -Wl,-u,eadk_api_level -Wl,-T,tools/kit_base.ld -flinker-output=nolto-rel $(LAUNCHER_OBJS) \
	  $(foreach s,$(KIT_API),-Wl,-u,$(s)) \
	  $$(arm-none-eabi-nm -u $(KIT_SMALL) | awk '$$1 == "U" || $$1 == "w" {print "-Wl,-u," $$2}' | sort -u) -lm -lgcc -o $@

.SECONDARY: $(foreach v,$(VARIANTS),$(B)/variant/$(v)-name.o $(B)/variant/$(v)-icon.o) \
  $(foreach g,$(KIT_ALL),$(B)/arm/shots_$(g).o $(B)/arm/entry_$(g).o $(B)/arm/marks_$(g).o)
$(B)/variant/%-name.o: launcher/app_name.c Makefile | $(B)
	mkdir -p $(B)/variant
	$(ARM_CC) $(ARM_CFLAGS) '-DNP_APP_NAME=$(NAME_$*)' -c $< -o $@
$(B)/variant/%-icon.o: $$(ICON_$$*) | $(B)
	mkdir -p $(B)/variant
	$(NWLINK) png-icon-o $< $@

# ------------------------------------------------------------------ NumPlay.nwa
$(B)/%.nwa: $(KIT)/base.o $(B)/arm/arena.o $(NP_GAMES) $(B)/variant/%-name.o $(B)/variant/%-icon.o
	$(ARM_LD) $(KIT_LINK) -T $(B)/gen/numplay.ld $(KIT)/base.o $(B)/arm/arena.o \
	  $(B)/variant/$*-name.o $(B)/variant/$*-icon.o $(NP_GAMES) -o $@
	arm-none-eabi-strip --strip-unneeded $@
	@if [ "$*" = NumPlay ]; then $(PY) tools/sizes.py $@ $(B)/modules; fi

# ------------------------------------------------------------------ the website's kit
# build/site-kit: what the customizer website (site/) downloads to link a NumPlay of your own
# (tools/kit.py checks it links NumPlay.nwa to the same bytes the Makefile does).
kit: $(KIT)/base.o $(B)/arm/arena.o $(KIT_GAMES) $(B)/NumPlay.nwa \
  $(foreach v,$(VARIANTS),$(B)/variant/$(v)-name.o $(B)/variant/$(v)-icon.o)
	$(PY) tools/kit.py games/games.json $(B) $(B)/site-kit --version $(VERSION) --variants "$(VARIANTS)" \
	  --default "$(GAMES)" --upsilon "$(UPSILON_GAMES)" --app-space $(APP_SPACE) --xl-app-space $(XL_APP_SPACE) \
	  --spaces "$(SPACES)" --ram $(CUSTOM_RAM) --ram-limit $(RAM_LIMIT) --upsilon-ram $(UPSILON_RAM) --nwlink "$(NWLINK)" \
	  --langs "$(LANGS)" --alone "$(foreach g,$(XL_GAMES),$(g)=$(if $(XL_FROM),$(XL_FROM)/)$(APP_$(g)))" \
	  $(if $(XL_FROM),--english "$(XL_FROM)/$(B)/site-kit")

# ...and in French and Chinese, from each language's tree (make lang), in site-kit/<code>. The big games stay in
# English: their modules come from this build (XL_FROM), their names, taglines and screenshots from the tree
kit-langs: kit
	@for l in $(LANGS); do \
	  [ -d $(B)/lang/$$l ] || $(PY) tools/lang.py tree $$l $(B)/lang/$$l || exit 1; \
	  cp Makefile $(B)/lang/$$l/ && cp tools/*.py tools/*.ld $(B)/lang/$$l/tools/ || exit 1; \
	  $(MAKE) -C $(B)/lang/$$l kit XL_FROM="$(CURDIR)" NWLINK="$(NWLINK)" || exit 1; \
	  rm -rf $(B)/site-kit/$$l && mkdir -p $(B)/site-kit/$$l && \
	  cp -R $(B)/lang/$$l/$(B)/site-kit/kit.json $(B)/lang/$$l/$(B)/site-kit/base.o.gz \
	    $(B)/lang/$$l/$(B)/site-kit/arena.o.gz $(B)/lang/$$l/$(B)/site-kit/games $(B)/site-kit/$$l/ || exit 1; \
	  cp $(B)/lang/$$l/$(B)/site-kit/apps/* $(B)/site-kit/apps/ 2>/dev/null; \
	done

check: nwa apps
	@x=$$(cat $(B)/gen/arena_extra); \
	  $(NWLINK) nwa-bin --ram-length $$(( $(CUSTOM_RAM) - x )) $(B)/NumPlay.nwa $(B)/NumPlay-custom.bin >/dev/null || \
	  { echo "a game of NumPlay needs more RAM than custom builds of 25.2 give apps ($(CUSTOM_RAM) bytes)"; exit 1; }
	@for a in $(BIG_APPS); do $(NWLINK) nwa-bin --ram-length $(CUSTOM_RAM) $(B)/apps/$$a.nwa $(B)/apps/$$a.bin >/dev/null || \
	  { echo "$$a.nwa needs more RAM than custom builds of 25.2 give apps ($(CUSTOM_RAM) bytes)"; exit 1; }; done; \
	  echo "NumPlay's games and $(BIG_APPS) fit in $(CUSTOM_RAM) bytes of RAM"
	@for a in $(APP_SPACE_APPS); do n=$$(wc -c < $(B)/apps/$$a.bin); \
	  [ $$n -le $(APP_SPACE) ] || { echo "$$a.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }; \
	  echo "$$a.nwa installs as $$n bytes ($$(( $(APP_SPACE) - n )) to spare)"; done
	@for a in $(foreach g,$(OPTIONAL_GAMES),$(if $(NWA_$(g)),$(basename $(NWA_$(g))))); do \
	  $(NWLINK) nwa-bin --ram-length $(CUSTOM_RAM) $(B)/apps/$$a.nwa $(B)/apps/$$a.bin >/dev/null || \
	  { echo "$$a.nwa needs more RAM than custom builds of 25.2 give apps ($(CUSTOM_RAM) bytes)"; exit 1; }; \
	  echo "$$a.nwa (not in NumPlay.nwa: the customizer adds it) fits in $(CUSTOM_RAM) bytes of RAM"; done
	@x=$$(cat $(B)/gen-upsilon/arena_extra); \
	  $(NWLINK) nwa-bin --ram-length $$(( $(UPSILON_RAM) - x )) $(B)/NumPlay-Upsilon.nwa $(B)/NumPlay-Upsilon.bin && \
	  echo "NumPlay-Upsilon.nwa installs as $$(wc -c < $(B)/NumPlay-Upsilon.bin) bytes; its RAM and any game's fit in $(UPSILON_RAM) bytes"
	@x=$$(cat $(B)/gen/arena_extra); \
	  for v in $(VARIANTS); do $(NWLINK) nwa-bin --ram-length $$(( $(RAM_LIMIT) - x )) $(B)/$$v.nwa $(B)/$$v.bin || exit 1; \
	  n=$$(wc -c < $(B)/$$v.bin); \
	  echo "$$v.nwa installs as $$n bytes ($$(( $(APP_SPACE) - n )) to spare); its RAM and any game's fit in $(RAM_LIMIT) bytes"; \
	  [ $$n -le $(APP_SPACE) ] || { echo "$$v.nwa is bigger than the calculator's app space ($(APP_SPACE) bytes)"; exit 1; }; done

# ------------------------------------------------------------------ NumPlay-Upsilon.nwa
# The same kit, with the games that fit Upsilon's RAM (its arena is NumPlay's: each game's place
# in it doesn't depend on the others).
$(B)/gen-upsilon/numplay.ld: games/games.json tools/gen_games.py $(foreach g,$(UPSILON_GAMES),$(B)/modules/$(g).o)
	$(PY) tools/gen_games.py games/games.json $(B)/modules $(B)/gen-upsilon --only "$(UPSILON_GAMES)"
$(B)/NumPlay-Upsilon.nwa: $(KIT)/base.o $(B)/arm/arena.o $(foreach g,$(UPSILON_GAMES),$(KIT)/games/$(g).o) \
  $(B)/variant/NumPlay-name.o $(B)/variant/NumPlay-icon.o $(B)/gen-upsilon/numplay.ld
	$(ARM_LD) $(KIT_LINK) -T $(B)/gen-upsilon/numplay.ld $(filter %.o,$^) -o $@
	arm-none-eabi-strip --strip-unneeded $@

# ------------------------------------------------------------------ NumPlay in other languages
# tools/lang.py copies what builds NumPlay to build/lang/<code> with the translations in place
# (launcher/lang/<code>.txt, games/<game>/lang/<code>.txt), and NumPlay is built there.
LANGS = fr zh
LANG_fr = French
LANG_zh = Chinese
lang: $(foreach l,$(LANGS),$(B)/NumPlay-$(LANG_$(l)).nwa)
$(foreach l,$(LANGS),$(B)/NumPlay-$(LANG_$(l)).nwa): $(B)/NumPlay-%.nwa: FORCE | $(B)
	$(eval L := $(if $(filter French,$*),fr,zh))
	$(PY) tools/lang.py tree $(L) $(B)/lang/$(L)
	$(MAKE) -C $(B)/lang/$(L) build/data/NumPlay.nwa NWLINK="$(NWLINK)"
	$(NWLINK) nwa-bin --ram-length $$(( $(CUSTOM_RAM) - $$(cat $(B)/lang/$(L)/build/data/gen/arena_extra) )) \
	  $(B)/lang/$(L)/build/data/NumPlay.nwa $(B)/lang/$(L)/build/data/NumPlay.bin
	@n=$$(wc -c < $(B)/lang/$(L)/build/data/NumPlay.bin); echo "NumPlay-$*.nwa installs as $$n bytes ($$(( $(APP_SPACE) - n )) to spare)"; \
	  [ $$n -le $(APP_SPACE) ] || { echo "NumPlay-$*.nwa is bigger than the calculator's app space"; exit 1; }
	cp $(B)/lang/$(L)/build/data/NumPlay.nwa $@

# ------------------------------------------------------------------ games on their own
APP_numdash = games/numdash/build/numdash.nwa:NumDash.nwa
APP_crossyroad = games/crossyroad/output/crossyroad.nwa:CrossyRoad.nwa
APP_numdrive = games/numdrive/output/device/numdrive.nwa:NumDrive.nwa
APP_balatro = $(B)/games/balatro/balatro.nwa:Balatro.nwa
APP_buckshot = $(B)/games/buckshot/buckshot.nwa:BuckshotRoulette.nwa
APP_portal = $(B)/games/portal/portal.nwa:PortalReturns.nwa
APP_chess = $(B)/games/chess/chess.nwa:NumChess.nwa
APP_tetris = games/tetris/tetris/target/thumbv7em-none-eabihf/release/tetris:Tetris.nwa
# Tetris on its own, without building every game (tools/build.py --game tetris)
tetris-app: | $(B)
	cd games/tetris/tetris && NWLINK="$(NWLINK)" $(CARGO) build --release --quiet
	cp $(word 1,$(subst :, ,$(APP_tetris))) $(B)/apps/Tetris.nwa
	arm-none-eabi-strip --strip-unneeded $(B)/apps/Tetris.nwa
APP_numvisuals = $(B)/games/numvisuals/numvisuals.nwa:NumVisuals.nwa
# too big to share the calculator's app space with NumPlay: only on their own
APP_celeste = $(B)/games/celeste/celeste.nwa:Celeste.nwa
APP_championisland = $(B)/games/championisland/championisland.nwa:ChampionIsland.nwa
APP_hollowknight = $(B)/games/hollowknight/hollowknight.nwa:HollowKnight.nwa
APP_undertale = $(B)/games/undertale/undertale.nwa:Undertale.nwa
APP_subway = $(B)/games/subway/subway.nwa:SubwaySurfers.nwa
APP_fnaf = $(B)/games/fnaf/fnaf.nwa:FiveNightsAtFreddys.nwa
APP_hillclimb = $(B)/games/hillclimb/hillclimb.nwa:HillClimbRacing.nwa
APP_finalearth2 = $(B)/games/finalearth2/finalearth2.nwa:FinalEarth2.nwa
APP_numpowder = $(B)/games/numpowder/numpowder.nwa:NumPowder.nwa
APP_doom = $(B)/games/doom/doom.nwa:Doom.nwa
APP_isaac = $(B)/games/isaac/isaac.nwa:BindingOfIsaac.nwa
APP_pvz = $(B)/games/pvz/pvz.nwa:PlantsVsZombies.nwa
APP_sonic = $(B)/games/sonic/sonic.nwa:SonicTheHedgehog.nwa
APP_cookieclicker = $(B)/games/cookieclicker/cookieclicker.nwa:CookieClicker.nwa
APP_leveldevil = $(B)/games/leveldevil/leveldevil.nwa:LevelDevil.nwa
APP_jetpackjoyride = $(B)/games/jetpackjoyride/jetpackjoyride.nwa:JetpackJoyride.nwa
APP_stickmanhook = $(B)/games/stickmanhook/stickmanhook.nwa:StickmanHook.nwa
APP_donkeykong = $(B)/games/donkeykong/donkeykong.nwa:DonkeyKong.nwa
APP_dune = $(B)/games/dune/dune.nwa:Dune.nwa
APP_monkeymart = $(B)/games/monkeymart/monkeymart.nwa:MonkeyMart.nwa
APP_tombofthemask = $(B)/games/tombofthemask/tombofthemask.nwa:TombOfTheMask.nwa
APP_angrybirds = games/angrybirds/output/angrybirds.nwa:AngryBirds.nwa
# the big games' modules are made from these (the kit)
$(foreach g,$(XL_GAMES),$(eval MOD_$(g) = $$(word 1,$$(subst :, ,$$(APP_$(g))))))

apps: | $(B)
	$(MAKE) -C games/numdash build NWLINK="node node_modules/nwlink/bin/nwlink"
	$(MAKE) -C games/crossyroad NWLINK="$(NWLINK)"
	$(MAKE) -C games/numdrive NWLINK="$(NWLINK)"
	$(MAKE) -C games/balatro build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/balatro
	$(MAKE) -C games/buckshot build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/buckshot
	$(MAKE) -C games/portal build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/portal
	$(MAKE) -C games/chess build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/chess
	$(MAKE) -C games/numvisuals build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/numvisuals
	$(MAKE) -C games/celeste build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/celeste
	$(MAKE) -C games/championisland build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/championisland
	$(MAKE) -C games/hollowknight build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/hollowknight
	$(MAKE) -C games/undertale build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/undertale
	$(MAKE) -C games/subway build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/subway
	$(MAKE) -C games/fnaf build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/fnaf
	$(MAKE) -C games/hillclimb build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/hillclimb
	$(MAKE) -C games/finalearth2 build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/finalearth2
	$(MAKE) -C games/numpowder build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/numpowder
	$(MAKE) -C games/doom build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/doom
	$(MAKE) -C games/isaac build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/isaac
	$(MAKE) -C games/pvz build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/pvz
	$(MAKE) -C games/sonic build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/sonic
	$(MAKE) -C games/cookieclicker build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/cookieclicker
	$(MAKE) -C games/leveldevil build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/leveldevil
	$(MAKE) -C games/jetpackjoyride build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/jetpackjoyride
	$(MAKE) -C games/stickmanhook build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/stickmanhook
	$(MAKE) -C games/donkeykong build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/donkeykong
	$(MAKE) -C games/dune build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/dune
	$(MAKE) -C games/monkeymart build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/monkeymart
	$(MAKE) -C games/tombofthemask build NWLINK="$(NWLINK)" BUILD_DIR=../../$(B)/games/tombofthemask
	cd games/tetris/tetris && NWLINK="$(NWLINK)" $(CARGO) build --release --quiet
	cp $(word 1,$(subst :, ,$(APP_numdash))) $(B)/apps/$(word 2,$(subst :, ,$(APP_numdash)))
	cp $(word 1,$(subst :, ,$(APP_crossyroad))) $(B)/apps/$(word 2,$(subst :, ,$(APP_crossyroad)))
	cp $(word 1,$(subst :, ,$(APP_numdrive))) $(B)/apps/$(word 2,$(subst :, ,$(APP_numdrive)))
	cp $(word 1,$(subst :, ,$(APP_balatro))) $(B)/apps/$(word 2,$(subst :, ,$(APP_balatro)))
	cp $(word 1,$(subst :, ,$(APP_buckshot))) $(B)/apps/$(word 2,$(subst :, ,$(APP_buckshot)))
	cp $(word 1,$(subst :, ,$(APP_portal))) $(B)/apps/$(word 2,$(subst :, ,$(APP_portal)))
	cp $(word 1,$(subst :, ,$(APP_chess))) $(B)/apps/$(word 2,$(subst :, ,$(APP_chess)))
	cp $(word 1,$(subst :, ,$(APP_tetris))) $(B)/apps/$(word 2,$(subst :, ,$(APP_tetris)))
	cp $(word 1,$(subst :, ,$(APP_numvisuals))) $(B)/apps/$(word 2,$(subst :, ,$(APP_numvisuals)))
	cp $(word 1,$(subst :, ,$(APP_celeste))) $(B)/apps/$(word 2,$(subst :, ,$(APP_celeste)))
	cp $(word 1,$(subst :, ,$(APP_championisland))) $(B)/apps/$(word 2,$(subst :, ,$(APP_championisland)))
	cp $(word 1,$(subst :, ,$(APP_hollowknight))) $(B)/apps/$(word 2,$(subst :, ,$(APP_hollowknight)))
	cp $(word 1,$(subst :, ,$(APP_undertale))) $(B)/apps/$(word 2,$(subst :, ,$(APP_undertale)))
	cp $(word 1,$(subst :, ,$(APP_subway))) $(B)/apps/$(word 2,$(subst :, ,$(APP_subway)))
	cp $(word 1,$(subst :, ,$(APP_fnaf))) $(B)/apps/$(word 2,$(subst :, ,$(APP_fnaf)))
	cp $(word 1,$(subst :, ,$(APP_hillclimb))) $(B)/apps/$(word 2,$(subst :, ,$(APP_hillclimb)))
	cp $(word 1,$(subst :, ,$(APP_finalearth2))) $(B)/apps/$(word 2,$(subst :, ,$(APP_finalearth2)))
	cp $(word 1,$(subst :, ,$(APP_numpowder))) $(B)/apps/$(word 2,$(subst :, ,$(APP_numpowder)))
	cp $(word 1,$(subst :, ,$(APP_doom))) $(B)/apps/$(word 2,$(subst :, ,$(APP_doom)))
	cp $(word 1,$(subst :, ,$(APP_isaac))) $(B)/apps/$(word 2,$(subst :, ,$(APP_isaac)))
	cp $(word 1,$(subst :, ,$(APP_pvz))) $(B)/apps/$(word 2,$(subst :, ,$(APP_pvz)))
	cp $(word 1,$(subst :, ,$(APP_sonic))) $(B)/apps/$(word 2,$(subst :, ,$(APP_sonic)))
	cp $(word 1,$(subst :, ,$(APP_cookieclicker))) $(B)/apps/$(word 2,$(subst :, ,$(APP_cookieclicker)))
	cp $(word 1,$(subst :, ,$(APP_leveldevil))) $(B)/apps/$(word 2,$(subst :, ,$(APP_leveldevil)))
	cp $(word 1,$(subst :, ,$(APP_jetpackjoyride))) $(B)/apps/$(word 2,$(subst :, ,$(APP_jetpackjoyride)))
	cp $(word 1,$(subst :, ,$(APP_stickmanhook))) $(B)/apps/$(word 2,$(subst :, ,$(APP_stickmanhook)))
	cp $(word 1,$(subst :, ,$(APP_donkeykong))) $(B)/apps/$(word 2,$(subst :, ,$(APP_donkeykong)))
	cp $(word 1,$(subst :, ,$(APP_dune))) $(B)/apps/$(word 2,$(subst :, ,$(APP_dune)))
	cp $(word 1,$(subst :, ,$(APP_monkeymart))) $(B)/apps/$(word 2,$(subst :, ,$(APP_monkeymart)))
	cp $(word 1,$(subst :, ,$(APP_tombofthemask))) $(B)/apps/$(word 2,$(subst :, ,$(APP_tombofthemask)))
	arm-none-eabi-strip --strip-unneeded $(B)/apps/Tetris.nwa

# Angry Birds installs separately from the launcher.
apps: $(B)/apps/AngryBirds.nwa
$(B)/apps/AngryBirds.nwa: FORCE | $(B)
	$(MAKE) --no-print-directory -C games/angrybirds build NWLINK="$(NWLINK)" PYTHON="$(PY)"
	mkdir -p $(B)/apps
	cp games/angrybirds/output/angrybirds.nwa $@

STD_APPS = $(foreach g,$(STD_GAMES),$(B)/apps/$(NWA_$(g)))
apps: $(STD_APPS)
$(STD_APPS): $(B)/apps/%.nwa: FORCE | $(B)
	$(MAKE) --no-print-directory -C games/$(APPID_$*) build NWLINK="$(NWLINK)" $(if $(filter $(APPID_$*),$(GAMES_REDIRECT)),$(call GAME_REDIRECT_ARG,$(APPID_$*)),)
	cp $(if $(filter $(APPID_$*),$(GAMES_REDIRECT)),$(B)/games/$(APPID_$*)/$(APPID_$*).nwa,games/$(APPID_$*)/output/$(APPID_$*).nwa) $@

# ------------------------------------------------------------------ simulator
SIM_numdash = games/numdash/build/sim-module.o
SIM_crossyroad = games/crossyroad/output/sim-module.o
SIM_numdrive = games/numdrive/output/sim-module.o
SIM_balatro = $(B)/games/balatro/sim-module.o
SIM_buckshot = $(B)/games/buckshot/sim-module.o
SIM_portal = $(B)/games/portal/sim-module.o
SIM_chess = $(B)/games/chess/sim-module.o
SIM_tetris = $(B)/sim/tetris.o
SIM_CFLAGS = -std=gnu11 -O2 -fPIC -DNP_SIMULATOR=1 -DNP_VERSION='"$(VERSION)"' $(EADK_SIM_CFLAGS) -Ilauncher/src -Wall -Wno-unused-parameter

$(B)/sim/tetris.o: FORCE | $(B)
	cd games/tetris/tetris && NWLINK="$(NWLINK)" $(CARGO) build --lib --release --quiet --target aarch64-apple-darwin
	cc -r -nostdlib -arch arm64 -Wl,-u,_np_tetris_main -Wl,-exported_symbol,_np_tetris_main \
	  games/tetris/tetris/target/aarch64-apple-darwin/release/libtetris.a -o $@

$(B)/sim/%.mod: FORCE | $(B)
	@if [ "$*" != tetris ]; then $(MAKE) --no-print-directory -C games/$* sim-module NWLINK="$(NWLINK)" $(if $(filter $*,$(GAMES_REDIRECT)),$(call GAME_REDIRECT_ARG,$*),); \
	else $(MAKE) --no-print-directory $(SIM_tetris); fi

$(B)/sim/gen/gametable.c: games/games.json tools/gen_games.py $(MODULES)
	$(PY) tools/gen_games.py games/games.json $(B)/modules $(B)/sim/gen --simulator

sim: $(B)/NumPlay.nwb
$(B)/NumPlay.nwb: $(LAUNCHER_SRC) $(LAUNCHER_H) $(B)/sim/gen/gametable.c $(foreach g,$(GAMES),$(B)/gen/shots_$(g).c) \
  $(foreach g,$(GAMES),$(B)/sim/$(g).mod)
	cc $(SIM_CFLAGS) -shared -undefined dynamic_lookup $(LAUNCHER_SRC) launcher/sim/*.c $(B)/sim/gen/gametable.c \
	  $(foreach g,$(GAMES),$(B)/gen/shots_$(g).c) $(foreach g,$(GAMES),$(SIM_$(g))) -o $@

# ------------------------------------------------------------------ tools
emu: $(B)/NumPlay.nwa
	$(PY) tools/emu.py $< --ms 4000 --shots 3000 --out $(B)/emu

clean:
	rm -rf $(B) build/compiled
	-$(MAKE) -C games/numdash clean
	-$(MAKE) -C games/crossyroad clean
	-$(MAKE) -C games/numdrive clean
	-$(MAKE) -C games/balatro clean
	-$(MAKE) -C games/buckshot clean
	-$(MAKE) -C games/portal clean
	-$(MAKE) -C games/chess clean
	-$(MAKE) -C games/numvisuals clean
	-$(MAKE) -C games/championisland clean
	-$(MAKE) -C games/hollowknight clean
	-$(MAKE) -C games/undertale clean
	-$(MAKE) -C games/subway clean
	-$(MAKE) -C games/fnaf clean
	-$(MAKE) -C games/hillclimb clean
	-$(MAKE) -C games/finalearth2 clean
	-$(MAKE) -C games/numpowder clean
	-$(MAKE) -C games/doom clean
	-$(MAKE) -C games/isaac clean
	-$(MAKE) -C games/pvz clean
	-$(MAKE) -C games/sonic clean
	-$(MAKE) -C games/cookieclicker clean
	-$(MAKE) -C games/leveldevil clean
	-$(MAKE) -C games/jetpackjoyride clean
	-$(MAKE) -C games/stickmanhook clean
	-$(MAKE) -C games/donkeykong clean
	-$(MAKE) -C games/dune clean
	-$(MAKE) -C games/monkeymart clean
	-$(MAKE) -C games/tombofthemask clean
	-$(MAKE) -C games/angrybirds clean
	-cd games/tetris/tetris && $(CARGO) clean
	-$(foreach g,$(STD_GAMES),$(MAKE) -C games/$(g) clean;)
