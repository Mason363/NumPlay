> **NumDash is part of [NumPlay](../../README.md)**, every NumWorks game in one app. Get it with all the others, or on its own as `NumDash.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

# NumDash

**Geometry Dash on your NumWorks calculator.**

NumDash is a fan-made version of Geometry Dash that runs right on a NumWorks graphing calculator. Jump over spikes, fly the ship, flip the ball, collect secret coins and try to beat eighteen levels of the real game, from Stereo Madness through Dash and the Geometry Dash Subzero and Meltdown levels, all with the calculator's keys.

### [⬇ Download NumDash.nwa](https://github.com/Mason363/NumPlay/releases/latest/download/NumDash.nwa)

<img src="docs/screenshots/menu.png" width="640" alt="The NumDash main menu">

## How to install it

You only need the calculator, its USB cable and a computer with Chrome or Edge.

1. **Download** [NumDash.nwa](https://github.com/Mason363/NumPlay/releases/latest/download/NumDash.nwa) (the button above).
2. **Plug** your calculator into the computer with the USB cable.
3. **Open** [my.numworks.com/apps](https://my.numworks.com/apps) and follow the steps to send the file to your calculator.
4. **Play:** on the calculator, press the Home key and open **NumDash**.

That's it. Your progress is saved on the calculator, so you can close the game and come back later.

> NumDash is made for the **NumWorks N0120** (the newest model). The website above tells you which model you have.

## What's inside

### The real levels

Stereo Madness, Back on Track, Polargeist, Dry Out, Base After Base, Can't Let Go, Jumper, Time Machine and Cycles, with the same obstacles, colour changes and 3 hidden coins in each one.

### Subzero and Meltdown

Press Start, Nock Em, Power Trip, The Seven Seas, Viking Arena and Airborne Robots are included after the twelve original main-game levels.

<img src="docs/screenshots/level-select.png" width="320" alt="Picking a level"> <img src="docs/screenshots/gameplay.png" width="320" alt="Jumping through Stereo Madness">

### Clubstep, Deadlocked and Dash

The two demons and the newest level come after Cycles, each with its 3 coins. They bring the UFO, the wave, the robot, the spider and the swing, mini and dual sections, teleports, slopes, moving and spinning obstacles, and in Dash the whole level turning around you.

<img src="docs/screenshots/clubstep.png" width="320" alt="Clubstep"> <img src="docs/screenshots/deadlocked.png" width="320" alt="Deadlocked">

<img src="docs/screenshots/dash.png" width="320" alt="Dash"> <img src="docs/dash.gif" width="320" alt="The start of Dash: fire, pixel art and a turning level">

### It plays like the original

The jump, the gravity, every game mode and every pad and orb behave like they do in Geometry Dash, so the timing you already know works here too.

<img src="docs/screenshots/ship.png" width="320" alt="Flying the ship"> <img src="docs/screenshots/pause.png" width="320" alt="The pause menu">

### Practice mode

Pause the game and pick the green diamond to practice. Press **0** to drop a checkpoint and you'll restart from there instead of from the beginning.

### The level complete moment

Beat a level and you get the light rays, the fireworks, the big "Level Complete!" and your stats, just like the game.

<img src="docs/screenshots/level-complete.png" width="320" alt="Level complete"> <img src="docs/screenshots/results.png" width="320" alt="Your results after beating a level">

### Make it yours

Pick your icon colours in the icon kit, change settings like the progress bar or low detail mode, and build your own levels in the level editor (3 save slots).

<img src="docs/screenshots/icon-kit.png" width="320" alt="The icon kit"> <img src="docs/screenshots/editor.png" width="320" alt="The level editor">

## Controls

| Key | What it does |
| --- | --- |
| **OK**, **EXE** or **Up** | Jump. Hold it to keep jumping, to fly up in the ship, or to flip the ball |
| **Back** | Pause the game, or go back in menus |
| **Arrows** | Move around the menus |
| **0** | Place a checkpoint (in practice mode) |
| **Backspace** | Remove your last checkpoint |
| **Home** | Save and quit |

The **?** button on the main menu shows these again. For the level editor, **KEYS** on the My Levels screen draws the keyboard with what each key does.

## Good to know

- **There is no music.** NumWorks calculators have no speaker. Objects still pulse to the beat of each song.
- **Your old progress is kept.** If you played an older version of NumDash, your progress on the first four levels and your custom levels carry over.
- **It won't mess with your other files.** NumDash saves into a few small files of its own and never touches your Python scripts or anything else.
- NumDash is a free fan project, inspired by Geometry Dash and not affiliated with RobTop Games. All the graphics were redrawn from scratch to look like the game.

## Thanks

Inspired by Geometry Dash. Not affiliated with RobTop Games.

- **[gd3ds](https://github.com/AleFunky/gd3ds)** by AleFunky and friends, a Geometry Dash remake for the Nintendo 3DS, whose research into how the game works made this possible.
- **[gdsolver](https://github.com/gdsolver/gdsolver)** (MIT), whose measurements of Geometry Dash 2.2 shaped the newer game modes, triggers and turning levels.
- **[GDRWeb](https://github.com/iliasHDZ/GDRWeb)** by IliasHDZ (MIT), a Geometry Dash level renderer, used to check the redrawn objects against the originals.
- **[gmdkit](https://github.com/UHDanke/gmdkit)** by HDanke (MIT), where the level data of Clubstep, Deadlocked and Dash comes from.
- The **Rammetto One** and **Nunito** fonts, both free under the SIL Open Font License (see the [LICENSES](LICENSES) folder).

Made by Mason Chen as part of NumPlay.

## For developers

You need `make`, `arm-none-eabi-gcc`, Node.js and, for the desktop version, SDL2.

```sh
npm ci
make build check   # builds build/numdash.nwa
make test          # tests, level replays and random-input tests
make run           # desktop version (arrow keys, Space to jump, Esc to go back)
```

`make assets` and `make levels` rebuild the graphics and level data (Python 3 with numpy and Pillow).
