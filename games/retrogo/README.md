# Retro-Go

Play your own Game Boy, Game Boy Color, NES, Master System and Game Gear games on the NumWorks calculator. Retro-Go is the launcher and emulators of handhelds like the ODROID-GO, whose screen is the calculator's size; this is its look and its menus, made for the keypad.

<img src="docs/shot.png" width="320" alt="Retro-Go's list of NES games, with Thwaite chosen">

You pick the games on NumPlay's customizer website: it puts them inside your NumPlay (or inside Retro-Go on its own), and they stay in the calculator's memory for apps. Only games you own or that are free to share, like the homebrew games in the pictures here.

- **Retro-Go's launcher:** a tab for each system with its own colours, the game list, and for each game Resume game, New game, Delete save and Properties.
- **Game Boy and Game Boy Color** with gnuboy, the emulator Retro-Go started from: the cartridges with MBC1, MBC2, MBC3 (with its clock), MBC5, HuC1 and HuC3 bank controllers, up to 32 KB of cartridge RAM. Big games fit too, like the 2 MB Game Boy Color ones, in a NumPlay made for Epsilon 26 or Nwagra (NumPlay Studio says when a game needs it).
- **NES** with nofrendo, Retro-Go's NES emulator at its start: 34 mappers, among them NROM, MMC1, UNROM, CNROM, MMC3, MMC5, AxROM, MMC2, Namco 163 and the Konami VRC chips.
- **Master System and Game Gear** with a machine written for NumPlay around superzazu's Z80: Sega's, Codemasters' and the Korean mappers, the cartridges' RAM, the 224-line pictures, the European games that need 50 Hz, and the Game Gear games made for the Master System's mode.
- **Full speed** on every calculator: as Retro-Go does, a frame is drawn, then at least one is skipped, more when the calculator can't keep up, so the game always runs at its own speed.
- **Retro-Go's menu** in a game: Save & Continue, Save & Quit, Load game, Reset, Options, Controls, About and Quit.
- **4 save slots** for each game, and the cartridges' own battery saves, kept a few seconds after the game writes them. Home saves and quits.
- **Options:** the picture at 1x, Fit or Full screen, the speed from 0.5x to 2.5x, the Game Boy's DMG, Pocket, Light, GBC or SGB colours, the NES's Nofrendo or NTSC colours, its overscan, side cropping and sprite limit, the Game Boy's clock.

<table>
  <tr>
    <td align="center"><img src="docs/tabs.png" width="320" alt="Retro-Go's NES tab: a red background with a controller"><br><sub>The NES tab</sub></td>
    <td align="center"><img src="docs/game.png" width="320" alt="Thwaite, an NES homebrew game, in Fit mode"><br><sub>Thwaite, an NES homebrew game, in Fit</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/menu.png" width="320" alt="Retro-Go's menu over the game"><br><sub>Retro-Go's menu</sub></td>
    <td align="center"><img src="docs/options.png" width="320" alt="Retro-Go's options: scaling, speed and the emulator's options"><br><sub>Options</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/sms.png" width="320" alt="Raposa do Sol, a Master System homebrew game, in Fit"><br><sub>Raposa do Sol, a Master System homebrew game</sub></td>
    <td align="center"><img src="docs/gg.png" width="320" alt="Waternet, a Game Gear homebrew game, at 1x with the keys on the side"><br><sub>Waternet on the Game Gear, at 1x</sub></td>
  </tr>
</table>

## Controls

| Key | In the launcher | In a game |
| --- | --- | --- |
| Arrows | Choose a system, then a game | The D-pad |
| OK | Open the system, then the game | A (Sega: 2) |
| Back | Back to the systems | B (Sega: 1) |
| shift | | Select |
| EXE | Like OK | Start (Master System: Pause) |
| Backspace | Options, Controls, About | Retro-Go's menu |
| Home | Quit | Save and quit |

In 1x on the Game Boy and the Game Gear, the keys are written on the side of the screen. The first time you play, Retro-Go shows them, and Controls in its menu shows them again.

## Good to know

- No sound: the calculator has no speaker.
- The calculator has about 42 KB for every app's saves. A Game Boy or NES state takes 2 to 10 KB, a Master System or Game Gear one 3 to 15 KB, a Game Boy Color one up to 30 KB, so keep the slots you need and delete the others in the launcher (Delete save).
- The calculator has no clock for apps: a Game Boy game's clock runs while you play, and you can set it in Options, RTC config.
- Cartridges with more than 32 KB of RAM (a few Game Boy Color games) and NES mappers nofrendo doesn't have say so instead of starting.
- The SG-1000's games, and the light gun and 3-D glasses games of the Master System, don't play.

## Credits

[Retro-Go](https://github.com/ducalex/retro-go) is **ducalex**'s. Its code is under the GPL version 2 only, which can't be mixed with NumPlay's GPL version 3, so none of it is copied here: the launcher, the menus, the options and the frame skipping are written again from how Retro-Go looks and works, and its pictures are drawn again in its style. Not affiliated with Retro-Go.

- **[gnuboy](https://github.com/rofl0r/gnuboy)** by Laguna and Gilgamesh and gnuboy's contributors (GPL-2.0-or-later, [COPYING](src/gb/COPYING)), from rofl0r's tree, emulates the Game Boy. Its picture is drawn by new code here: gnuboy's keeps 256 KB of decoded tiles, more than the calculator has.
- **nofrendo** by Matthew Conte and nofrendo's contributors (version 2 of the GNU Library GPL, [COPYING.LGPL](src/nes/COPYING.LGPL), used under the GPL as its section 3 allows) emulates the NES. Its 2.0 release is the one used here.
- **[z80](https://github.com/superzazu/z80)** by Nicolas Allemand (superzazu) (MIT License, [LICENSE.z80](src/sms/LICENSE.z80)) is the Master System's and Game Gear's processor, changed to run faster on the calculator. The rest of those machines is NumPlay's, written from Charles MacDonald's notes on their hardware and SMS Power's pages. Retro-Go's own Master System emulator, SMS Plus, can't be used: its Z80 may not be shared freely. The short list of games that need something of their own is [SMS Plus GX](https://github.com/libretro/smsplus-gx)'s (GPL-2.0-or-later).
- The tests run [Blargg's test ROMs](https://github.com/retrio/gb-test-roms), [dmg-acid2 and cgb-acid2](https://github.com/mattcurrie) by Matt Currie, [nestest](https://github.com/christopherpow/nes-test-roms) by kevtris, Frank Cringle's zexdoc and zexall, as [superzazu's z80](https://github.com/superzazu/z80) has them and in [Maxim's Master System port](https://github.com/maxim-zhao/zexall-sms), which aren't in NumPlay. The pictures show [Thwaite](https://github.com/pinobatch/thwaite-nes), [Concentration Room](https://github.com/pinobatch/croom-nes), [Libbet and the Magic Floor](https://github.com/pinobatch/libbet) and the [240p Test Suite](https://github.com/pinobatch/240p-test-mini) by Damian Yerrick and others, [µCity](https://github.com/AntonioND/ucity) by Antonio Niño Díaz, [Raposa do Sol](https://github.com/jflores82/raposadosol) by tibonev, [Waternet](https://github.com/joyrider3774/waternet) by Willems Davy and [Tunnel](https://github.com/liamhays/tunnel) by Liam Hays, all free homebrew games.
- Game Boy, Game Boy Color and NES are trademarks of Nintendo, Master System and Game Gear of Sega. Not affiliated with Nintendo or Sega.

Made by Mason Chen as part of NumPlay. It comes from the customizer website with your games in it, inside [NumPlay](https://github.com/Mason363/NumPlay) or on its own.

Build: `make ROMS="game.gb game.nes game.sms"` makes `output/retrogo.nwa` with those games in it, and `make test` checks the emulators and the saves on your computer (see `tests/`).
