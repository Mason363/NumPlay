#!/usr/bin/env python3
"""Plays each game's calculator build in the ARM emulator, twice, and checks
what used to crash calculators or lose saves:

- the app quits on Home by itself (Epsilon holds Home back while it runs), and
  on On/Off (the second run), and hands back its RAM cleared, so Epsilon's
  memory pools come back safely;
- no unaligned multi-word access (a fault on the Cortex-M7), no write to flash
  or to the firmware's RAM outside the file system;
- the file system stays valid: the game's save is there, the other files are
  untouched, and a file added after the save does not stop the game from
  updating it on the second run.

Usage: test_games.py build/compiled/games [--game NAME] [--out DIR]
"""
import argparse
import json
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import emu  # noqa: E402

K = emu.KEYS


def presses(*items):
    """(time, key[, duration]) -> key ranges."""
    out = []
    for it in items:
        t, k = it[0], it[1]
        d = it[2] if len(it) > 2 else 90
        out.append((t, t + d, K[k]))
    return out


def every(t0, t1, step, key, d=80):
    return [(t, t + d, K[key]) for t in range(t0, t1, step)]


# name: (nwa, save record, first run keys, home time, second run keys, home time)
GAMES = {
    "crossyroad": ("CrossyRoad.nwa", "crossyroad.sav",
                   every(1500, 9000, 350, "up"), 12000, every(1500, 4000, 400, "up"), 6000),
    "chess": ("NumChess.nwa", "numchess.sav", presses((1500, "ok"), (2500, "ok")), 4000, [], 2500),
    "numdash": ("NumDash.nwa", "numdash.nds", presses((2500, "ok"), (4000, "ok"), (5500, "ok")), 9000,
                presses((2500, "ok")), 5000),
    # pause, Levels, level list, next level, play: that saves the level to start from
    # (held keys: the pause card's frames are slow while it slides in)
    "numdrive": ("NumDrive.nwa", "drivemad.sav",
                 presses((1500, "ok", 250), (2000, "right", 250), (2500, "ok", 250), (3000, "right", 250),
                         (3500, "ok", 250)), 5500,
                 presses((1500, "ok", 250), (2000, "right", 250), (2500, "ok", 250), (3000, "left", 250),
                         (3500, "ok", 250)), 5500),
    "tetris": ("Tetris.nwa", "tetris.sav", presses((1200, "ok")) + every(2500, 7000, 500, "up"), 8000,
               presses((1200, "ok"), (2500, "ok")), 5000),
    # start and run into the first cactus: a best; next time, start and hold OK (the keyboard's
    # repeat jumps again on landing): further, a better best
    "dinorun": ("ChromeDino.nwa", "dinorun.sav", presses((1500, "ok")), 11000,
                presses((1500, "ok")) + [(1700, 10500, K["ok"])], 11000),
    # add-ons, the Counter (5th), count to 2; next time it opens there: one more
    "numvisuals": ("NumVisuals.nwa", "numvisuals.sav",
                   presses((1500, "ok"), (2000, "right"), (2300, "right"), (2600, "right"), (2900, "right"),
                           (3300, "ok"), (3700, "up"), (4000, "up")), 5000,
                   presses((1500, "up")), 2500),
    # settings: Speed to Fast, back, then a Custom game; next time Speed goes on to Insane
    # past how to play, two flaps; next time the run goes on after a flap
    "flappydunk": ("FlappyDunk.nwa", "flappydunk.sav", presses((1500, "ok"), (2200, "ok"), (2550, "ok")), 3500,
                   presses((1500, "ok")), 2500),
    # past how to play, a fruit dropped, the cloud moved, another dropped; next time the box is still there, and one
    # more falls in
    "suika": ("SuikaGame.nwa", "suika.sav", presses((1500, "ok"), (2200, "ok"), (2600, "right", 400), (3600, "ok")), 4800,
              presses((1500, "ok")), 3000),
    # past how to play, a game started and a block placed; next time the tower is still there, waiting
    "towerblock": ("TowerBlocks.nwa", "towerblocks.sav", presses((1500, "ok"), (2200, "ok"), (3000, "ok")), 3600, [], 2500),
    # past how to play (the saved level 5 kept), level 5 started and two balls shot; next time the menu, Levels,
    # two to the right and OK: level 7, saved
    "coreball": ("CoreBall.nwa", "coreball.sav", presses((1500, "ok"), (2000, "ok"), (2800, "ok"), (3300, "ok")), 3800,
                 presses((1500, "back"), (1800, "down"), (2100, "down"), (2400, "ok"), (2800, "right"), (3100, "right"),
                         (3400, "ok")), 4500),
    # past how to play, a climb started; next time it goes on after OK
    "doodlejump": ("DoodleJump.nwa", "doodlejump.sav", presses((1500, "ok"), (2200, "ok")), 3500,
                   presses((1500, "ok")), 2500),
    "flappybird": ("FlappyBird.nwa", "flappy.sav",
                   presses((1500, "down"), (1800, "ok"), (2100, "right"), (2400, "back"), (2700, "up"),
                           (2900, "right"), (3100, "ok"), (3900, "ok")) + every(4300, 7500, 450, "ok"), 9000,
                   presses((1500, "down"), (1800, "ok"), (2100, "right"), (2400, "back")), 3500),
    # continue the saved game (level 2) and play a bit; then set Fast in the options
    "pacman": ("PacMan.nwa", "pacman.sav", presses((1500, "ok")), 8000,
               presses((1500, "down"), (1800, "ok"), (2100, "right"), (2400, "back")), 3500),
    # Play, straight through the first apple into the wall: a best of 1; next
    # time, Settings, the fruit to a banana and back: saved, the best kept
    "snake": ("Snake.nwa", "snake.sav", presses((1500, "ok"), (2000, "right")), 7000,
              presses((1500, "down"), (1800, "ok"), (2100, "right"), (2400, "back")), 3500),
    # vs the computer: the settings are saved; next time, the dark theme
    "connectfour": ("ConnectFour.nwa", "connect4.sav", presses((1500, "ok"), (2500, "4")), 5000,
                    presses((1500, "down"), (1700, "down"), (1900, "down"), (2100, "down"), (2300, "right")), 3500),
    # a new game (Vegas, draw 1 from the seeded options), two draws; then Continue and one more
    "solitaire": ("Solitaire.nwa", "solitaire.sav", presses((1500, "ok"), (3500, "exe"), (4000, "exe")), 5000,
                  presses((1500, "ok"), (2500, "exe")), 3500),
    # the 4x4 game in the save (40 moves): Continue, two moves; next time three more
    "g2048": ("2048.nwa", "g2048.sav", presses((1500, "ok"), (2500, "left"), (3000, "right")), 4000,
              presses((1500, "ok"), (2500, "left"), (3000, "right"), (3500, "left")), 4500),
    # Beginner, the first square, leave mid-game; next time Continue, then a
    # new game from the pause menu and its first square
    "minesweeper": ("Minesweeper.nwa", "mines.sav", presses((1500, "ok"), (2000, "ok"), (2400, "right")), 3000,
                    presses((1500, "ok"), (2000, "back"), (2300, "down"), (2600, "ok"), (3000, "ok")), 3600),
    # past the title and the jaws, the first door: the runner runs right into the first pit; next time (only
    # the copy of the save left) the first door again, and another death
    "leveldevil": ("LevelDevil.nwa", "leveldevil.sav", presses((1800, "ok"), (3200, "ok"), (4300, "right", 7000)), 9500,
                   presses((1800, "ok"), (3200, "ok"), (4300, "right", 7000)), 9500),
    # Play, serve, a brick or more: the best is kept; next time, Settings, screen shake off, back
    "breakout": ("BlockBreaker.nwa", "breakout.sav", presses((1500, "ok"), (2500, "ok")), 6000,
                 presses((1500, "down"), (1800, "ok"), (2100, "down"), (2400, "ok"), (2800, "back")), 4000),
    # Air Hockey against the easy computer, a little play; next time the normal computer, and Tanks
    "twoplayer": ("TwoPlayers.nwa", "twoplayer.sav",
                  presses((1500, "down"), (1800, "right"), (2100, "ok"), (2600, "ok"), (3000, "up", 800)), 5000,
                  presses((1500, "right"), (1800, "down")), 3000),
    # play, new run, the small blind, pick two cards and play them; then continue the run
    "balatro": ("Balatro.nwa", "balatro.sav",
                presses((1500, "ok"), (2500, "ok"), (3500, "ok"), (5000, "ok"), (5400, "right"), (5800, "ok"),
                        (6300, "exe")), 10000,
                presses((1500, "ok"), (2500, "ok")), 5000),
    # start, then through the bathroom and the hallway to the table; then continue
    "buckshot": ("BuckshotRoulette.nwa", "buckshot.sav", every(1500, 14000, 900, "ok"), 16000,
                 presses((1500, "ok")), 4000),
    # the guide, then a block placed in the middle; next time the game goes on: one more in the corner
    "blockblast": ("BlockBlast.nwa", "blockblast.sav", presses((1500, "ok"), (2000, "ok")), 3000,
                   presses(*[(1500 + 250 * i, "up") for i in range(4)], *[(2500 + 250 * i, "left") for i in range(4)],
                           (3600, "ok")), 4500),
    # a game: right to the gap between the first bunkers, and four shots up the third column; Home keeps the
    # best. Next time another game, five shots, quit with On/Off: the best is read back, kept or beaten
    "invaders": ("SpaceInvaders.nwa", "invaders.sav",
                 presses((1500, "ok"), (2600, "right", 700), (3500, "ok"), (4100, "ok"), (4700, "ok"), (5300, "ok")), 6000,
                 presses((1500, "ok"), (2600, "right", 700), (3500, "ok"), (4100, "ok"), (4700, "ok"), (5300, "ok"),
                         (5900, "ok")), 6500),
    # past how to play, an Easy puzzle made, two hints; next time the game goes on: one more
    "sudoku": ("Sudoku.nwa", "sudoku.sav", presses((1500, "ok"), (2000, "ok"), (3500, "shift"), (4000, "shift")), 5500,
               presses((1500, "shift")), 3000),
    # past how to play, X in the middle of the middle board (5, 5), O sent there takes its top left (7);
    # next time the game goes on: X, sent to the top left board, takes its top right (9)
    "uttt": ("UltimateTicTacToe.nwa", "uttt.sav", presses((1500, "ok"), (2000, "5"), (2300, "5"), (2700, "7")), 4000,
             presses((1500, "9")), 3000),
    # past how to play, the first level and a few steps; next time the second level, picked in the list
    "sokoban": ("Sokoban.nwa", "sokoban.sav", presses((1500, "ok"), (2000, "ok"), (2600, "right"), (2900, "right")), 4000,
                presses((1500, "right"), (1900, "ok"), (2500, "up")), 4000),
    # the intro film is skipped (seen), Lucky walks up the dock and talks to the
    # guardians; next time the game starts where it was left
    "championisland": ("ChampionIsland.nwa", "champion.sav",
                       presses((2000, "up", 1500)) + every(4000, 9000, 700, "ok"), 11000,
                       presses((2000, "up", 400)), 4000),
    # past the key sheet, Singleplayer, Create New World (Survival, a random seed), walk; then
    # Play Selected World
    "numblocks": ("NumBlocks.nwa", "nb1.nbw",
                  presses((3000, "ok"), (3600, "ok"), (4200, "ok"), (4800, "down"), (5100, "down"), (5400, "down"),
                          (5700, "down"), (6000, "down"), (6300, "ok"), (8700, "comma", 1500)), 12200,
                  presses((3000, "ok"), (3600, "ok")), 8000),
    # past the key sheet, the title, Climb, the chapter select and Start: the Prologue, walking; then Continue
    "celeste": ("Celeste.nwa", "celeste.sav",
                presses((2500, "ok"), (4000, "ok"), (5500, "ok"), (7000, "ok"), (8500, "ok"), (11000, "right", 2500)), 15000,
                presses((2500, "ok"), (4000, "ok"), (5500, "ok"), (7500, "right", 1500)), 11000),
    # Start Game, the first profile (a new game: past how to play's two pages, the Knight falls into King's Pass and
    # lies there a while), then Home, which saves as Quit to Menu; then the same profile, loaded
    "hollowknight": ("HollowKnight.nwa", "hk1.sav",
                     presses((2500, "ok"), (4500, "ok"), (5500, "ok"), (6500, "ok")) + every(18000, 22000, 600, "right", 400),
                     25000,
                     presses((2500, "ok"), (4500, "ok")) + every(9000, 11000, 600, "left", 400), 14000),
    # past the intro and the title, the name "A", Done, Yes: Frisk falls on the golden flowers and walks; Home keeps
    # the moment, and the next start is back there
    "undertale": ("Undertale.nwa", "utresume.sav",
                  presses((1500, "ok"), (2500, "ok"), (3500, "ok"), (4500, "ok"),
                          *[(5000 + 200 * i, "down") for i in range(8)], (6800, "right"), (7000, "right"),
                          (7300, "ok"), (8300, "right"), (8600, "ok"), (10500, "right", 1500)), 13000,
                  presses((1500, "left", 1000)), 4000),
    # how to play, the title, a run up the middle track through the first coins; Home banks them. Next time
    # straight from the title into another run, quit with On/Off
    "subway": ("SubwaySurfers.nwa", "subway.sav", presses((1500, "ok"), (2500, "ok")), 8000,
               presses((1500, "ok")), 6000),
    # Continue: the first night's card, then the office; Home keeps the night, and the next start is back in it,
    # paused: Back, and the night goes on
    "fnaf": ("FiveNightsAtFreddys.nwa", "fnafres.sav", presses((1500, "down"), (1800, "ok")), 9000,
             presses((1500, "back")), 5000),
    # the title, the garage, the stage, how to drive, then the Hill Climber up the Countryside: Home keeps the run's
    # coins and best; next time straight from the stage to a second run
    "hillclimb": ("HillClimbRacing.nwa", "hillclimb.sav",
                  presses((1500, "ok"), (2500, "ok"), (3500, "ok"), (4500, "ok"), (5000, "right", 6000)), 12000,
                  presses((1500, "ok"), (2500, "ok"), (3500, "ok"), (4000, "right", 2500)), 7500),
    # the intro, then OK draws for a while (the autosave on Home); next time it comes back and more is drawn
    "numpowder": ("NumPowder.nwa", "numpowder.sav", presses((2000, "ok"), (3000, "ok", 1500)), 6000,
                  presses((2000, "ok"), (2500, "right", 600), (3200, "ok", 800)), 5000),
    # the title, the menu, New Game, Knee-Deep in the Dead, Hurt Me Plenty: the hangar, walking; Home keeps the
    # moment, and the next start is back in it
    "doom": ("Doom.nwa", "doomres.sav",
             presses((2500, "ok"), (3200, "ok"), (3900, "ok"), (4600, "ok"), (6500, "up", 1500)), 9500,
             presses((4000, "up", 1000)), 7000),
    # Press OK to Play: the first scenario, its first goal closed, the city runs; then Continue
    "finalearth2": ("FinalEarth2.nwa", "finalearth2.sav", presses((2000, "ok"), (3000, "ok")), 9000,
                    presses((2000, "ok")), 6000),
    # past the title, a new run as Isaac: the floor's name, then he walks and cries; Home keeps the run. Next time,
    # Continue, and the run goes on (quitting with On/Off)
    "isaac": ("BindingOfIsaac.nwa", "isaacrun.sav",
              presses((1500, "ok"), (2200, "ok"), (2800, "ok"), (3400, "ok"), (3900, "left", 1200), (4200, "6", 900),
                      (5400, "up", 600)), 7000,
              presses((1500, "ok"), (2200, "ok"), (2800, "right", 900), (3000, "8", 600)), 4500),
    # past the keys, Adventure: the street, Ready Set Plant, a peashooter on the sod; Home keeps the level. Next
    # time Continue, Back to Game, and the level goes on
    "pvz": ("PlantsVsZombies.nwa", "pvzlevel.sav", presses((1500, "ok"), (2500, "ok"), (3500, "ok"), (12500, "1"),
                                                           (13000, "ok")), 16000,
            presses((1500, "ok"), (2500, "ok"), (3500, "ok")), 9000),
    # past the keys, the SEGA screen and the title: Green Hill Zone, running; Home keeps the act, and next time
    # Continue goes back to it
    "sonic": ("SonicTheHedgehog.nwa", "sonic.sav",
              presses((1500, "ok"), (5500, "ok"), (7000, "ok"), (9000, "right", 6000)), 16000,
              presses((5500, "ok"), (7000, "ok"), (9500, "right", 2000)), 13000),
    # past the keys, the big cookie clicked three times; Home saves the bakery. Next time, from the copy, two more
    "cookieclicker": ("CookieClicker.nwa", "cookieclicker.sav",
                      presses((2500, "ok"), (3500, "ok"), (4000, "ok"), (4500, "ok")), 6500,
                      presses((3000, "ok"), (3600, "ok")), 5500),
    # no games in its table: the welcome; Backspace, Options, Scaling from Fit to Full, back; next time to 1x
    "retrogo": ("RetroGo.nwa", "retrogo.cfg",
                presses((1500, "backspace"), (2200, "ok"), (2900, "right"), (3500, "back")), 4500,
                presses((1500, "backspace"), (2200, "ok"), (2900, "right"), (3500, "back")), 4500),
    # the title, how to play, then the run: a second of it, paused (that saves it); Home keeps it. Next time, from the
    # copy, it opens paused: Back, another second, paused again, quit with On/Off
    "jetpackjoyride": ("JetpackJoyride.nwa", "jetpackjoyride.sav", presses((1500, "ok"), (2500, "ok"), (3600, "back")), 5000,
                       presses((1500, "back"), (2700, "back")), 4000),
    # the title, how to play (the first time), then level 1: the stickman falls and swings; Home keeps the level.
    # Next time, from the copy, Play goes back to it, then Back pauses it; quit with On/Off
    "stickmanhook": ("StickmanHook.nwa", "stickmanhook.sav", presses((1500, "ok"), (2500, "ok"), (3300, "ok", 700)), 5000,
                     presses((1500, "ok"), (2700, "back")), 4000),
    # the title, how to play, then the game: a second of Kong's climb, paused (that saves it); Home keeps it. Next
    # time, from the copy, it opens paused: Back, a second more, paused again, quit with On/Off
    "donkeykong": ("DonkeyKong.nwa", "donkeykong.sav", presses((1500, "ok"), (2500, "ok"), (3600, "back")), 5000,
                   presses((1500, "back"), (2700, "back")), 4000),
    # the title, Play, the first press (the guide goes) and the ball rolls; Home keeps the run. Next time, from the
    # copy, it opens paused: Back, and it rolls on
    "dune": ("Dune.nwa", "dune.sav", presses((1500, "ok"), (2600, "ok")), 5000, presses((1500, "back")), 3500),
    # a mart past its tutorial (seeded): the title, the upgrades, your free stack bought (that saves); next time, from
    # the copy, the next level for 100 coins
    "monkeymart": ("MonkeyMart.nwa", "monkeymart.sav", presses((1500, "ok"), (2500, "ok"), (3200, "ok"), (3900, "back")),
                   5000, presses((1500, "ok"), (2500, "ok"), (3200, "ok"), (3900, "back")), 5000),
    # the title, Arcade, the guide (the first time), then the climb: two dashes, paused (that saves); Home keeps the
    # run. Next time, from the copy, Arcade starts at once: a dash, paused, quit with On/Off
    "tombofthemask": ("TombOfTheMask.nwa", "tombofthemask.sav",
                      presses((1500, "ok"), (2500, "ok"), (3200, "up"), (3800, "left"), (4400, "back")), 5500,
                      presses((1500, "ok"), (2500, "up"), (3200, "back")), 4500),
    # the title, New game, Deal, how to play (the first time: Back skips it), then the table: the bots play; Home
    # keeps the game. Next time, from the copy, Continue: back at the table, quit with On/Off
    "uno": ("UNO.nwa", "uno.sav", presses((1500, "ok"), (2200, "ok"), (2900, "back")), 6000,
            presses((1500, "ok")), 4000),
    # the title, Story Mode, Week 1, how to play (the first time: Back skips it), then Bopeebo plays; Home keeps the
    # guide read. Next time, from the copy: Options, downscroll on (Back saves it), quit with On/Off
    "fnf": ("FridayNightFunkin.nwa", "fnf.sav", presses((1500, "ok"), (2200, "ok"), (2900, "ok"), (3600, "back")), 7000,
            presses((1500, "ok"), (2200, "down"), (2700, "down"), (3200, "ok"), (3900, "ok"), (4600, "back")), 6000),
    # the title's Play, Start, the first-run guide's OK, then the race: flat out, Back to pause, Home. Next time,
    # from the copy, Continue: the race again (after a 3, 2, 1), quit with On/Off
    "turborush": ("TurboRush.nwa", "turborush.sav", presses((1500, "ok"), (2200, "ok"), (2900, "ok"), (9500, "back"))
                  + [(3500, 9000, K["up"])], 11000, presses((1500, "ok")) + [(3500, 6500, K["up"])], 7500),
    # the title, Poached Eggs, its first level, the story, how to play (the first time), then a bird launched; Home
    # keeps the play time. Next time, from the copy, the same level without the guide, quit with On/Off
    "angrybirds": ("AngryBirds.nwa", "angrybirds.sav",
                   presses((1500, "ok"), (2200, "ok"), (2900, "ok"), (3600, "ok"), (4300, "ok"), (5500, "ok")), 9000,
                   presses((1500, "ok"), (2200, "ok"), (2900, "ok"), (3600, "ok")), 6000),
    # the title's Play, the first track, the first-run guide's three pages, then the race: flat out, Back to pause,
    # Home. Next time, from the copy, Play and the track straight into the race (the guide was read), quit with On/Off
    "polytrack": ("PolyTrack.nwa", "polytrack.sav",
                  presses((1500, "ok"), (2200, "ok"), (2900, "ok"), (3600, "ok"), (4300, "ok"), (9500, "back"))
                  + [(5000, 9000, K["up"])], 11000, presses((1500, "ok"), (2200, "ok")) + [(3000, 6500, K["up"])], 7500),
    # how to play (the first time: OK starts), then Make Paperclip held, Back to pause (that saves); Home keeps the
    # clips. Next time, from the copy, no guide: a clip more, quit with On/Off
    "paperclips": ("UniversalPaperclips.nwa", "paperclips.sav",
                   presses((1500, "ok"), (4500, "back")) + [(2200, 4000, K["ok"])], 6000,
                   presses((1500, "ok"), (2000, "ok")), 4000),
    # a level, its message, then play; then back to the level select
    "portal": ("PortalReturns.nwa", "portal.sav",
               presses((1500, "ok"), (2500, "ok"), (3500, "ok")) + every(4000, 7000, 400, "right", 300), 8000,
               presses((1500, "ok")), 4000),
}


# saves already on the calculator, and what each session must leave in them
def coreball_save(level, best, flags=0):
    """coreball.sav: 'C', version 1, flags, level, best, 3 spare bytes, then FNV-1a of those 8"""
    v = bytes([ord("C"), 1, flags, level, best, 0, 0, 0])
    h = 0x811C9DC5
    for x in v:
        h = (h ^ x) * 0x01000193 & 0xFFFFFFFF
    return v + struct.pack("<I", h)


def monkeymart_save():
    """monkeymart.sav: "MMMT", version 1, its size (364), then the progress (save.h): 500 coins, the first hat, mart 1
    with its first six places bought (the tutorial over) and 40 in the cash box, every upgrade at level 1; FNV-1a of
    it all after"""
    p = bytearray(352)
    struct.pack_into("<IIBB", p, 0, 500, 1, 0, 0)
    p[10] = 6
    struct.pack_into("<I", p, 12, 40)
    for i in (6, 4, 0, 1, 5, 7):    # the cash box, the banana shelf, two palms, the corn shelf, a corn plant
        p[20 + i] = 1
    for i in range(72):
        p[278 + i] = 1
    v = b"MMMT" + struct.pack("<HH", 1, 364) + bytes(p)
    h = 0x811C9DC5
    for x in v:
        h = (h ^ x) * 0x01000193 & 0xFFFFFFFF
    return v + struct.pack("<I", h)


SEEDS = {
    "monkeymart": monkeymart_save(),
    # level 5 to play, 9 reached, how to play not read yet
    "coreball": coreball_save(5, 9),
    # the first level done and last played
    "numdrive": b"MD" + bytes([1] + [0] * 24) + b"\0\0\0",
    # puzzle rating 1234, a streak of 3, the bot Hugo, side 1, a 10-minute clock
    "chess": bytes([ord("C"), 1, 1234 & 255, 1234 >> 8, 3, 0, 7, 1 | 4 << 2]),
    # options (normal speed, 3 lives), best 9000, a game at level 2 with 5000 points, 3 lives, every dot
    "pacman": bytes([ord("P"), 1, 1, 2, 2, 1, 0, 0]) + struct.pack("<I", 9000) + bytes([1, 2, 3, 0])
              + struct.pack("<I", 5000) + bytes([255] * 30 + [15, 0]),
    # draw 1, Vegas, timed; 5 played, 2 won, a Vegas bank of -$100; no game in progress
    "solitaire": b"S\1\1\1\1\0\0\0" + bytes(8) + struct.pack("<6H3i2I", 5, 2, 1, 2, 200, 0, 1234, -100, 0, 0, 0)
                 + bytes(68),
    # 4x4 last played, with a game to continue: best 1000, score 500, 40 moves, a 2, a 4 and a 2
    "g2048": b"2\1\1" + bytes([0, 1, 0, 0, 0]) + struct.pack("<12I", 0, 1000, 0, 0, 0, 500, 0, 0, 0, 40, 0, 0)
             + bytes(9) + bytes([1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]) + bytes(61 + 2),
}
# games that keep a copy of their save in a Python script (the only files the
# NumWorks installer keeps): before the second session, as after installing
# the app again, only the scripts are left, and the save must come back
COPIES = {"championisland": "champion_saves.py", "numblocks": "numblocks_saves.py", "celeste": "celeste_saves.py",
          "hollowknight": "hollowknight_saves.py", "finalearth2": "finalearth2_saves.py",
          "numpowder": "numpowder_saves.py", "undertale": "undertale_saves.py", "subway": "subway_saves.py",
          "fnaf": "fnaf_saves.py", "doom": "doom_saves.py", "isaac": "isaac_saves.py",
          "hillclimb": "hillclimb_saves.py", "pvz": "pvz_saves.py", "sonic": "sonic_saves.py",
          "cookieclicker": "cookieclicker_saves.py", "leveldevil": "leveldevil_saves.py",
          "jetpackjoyride": "jetpackjoyride_saves.py", "stickmanhook": "stickmanhook_saves.py",
          "donkeykong": "donkeykong_saves.py", "dune": "dune_saves.py", "monkeymart": "monkeymart_saves.py",
          "tombofthemask": "tombofthemask_saves.py", "uno": "uno_saves.py",
          "fnf": "fnf_saves.py", "turborush": "turborush_saves.py",
          "angrybirds": "angrybirds_saves.py", "polytrack": "polytrack_saves.py", "paperclips": "paperclips_saves.py"}


def paperclips_ok(v):
    """Universal Paperclips' save: "UPC1", version 1, 3400 bytes, an FNV-1a sum of the rest at the end"""
    if len(v) != 3400 or v[:4] != b"UPC1" or struct.unpack_from("<HH", v, 4) != (1, 3400):
        return False
    h = 2166136261
    for x in v[:-4]:
        h = (h ^ x) * 16777619 & 0xFFFFFFFF
    return struct.unpack_from("<I", v, len(v) - 4)[0] == h


def polytrack_ok(v):
    """PolyTrack's save: "PTK1", version 1, 768 bytes, an FNV-1a sum of the rest at the end"""
    if len(v) != 768 or v[:4] != b"PTK1" or struct.unpack_from("<HH", v, 4) != (1, 768):
        return False
    h = 2166136261
    for b in v[:764]:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return struct.unpack_from("<I", v, 764)[0] == h


def dk_frames(v):
    """Donkey Kong's save: the frames since the game began (the arcade's difficulty clock, in the packed RAM)."""
    raw, i = bytearray(), 24
    while i < len(v) - 4 and len(raw) <= 0x384:
        c = v[i]
        i += 1
        if c < 128:
            raw += v[i:i + c + 1]
            i += c + 1
        else:
            raw += bytes([v[i]]) * (c - 126)
            i += 1
    return raw[0x381] * 256 + raw[0x384] if len(raw) > 0x384 else -1


CHECKS = {
    # the island remembers where Lucky was; after the reinstall, from the copy
    "championisland": (lambda v: v[:3] == b"CI1" and b"PLAYER_LOC" in v,) * 2,
    "numdrive": (lambda v: v[27] == 1, lambda v: v[27] == 0),  # last played level
    # a session saved (has_session at 1072, the play time at 1096); after the reinstall, from the copy, it goes on
    "celeste": (lambda v: v[:4] == b"SLEC" and v[1072] == 1 and struct.unpack_from("<I", v, 1096)[0] > 0,
                lambda v: v[:4] == b"SLEC" and v[1072] == 1 and struct.unpack_from("<I", v, 1096)[0] > 60),
    "chess": (lambda v: v == SEEDS["chess"],) * 2,  # read back and kept
    # a city saved ("FE2S", version 1); after the reinstall, from the copy, it goes on and is saved again
    "finalearth2": (lambda v: v[:4] == b"FE2S" and v[4] == 1,) * 2,
    # the moment kept ("UT01", the frames played at 52); after the reinstall, from the copy, it goes on
    "undertale": (lambda v: v[:4] == b"UT01" and struct.unpack_from("<I", v, 52)[0] > 0,
                  lambda v: v[:4] == b"UT01" and struct.unpack_from("<I", v, 52)[0] > 60),
    # the progress ("SUB1"): a run played, its coins banked (offset 8), runs at 20; then a second run
    "subway": (lambda v: v[:4] == b"SUB1" and struct.unpack_from("<I", v, 8)[0] > 0 and struct.unpack_from("<I", v, 20)[0] == 1,
               lambda v: v[:4] == b"SUB1" and struct.unpack_from("<I", v, 20)[0] == 2),
    # the night kept ("FNAF", its seconds at 40); after the reinstall, from the copy, it goes on
    "fnaf": (lambda v: v[:4] == b"FNAF" and struct.unpack_from("<f", v, 40)[0] > 2,
             lambda v: v[:4] == b"FNAF" and struct.unpack_from("<f", v, 40)[0] > 6.5),
    # the moment kept (a save named "the moment"); after the reinstall, from the copy, loaded and kept again
    "doom": (lambda v: v[:10] == b"the moment",) * 2,
    # the run kept ("ISR1", the frames played at 16); after the reinstall, from the copy, it goes on
    "isaac": (lambda v: v[:4] == b"ISR1" and struct.unpack_from("<I", v, 16)[0] > 30,
              lambda v: v[:4] == b"ISR1" and struct.unpack_from("<I", v, 16)[0] > 80),
    # the level kept ("PVZL", level 1-1 at 8, its ticks at 16, a plant on it); after the reinstall, from the copy, it
    # goes on
    "pvz": (lambda v: v[:4] == b"PVZL" and v[8] == 1 and struct.unpack_from("<I", v, 16)[0] > 0,
            lambda v: v[:4] == b"PVZL" and v[8] == 1 and struct.unpack_from("<I", v, 16)[0] > 500),
    # a game kept ("S1SV": has_game at 6, Green Hill at 7, the frames played at 28); after the reinstall, from the
    # copy, it goes on
    "sonic": (lambda v: v[:4] == b"S1SV" and v[6] == 1 and v[7] == 0 and struct.unpack_from("<I", v, 28)[0] > 0,
              lambda v: v[:4] == b"S1SV" and v[6] == 1 and struct.unpack_from("<I", v, 28)[0] > 600),
    # a simulation saved ("NPWD"); after the reinstall, from the copy, it goes on and is saved again
    "numpowder": (lambda v: v[:4] == b"NPWD",) * 2,
    # a run banked ("HCR1", save version 3 at 8, runs at 416) with a best on the Countryside (at 72); after the
    # reinstall, from the copy, a second one
    "hillclimb": (lambda v: v[:4] == b"HCR1" and struct.unpack_from("<H", v, 8)[0] == 3
                  and struct.unpack_from("<I", v, 416)[0] == 1 and struct.unpack_from("<H", v, 72)[0] > 0,
                  lambda v: v[:4] == b"HCR1" and struct.unpack_from("<I", v, 416)[0] == 2),
    # a game saved ("HKSV", then the play time at 152); after the reinstall, from the copy, it goes on
    "hollowknight": (lambda v: v[:4] == b"HKSV" and struct.unpack_from("<f", v, 152)[0] > 0,
                     lambda v: v[:4] == b"HKSV" and struct.unpack_from("<f", v, 152)[0] > 10),
    "numvisuals": (lambda v: v[:2] == b"V\1" and v[3] == 4 and struct.unpack_from("<i", v, 8)[0] == 2,
                   lambda v: struct.unpack_from("<i", v, 8)[0] == 3),  # the add-on and its count
    # a run kept (guide read), the ball on its way; then further along the same run
    "flappydunk": (lambda v: len(v) == 200 and v[:2] == b"F\1" and v[2] & 3 == 3 and struct.unpack_from("<f", v, 24)[0] > 1,
                   lambda v: v[2] & 1 and struct.unpack_from("<f", v, 24)[0] > 6),
    # a game kept (guide read) with fruit in the box; then one more
    "suika": (lambda v: len(v) == 1052 and v[:2] == b"S\1" and v[2] & 3 == 3 and sum(v[42 + 16 * i] > 0 for i in range(64)) >= 1,
              lambda v: v[2] & 1 and sum(v[42 + 16 * i] > 0 for i in range(64)) >= 2),
    # read back and kept, the guide read; then level 7 picked in the list
    "coreball": (lambda v: v == coreball_save(5, 9, 2),
                 lambda v: v == coreball_save(7, 9, 2)),
    # a tower kept (guide read) with one block placed; then kept as it was
    "towerblock": (lambda v: len(v) == 668 and v[:2] == b"T\1" and v[2] & 3 == 3 and struct.unpack_from("<I", v, 12)[0] == 1,
                   lambda v: v[2] & 1 and struct.unpack_from("<I", v, 12)[0] == 1),
    # a climb kept (guide read) with some height; then still there
    "doodlejump": (lambda v: len(v) == 488 and v[:2] == b"D\1" and v[2] & 3 == 3 and struct.unpack_from("<I", v, 12)[0] > 40,
                   lambda v: v[2] & 1 and struct.unpack_from("<I", v, 12)[0] > 40),
    "flappybird": (lambda v: len(v) == 26 and v[:3] == b"F\1\1" and v[8] == 3,  # Custom, Speed Fast
                   lambda v: v[2] == 1 and v[8] == 4),  # read back, Speed Insane
    # the game went on (more points, fewer dots), then Fast was chosen and the game kept
    "pacman": (lambda v: v[:2] == b"P\1" and v[12:14] == bytes([1, 2]) and struct.unpack_from("<I", v, 16)[0] > 5000
               and struct.unpack_from("<I", v, 8)[0] == 9000 and sum(bin(b).count("1") for b in v[20:52]) < 244,
               lambda v: v[2] == 2 and v[12:14] == bytes([1, 2]) and struct.unpack_from("<I", v, 16)[0] > 5000),
    # the default options and a best of 1 (Classic, 1 fruit, normal speed and size); then a banana
    "snake": (lambda v: len(v) == 658 and v[:9] == b"S\2" + bytes(7) and struct.unpack_from("<H", v, 10)[0] == 1,
              lambda v: len(v) == 658 and v[2] == 1 and struct.unpack_from("<H", v, 10)[0] == 1),
    "connectfour": (lambda v: v == b"C\1\1\2\1\0\5\1" + bytes(8),  # vs computer, 2 players, Normal, light
                    lambda v: v[5] == 1 and v[6] == 5),  # dark now; the tally's settings read back
    # the seeded options made a Vegas draw-1 game (-$52), counted as played; two moves, then three
    "solitaire": (lambda v: len(v) == 116 and v[:4] == b"S\1\1\1" and v[6] == 1 and v[8:10] == b"\1\1"
                  and struct.unpack_from("<2H", v, 16) == (6, 2) and struct.unpack_from("<H", v, 26)[0] == 2
                  and struct.unpack_from("<2i", v, 32) == (-100, -52),
                  lambda v: v[6] == 1 and struct.unpack_from("<H", v, 16)[0] == 6
                  and struct.unpack_from("<H", v, 26)[0] == 3),
    # continued (moves 40 + 2, then + 3), still in progress, score and best kept or higher
    "g2048": (lambda v: v[:3] == b"2\1\1" and v[4] & 1 and struct.unpack_from("<I", v, 44)[0] == 42
              and struct.unpack_from("<I", v, 28)[0] >= 500 and struct.unpack_from("<I", v, 12)[0] >= 1000,
              lambda v: v[4] & 1 and struct.unpack_from("<I", v, 44)[0] == 45),
    # a Beginner game kept (9x9, 10 mines, cursor moved right), one game played; then two
    "minesweeper": (lambda v: len(v) == 276 and v[:2] == b"M\1" and v[32:34] == bytes([9, 9]) and v[28] == 10
                    and v[34] == 5 and struct.unpack_from("<H", v, 14)[0] == 1,
                    lambda v: v[32] == 9 and struct.unpack_from("<H", v, 14)[0] == 2),
    # the world's record: its magic, then (offset 16) the time of day, which goes on from one session to the next
    "numblocks": (lambda v: v[:4] == b"NBW3" and struct.unpack_from("<I", v, 16)[0] > 0,
                  lambda v: v[:4] == b"NBW3" and struct.unpack_from("<I", v, 16)[0] > 60),
    # a game kept (guide read) with a block's points, the best as high; then a second block's
    "blockblast": (lambda v: len(v) == 92 and v[:3] == b"B\1\3" and struct.unpack_from("<I", v, 12)[0] >= 1
                   and struct.unpack_from("<I", v, 8)[0] >= struct.unpack_from("<I", v, 12)[0],
                   lambda v: v[2] & 1 and struct.unpack_from("<I", v, 12)[0] >= 2),
    # the runner died (the deaths at 8); after the reinstall, from the copy, it died again
    "leveldevil": (lambda v: len(v) == 48 and v[:2] == b"L\1" and struct.unpack_from("<I", v, 8)[0] >= 1,
                   lambda v: struct.unpack_from("<I", v, 8)[0] >= 2),
    # the best score ("I", version 1, the best at 4): some invaders shot; then kept, or beaten
    "invaders": (lambda v: len(v) == 8 and v[:2] == b"I\1" and struct.unpack_from("<I", v, 4)[0] >= 20,
                 lambda v: len(v) == 8 and v[:2] == b"I\1" and struct.unpack_from("<I", v, 4)[0] >= 20),
    # how to play read, the game's moves (at 32) kept: 40 then 36; then 2 more
    "uttt": (lambda v: len(v) == 114 and v[:2] == b"U\1" and v[4] & 1 and v[5] == 2 and v[32:34] == bytes([40, 36]),
             lambda v: v[5] == 3 and v[34] == 2),
    # how to play seen and the first level played last; then the second
    "sokoban": (lambda v: len(v) == 314 and v[:4] == b"S\1\1\0", lambda v: len(v) == 314 and v[:4] == b"S\1\1\1"),
    # the bakery kept ("CCK1", the cookies at 8, the clicks at 48): 3 clicks; after the reinstall, from the copy, 5
    "cookieclicker": (lambda v: v[:4] == b"CCK1" and struct.unpack_from("<d", v, 48)[0] == 3
                      and struct.unpack_from("<d", v, 8)[0] >= 3,
                      lambda v: v[:4] == b"CCK1" and struct.unpack_from("<d", v, 48)[0] == 5),
    # a run kept ("JJR1", has_run at 23, its frames at 60); after the reinstall, from the copy, it goes on
    "jetpackjoyride": (lambda v: len(v) == 1200 and v[:4] == b"JJR1" and v[23] == 1 and struct.unpack_from("<I", v, 60)[0] >= 30,
                       lambda v: v[:4] == b"JJR1" and v[23] == 1 and struct.unpack_from("<I", v, 60)[0] >= 90),
    # how to play seen, level 1 being played ("SHK3", guide_seen at 17, has_course at 18, the course at 20); after the
    # reinstall, from the copy, the same level and course
    "stickmanhook": (lambda v: len(v) == 28 and v[:4] == b"SHK3" and v[17] == 1 and v[18] == 1 and v[20] == 0
                     and v[21] == 255,) * 2,
    # a game kept ("DKG1", the game at 23, one game played) a second in; after the reinstall, from the copy, it goes on
    "donkeykong": (lambda v: v[:4] == b"DKG1" and v[23] == 1 and v[12] == 1 and dk_frames(v) >= 30,
                   lambda v: v[:4] == b"DKG1" and v[23] == 1 and v[12] == 1 and dk_frames(v) >= 90),
    # a run kept ("DUNE", has_run at 27, the run's frames at 328); after the reinstall, from the copy, it goes on
    "dune": (lambda v: len(v) == 1548 and v[:4] == b"DUNE" and v[27] == 1 and struct.unpack_from("<I", v, 328)[0] >= 200,
             lambda v: len(v) == 1548 and v[:4] == b"DUNE" and v[27] == 1 and struct.unpack_from("<I", v, 328)[0] >= 300),
    # your stack bought (level 2, upgrades_level at 287), the coins kept; after the reinstall, from the copy, level 3
    # for 100 coins
    "monkeymart": (lambda v: len(v) == 364 and v[:4] == b"MMMT" and v[287] == 2 and struct.unpack_from("<I", v, 8)[0] >= 500,
                   lambda v: len(v) == 364 and v[:4] == b"MMMT" and v[287] == 3 and struct.unpack_from("<I", v, 8)[0] >= 400),
    # "TOTM", version 1, its size (296): the guide read (guide at 288) and the arcade played once (the mission's
    # progress at 268); after the reinstall, from the copy, twice
    "tombofthemask": (lambda v: len(v) == 296 and v[:4] == b"TOTM" and v[288] & 1 and struct.unpack_from("<I", v, 268)[0] == 1,
                      lambda v: len(v) == 296 and v[:4] == b"TOTM" and struct.unpack_from("<I", v, 268)[0] == 2),
    # "UNO1", version 1, its size (728): how to play shown (guide at 12) and a game going (has_game at 13), four at
    # the table (16) in its first hand (hands at 682); after the reinstall, from the copy, the same game goes on
    "uno": (lambda v: len(v) == 728 and v[:4] == b"UNO1" and v[12] == 1 and v[13] == 1 and v[16] == 4
            and struct.unpack_from("<H", v, 682)[0] == 1,) * 2,
    # "FNFS", version 1, its size (1552): how to play read (guide at 9); after the reinstall, from the copy, downscroll
    # on (at 8), the guide still read
    "fnf": (lambda v: len(v) == 1552 and v[:4] == b"FNFS" and v[4] == 1 and v[9] == 1 and v[8] == 0,
            lambda v: len(v) == 1552 and v[:4] == b"FNFS" and v[9] == 1 and v[8] == 1),
    # "TRU1", version 1, its size (492): the guide read (85), a race kept (114) and going (phase at 127), its clock
    # (164) running; after the reinstall, from the copy, the same race goes on
    "turborush": (lambda v: len(v) == 492 and v[:4] == b"TRU1" and v[85] == 1 and v[114] == 1 and v[127] == 1
                  and struct.unpack_from("<I", v, 164)[0] >= 1000,
                  lambda v: len(v) == 492 and v[:4] == b"TRU1" and v[114] == 1 and v[127] == 1
                  and struct.unpack_from("<I", v, 164)[0] >= 4000),
    # "ABS1", version 1, its size (3032): how to play shown (guide at 3014), the first level last played (3012), some
    # seconds of play (3024); after the reinstall, from the copy, more of them
    "angrybirds": (lambda v: len(v) == 3032 and v[:4] == b"ABS1" and v[3014] == 1
                   and struct.unpack_from("<H", v, 3012)[0] == 0 and struct.unpack_from("<I", v, 3024)[0] >= 2,
                   lambda v: len(v) == 3032 and v[:4] == b"ABS1" and v[3014] == 1
                   and struct.unpack_from("<I", v, 3024)[0] >= 4),
    # "PTK1", version 1, its size (768): the guide read (763), Summer 1 chosen (760), its checksum good (FNV-1a at 764);
    # after the reinstall, from the copy, the same
    "polytrack": (lambda v: polytrack_ok(v) and v[763] == 1 and v[760] == 0,
                  lambda v: polytrack_ok(v) and v[763] == 1),
    # "UPC1", version 1, its size (3400): how to play shown (guide at 8), clips made (the first number, at 12); after the
    # reinstall, from the copy, one more
    "paperclips": (lambda v: paperclips_ok(v) and v[8] == 1 and struct.unpack_from("<d", v, 12)[0] >= 10,
                   lambda v: paperclips_ok(v) and v[8] == 1 and struct.unpack_from("<d", v, 12)[0] >= 11),
    "breakout": (lambda v: len(v) == 12 and v[:2] == b"B\2" and v[4] == 1 and struct.unpack_from("<I", v, 8)[0] >= 10,
                 lambda v: v[4] == 0 and struct.unpack_from("<I", v, 8)[0] >= 1),  # best kept, shake off
    # Air Hockey and the easy computer chosen; then the normal computer and Tanks, the tallies of the six games kept
    "twoplayer": (lambda v: len(v) == 52 and v[:4] == b"P\1\1\1",
                  lambda v: len(v) == 52 and v[:4] == b"P\1\2\2"),
    # a game kept (Easy, the guide read) with the two digits the hints wrote (the digits at 109); then three
    "sudoku": (lambda v: len(v) == 352 and v[:4] == b"S\1\3\0" and sum(1 <= b <= 9 for b in v[109:190]) == 2,
               lambda v: v[2] & 1 and sum(1 <= b <= 9 for b in v[109:190]) == 3),
    # the scaling (third byte): Full, then 1x
    "retrogo": (lambda v: len(v) == 19 and v[:2] == b"R\1" and v[2] == 2, lambda v: v[2] == 0),
}


def records(buf):
    out, p = [], 0
    while p + 2 <= len(buf):
        n, = struct.unpack_from("<H", buf, p)
        if n == 0:
            return out
        if n < 4 or p + n > len(buf):
            raise ValueError(f"corrupt record at {p}")
        name = buf[p + 2:buf.index(b"\0", p + 2)].decode("latin1")
        out.append((name, bytes(buf[p + 3 + len(name):p + n])))
        p += n
    raise ValueError("no end of the record list")


def through_launcher(index, keys, home, start=800):
    """The same session, started from NumPlay's carousel (game `index`); the game's keys come `start` ms
    after OK on its card."""
    lead = [(1200 + 300 * i, 1290 + 300 * i, K["right"]) for i in range(index)]
    ok = 1500 + 300 * index
    lead.append((ok, ok + 90, K["ok"]))
    shift = ok + start
    return lead + [(a + shift, b + shift, k) for a, b, k in keys], home + shift


def run_once(nwa, storage, keys, home, out, tag, quit_key="home"):
    c = emu.Calculator(nwa, storage_file=storage)
    c.enable_checks(reads=True)
    c.keys = keys + [(home, home + 400, K[quit_key])]
    c.pending_shots = [(home - 100, os.path.join(out, f"{tag}.png"))]
    c.run(home + 3000)
    problems = []
    if not c.exited:
        problems.append(f"did not quit on {'Home' if quit_key == 'home' else 'On/Off'}")
    if c.violations:
        problems += sorted(set(c.violations))[:8]
    if not c.app_ram_clean():
        problems.append("RAM not cleared on exit")
    if c.max_locks != 1 or c.locks != 0:
        problems.append(f"Home lock not held and released once (max {c.max_locks}, now {c.locks})")
    changed = c.firmware_ram_changes()
    if changed:
        problems.append(f"{len(changed)} words of the firmware's RAM changed, first at {changed[0]:#x}")
    stack = c.stack_used()
    if stack > 30 * 1024:
        problems.append(f"stack use {stack} bytes, close to the 32 KB limit")
    return c, problems, stack


def play(name, path, out, index=None, copy=None, tag=None, start=800):
    """The two sessions of a game, in the app at path (a NumPlay when index, the game's place in its
    carousel, is given; start: when its keys begin after OK on its card). copy: the script that holds the
    game's own copy of its saves: only the scripts are left before the second session, as after installing
    the app again. The problems, the stack used and the files after the first session."""
    nwa, save, keys1, home1, keys2, home2 = GAMES[name]
    tag = tag or name
    if index is not None:  # Home in a game quits NumPlay altogether
        keys1, home1 = through_launcher(index, keys1, home1, start)
        keys2, home2 = through_launcher(index, keys2, home2, start)
    with tempfile.TemporaryDirectory() as d:
        storage = os.path.join(d, "storage.bin")
        # the calculator already has a script
        pre = [("pi.py", b"\x01print(3.14159)\n")]
        if name in SEEDS:
            pre.append((save, SEEDS[name]))
        buf = bytearray(emu.STORAGE_SIZE)
        p = 0
        for rn, content in pre:
            size = 2 + len(rn) + 1 + len(content)
            struct.pack_into("<H", buf, p, size)
            buf[p + 2:p + size] = rn.encode() + b"\0" + content
            p += size
        open(storage, "wb").write(buf)
        c, problems, stack = run_once(path, storage, keys1, home1, out, f"{tag}_1")
        recs = dict(records(open(storage, "rb").read()))
        if recs.get("pi.py") != pre[0][1]:
            problems.append("pi.py changed")
        if save and save not in recs:
            problems.append(f"{save} not written")
        elif name in CHECKS and not CHECKS[name][0](recs[save]):
            problems.append(f"{save} not updated")
        # a script added after the save, then a second session
        buf = bytearray(open(storage, "rb").read())
        lst = records(buf)
        if copy:
            text = dict(lst).get(copy, b"")
            mark = b"#>" + save.encode() + b":"
            line = text[text.find(mark) + len(mark):].split(b"\n")[0] if mark in text else b""
            if not line or __import__("base64").b64decode(line) != dict(lst).get(save):
                problems.append(f"{copy} does not hold {save}")
            lst = [(n, v) for n, v in lst if n.endswith(".py")]
            buf = bytearray(emu.STORAGE_SIZE)
            q = 0
            for n, v in lst:
                struct.pack_into("<H", buf, q, 2 + len(n) + 1 + len(v))
                buf[q + 2:q + 3 + len(n) + len(v)] = n.encode() + b"\0" + v
                q += 2 + len(n) + 1 + len(v)
        p = sum(2 + len(n) + 1 + len(v) for n, v in lst)
        late = b"\x01print('later')\n"
        size = 2 + len("late.py") + 1 + len(late)
        struct.pack_into("<H", buf, p, size)
        buf[p + 2:p + size] = b"late.py\0" + late
        struct.pack_into("<H", buf, p + size, 0)
        open(storage, "wb").write(buf)
        # (quitting with On/Off this time: it saves and leaves as Home does)
        c2, problems2, stack2 = run_once(path, storage, keys2, home2, out, f"{tag}_2", "onoff")
        problems += [f"second run: {x}" for x in problems2]
        try:
            recs2 = dict(records(open(storage, "rb").read()))
            if recs2.get("pi.py") != pre[0][1] or recs2.get("late.py") != late:
                problems.append("second run: other files changed")
            if save and save not in recs2:
                problems.append(f"second run: {save} lost")
            elif name in CHECKS and not CHECKS[name][1](recs2[save]):
                problems.append(f"second run: {save} not updated")
        except ValueError as e:
            problems.append(f"second run: file system corrupt ({e})")
    return problems, max(stack, stack2), recs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("apps")
    ap.add_argument("--game")
    ap.add_argument("--out", default="build/test_games")
    ap.add_argument("--numplay", help="play the games inside this NumPlay.nwa instead")
    a = ap.parse_args()
    order = [g["id"] for g in json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "games",
                                                          "games.json"))) if g.get("default", True)]
    os.makedirs(a.out, exist_ok=True)
    failed = False
    for name in GAMES:
        if a.game and a.game != name or a.numplay and name not in order:  # (a big game: on its own only)
            continue
        if a.numplay:  # (NumPlay keeps its own copy, packed: tests/test_progress.py)
            problems, stack, recs = play(name, a.numplay, a.out, order.index(name))
        else:
            problems, stack, recs = play(name, os.path.join(a.apps, GAMES[name][0]), a.out, copy=COPIES.get(name))
        status = "ok" if not problems else "FAIL"
        failed |= bool(problems)
        print(f"{name:11s} {status}  stack {stack} bytes, files {sorted(recs)}", flush=True)
        for x in problems:
            print("   ", x)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
