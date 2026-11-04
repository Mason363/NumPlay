> **Balatro is part of [NumPlay](../../README.md)**, every NumWorks game in one app. Get it with all the others, or on its own as `Balatro.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

<p align="center">
  <img src="src/icon.png" width="84" alt="Balatro icon">
</p>

<h1 align="center">Balatro</h1>

<p align="center">
  <b>The poker roguelike, on the NumWorks N0120 calculator.</b><br>
  Play poker hands, build a deck of Jokers and beat the Boss Blind of Ante 8.
</p>

<p align="center">
  <a href="https://github.com/Mason363/NumPlay/releases/latest/download/Balatro.nwa"><b>Download Balatro.nwa</b></a>
  &nbsp;·&nbsp; <a href="#controls">Controls</a>
  &nbsp;·&nbsp; <a href="#credits">Credits</a>
</p>

<p align="center">
  <img src="docs/play.gif" width="640" alt="Playing a hand: cards score, Jokers trigger, the score adds up">
</p>

## Highlights

* **The whole core game:** Small, Big and Boss Blinds, the shop, booster packs, skip tags and Endless mode after Ante 8.
* **All 150 Jokers**, including the Legendary ones from The Soul, plus every Tarot, Planet and Spectral card, all 32 Vouchers and 24 Tags.
* **All 28 Boss Blinds**, with their own colours and effects, and the five final bosses.
* **Scoring like the original:** cards score left to right, then held cards, then Jokers, with retriggers, editions and seals, and every chip and mult popping up.
* **The look of the game:** the original card and Joker art, the moving paint background, the same panels, colours and font, the fire behind a hand that beats the Blind, and the foil, holographic, polychrome and negative sheens.
* **Saves everything:** press Home at any moment and pick your run back up exactly where you left it.
* Red Deck, White Stake.

<table>
  <tr>
    <td align="center"><img src="docs/shot.png" width="390" alt="Title screen"><br><sub>Title screen</sub></td>
    <td align="center"><img src="docs/round.png" width="390" alt="A round"><br><sub>Pick up to 5 cards and play them</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/blinds.png" width="390" alt="Choosing a blind"><br><sub>Play each Blind, or skip it for a Tag</sub></td>
    <td align="center"><img src="docs/cashout.png" width="390" alt="Cash out"><br><sub>Cash out: reward, hands left, Jokers, interest</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/shop.png" width="390" alt="Shop"><br><sub>The shop: Jokers, cards, Vouchers and packs</sub></td>
    <td align="center"><img src="docs/pack.png" width="390" alt="Booster pack"><br><sub>A Mega Arcana Pack: use Tarots on your hand</sub></td>
  </tr>
</table>

## Controls

| Key | Action |
|---|---|
| Arrows | Move between cards, Jokers and buttons |
| OK | Select a card, or pick a Joker or item (then Sell, Use, Buy, Open...) |
| EXE | Play Hand |
| ⌫ | Discard |
| shift | Sort the hand by rank or by suit |
| alpha | Pick up a card, Joker or consumable to move it: left / right carry it, OK puts it down |
| toolbox | Run Info (poker hands, blinds, vouchers) |
| var | View your deck |
| Hold OK or EXE | Speed up scoring |
| Back | Back, or the Options menu |
| Home | Save and quit |

The game speed can be changed in Options. "How to Play" in Options lists the keys too.

## Install

1. Download **[Balatro.nwa](https://github.com/Mason363/NumPlay/releases/latest/download/Balatro.nwa)** from the [latest NumPlay release](https://github.com/Mason363/NumPlay/releases/latest).
2. Plug the calculator into your computer and open **[my.numworks.com/apps](https://my.numworks.com/apps)** in Chrome or Edge.
3. Upload `Balatro.nwa`.

## TI-84 Plus CE

Balatro also runs on the TI-84 Plus CE, CE Python and TI-83 Premium CE: the same game, the same art and the moving background, drawn at 8 bits a pixel.

1. Send `BALATRO.8xp` and every `BAL*.8xv` (17 files, about 680 KB of archive) with TI Connect CE.
2. Run `prgmBALATRO` (on OS 5.5 or later through [arTIfiCE](https://yvantt.github.io/arTIfiCE/)). It needs about 110 KB of free RAM, and no C libraries.

| CE key | Action |
|---|---|
| Arrows | Move |
| 2nd | Select (OK) |
| enter | Play Hand |
| del | Discard |
| y= | Sort the hand |
| alpha | Move a card or Joker |
| math | Run Info |
| vars | View your deck |
| clear | Back, or Options |
| mode | Save and quit |

The run is saved in the appvar `NPbalato`.

## Credits

Inspired by **Balatro** by LocalThunk. Not affiliated with LocalThunk or Playstack.

* Card, Joker and interface art adapted from Balatro by LocalThunk.
* Font: m6x11 by Daniel Linssen.

Made by Mason Chen as part of NumPlay. If you enjoy it, please support the original game.

## Build

```sh
make          # output/balatro.nwa
make check    # link it like the calculator does and print the installed size
make sim      # a build for the NumWorks simulator
make assets   # rebuild the art from assets/ (Python 3, numpy, Pillow)
make port TARGET=ti84ce   # the TI-84 Plus CE version (ce/, CE C toolchain v15): build/ti84ce/balatro/bin
```
