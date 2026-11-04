> **Kingdom Rush is a [NumPlay](../../README.md) game that comes on its own.** It is too big to fit next to NumPlay on the calculator. Get `KingdomRush.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

<p align="center">
  <img src="src/icon.png" width="84" alt="Kingdom Rush icon">
</p>

<h1 align="center">Kingdom Rush</h1>

<p align="center">
  <b>Defend the realm with towers, on the NumWorks calculator.</b><br>
  The 2011 Flash game's 12 stages, each with its Heroic and Iron challenges, and its seven heroes.
</p>

<p align="center">
  <a href="https://github.com/Mason363/NumPlay/releases/latest/download/KingdomRush.nwa"><b>Download KingdomRush.nwa</b></a>
  &nbsp;·&nbsp; <a href="#controls">Controls</a>
</p>

<p align="center">
  <img src="../../docs/media/gallery/kingdomrush.gif" width="640" alt="Kingdom Rush: the Hero Room, then Alleria and the first towers at the Citadel">
</p>

## Highlights

* **The whole free campaign:** Southport to the Dark Tower, with the game's own paths, waves and story, and each stage's own buildings: the elves' hall to repair and its Sylvan elves, the Citadel's imperial guards, the Sasquatch's frozen cave (fire breaks its ice), the Sunray Tower and its sorcerers, the graveyards where the dead rise again, and Vez'nan at the end.
* **Heroic and Iron challenges** for every stage: 3 stars in the Campaign open them.
* **Every tower:** archers, barracks, mages and bombards to level 3, then the eight special towers (Rangers Hideout, Musketeer Garrison, Holy Order, Barbarian Hall, Arcane Wizard, Sorcerer Mage, Big Bertha, Tesla x104) with all their abilities.
* **Every enemy and boss:** 30-odd kinds, from goblins and wulves to dark knights, demons and necromancers, the Juggernaut, J.T. and Vez'nan.
* **The seven heroes** of the Flash game's Hero Room: Sir Gerald, Malik Hammerfury, Alleria Swiftwind, Bolin Farslayer, Magnus Spellbane, Ignus and King Denas, each with both skills, the wildcat and the illusions, levels 1 to 10 and their own pictures.
* **Rain of Fire and Reinforcements,** and the upgrade tree: 30 upgrades to buy with your stars, and to take back.
* **The game's rules and numbers,** read out of its Flash file: costs, damage, range, armor, speed, bounty, every wave's timing, the gold for calling a wave early.

<table>
  <tr>
    <td align="center"><img src="docs/shot.png" width="390" alt="The Citadel's roads held by towers and soldiers"><br><sub>The Citadel</sub></td>
    <td align="center"><img src="docs/ring.png" width="390" alt="The tower menu around a build spot at Southport"><br><sub>The tower menu</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/map.png" width="390" alt="The map of Linirea, seven stages won"><br><sub>The map</sub></td>
    <td align="center"><img src="docs/upgrades.png" width="390" alt="The upgrade tree"><br><sub>The upgrades</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/veznan.png" width="390" alt="The Dark Tower, Vez'nan's demons on the road"><br><sub>The Dark Tower</sub></td>
    <td align="center"><img src="docs/heroes.png" width="390" alt="The Hero Room, Sir Gerald picked"><br><sub>The Hero Room</sub></td>
  </tr>
</table>

## How to play

Enemies walk the roads, and each one that gets through costs lives. Build towers on the spots beside the road with the gold you have, and earn more for every kill. Archers and mages shoot, bombards blast what walks, and barracks send soldiers to block the road: move them with the rally point in their menu. Upgrade a tower to level 3, then pick one of its two special towers and buy their abilities.

Call the next wave early for gold. Rain of Fire and Reinforcements come back on their own after each use. Win a stage with 18 lives or more for 3 stars; stars buy upgrades on the map.

The Hero Room opens on the map for 15 stars. The hero you pick joins at the first wave, grows stronger as it fights and comes back a while after it falls.

## Controls

| Key | What it does |
| --- | --- |
| Arrows | Hop between build spots, towers, wave flags and enemies |
| OK | The tower menu: build, upgrade, sell, buy an ability, move the rally point |
| 1 | Rain of Fire: aim with the arrows, OK to cast |
| 2 | Reinforcements: aim, OK to call them |
| 3 or ⌫ | The next wave now, for gold |
| 4 | Your hero: move it with the arrows, OK sends it |
| Back | Close the menu, or pause |
| Home | Save and quit |

On the map: the arrows pick a stage, OK opens it, Left and Right pick the mode, 0 opens the upgrades (OK buys one, ⌫ gives every star back) and 5 the Hero Room (OK picks a hero, ⌫ gives the 15 stars back).

## Good to know

* **Too big to install next to NumPlay.** Your saves in each are kept.
* **Your stars and upgrades** are saved after every stage, in `kingdomrush.sav` and a copy in `kingdomrush_saves.py`, which comes back after installing apps from the NumWorks website. A stage itself is not saved: Home in a battle leaves it.
* **Frozen towers:** J.T. freezes towers. Press OK on one three times to break the ice, as the original's three clicks.
* **The Sunray:** its crosshair hops from enemy to enemy with the arrows, where the original follows the mouse; OK fires.
* **New enemies** show their note once, the first time they come, where the original put an icon to click.
* **No sound:** the calculator has no speaker.
* **The pictures are the game's,** at 0.4 of their size: the 700 by 600 battlefield fills 280 by 240 pixels, beside a bar with your lives, gold and spells.
* **Heroes level up in each battle,** from 1 to 10, as in the Flash game. They come to every campaign, and to the challenges of the last three stages.
* **The Hero Room** shows each hero's card, bars and skills, not the descriptions.
* **Left out:** the three premium stages, achievements and the encyclopedia.

## Credits

Inspired by Kingdom Rush by Ironhide Game Studio, published by Armor Games. Not affiliated with them.

* The pictures, rules and numbers come from the game's 2011 Flash file: its scripts give the towers, enemies, waves and spells, its shapes are drawn into the pictures, and `tools/pack.py` makes them into `src/data.bin`. The heroes come from the later Flash version with the Hero Room.
* The words are drawn in Lilita One ([license](../../LICENSES/OFL-1.1-LilitaOne.txt)) and the DejaVu fonts ([license](../../LICENSES/DejaVu.txt)).

Made by Mason Chen as part of NumPlay.

## License

The game's code is licensed under the GNU General Public License v3.0: see the `LICENSE` file at the root of the repository. The game's pictures keep their own terms (see Credits).

Build: `make` (needs `arm-none-eabi-gcc` and Node.js for nwlink), `make test` on a computer.
