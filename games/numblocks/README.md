# NumBlocks

Minecraft 1.8.8 on your NumWorks calculator. NumBlocks isn't a look-alike: it follows Minecraft 1.8.8 exactly, so a seed gives you the very same world that Minecraft 1.8.8 makes, block for block, with its blocks, items, recipes, mobs, textures, font and screens.

<img src="docs/shot.png" width="320" alt="NumBlocks: oak trees and lilacs, with a sheep, a cow, a pig, a chicken and a creeper">

- **The same worlds as Minecraft 1.8.8:** type a seed and you get the world Minecraft 1.8.8 makes, block for block: the same terrain, biomes, hills, caves, lakes and trees. Or a superflat world to build on.
- **Survival:** health, hunger, day and night, zombies, skeletons, creepers and spiders, and pigs, cows, sheep and chickens.
- **Weather:** rain, snow and thunderstorms with lightning, as often as in 1.8.
- **Crafting:** 129 items and all their 1.8 recipes, a furnace, chests, tools that wear out, armor.
- **Fishing:** cast, wait for the bite, reel in fish, junk or treasure.
- **Building:** 250 kinds of blocks, doors, beds to sleep through the night, stairs, torches that light up caves, flowing water and lava, farms that grow.
- **Fire:** light it with flint and steel. It spreads and burns wood, leaves and wool, like in 1.8.
- **Creative mode:** fly, every block and item, break things at once, and search for any block.
- **Commands:** /gamemode, /give, /time, /weather, /tp and more, with cheats on (Allow Cheats, or Open to LAN).
- **Your worlds:** keep several, give them names, and they save as you play and when you quit. A copy also goes into `numblocks_saves.py`, so installing the game again doesn't erase them.
- **The Nether and the End:** in `NumBlocks.nwa`, the app on its own (see below).
- **Not in yet:** redstone circuits and pistons, villages and the other structures, enchanting and brewing.

## The Nether and the End

`NumBlocks.nwa`, the app on its own, also has the Nether and the End, made the way Minecraft 1.8.8 makes them for your seed. NumPlay has no room for them, so its NumBlocks stays in the Overworld.

<img src="docs/nether.png" width="320" alt="NumBlocks: the Nether, netherrack, gravel and fire by a fortress wall"> <img src="docs/end.png" width="320" alt="NumBlocks: the Ender Dragon flying over the End, its health bar at the top">

- **Nether portals:** build an obsidian frame, from 4 by 5 up to 23 by 23, light it with flint and steel or a fire charge, and stand in it. One block in the Nether is eight in the Overworld, and the portal on the other side is found, or built, as in 1.8.8. Now and then a zombie pigman walks out of a portal.
- **The Nether:** netherrack, the lava sea, soul sand that slows you down, gravel, glowstone, nether quartz, fire that never goes out on netherrack, and lava that runs further and faster than in the Overworld. Water boils away, and beds blow up.
- **Nether fortresses:** laid out piece by piece as 1.8.8 does, with nether wart, chests and blaze spawners. Ghasts and their fireballs (hit them back), zombie pigmen that all turn on you when one is hit, blazes, magma cubes that split, and wither skeletons.
- **Endermen and ender pearls:** they teleport, carry blocks around and come after you if you look at them. A thrown ender pearl takes you where it lands.
- **Eyes of ender and strongholds:** blaze powder and an ender pearl make an eye of ender. Thrown, it flies towards the nearest of the world's three strongholds, the very ones 1.8.8 makes for your seed, with their corridors, prisons, libraries, fountains and iron doors that open with a button. Put an eye in each of the 12 frames of the portal room to open the End portal.
- **The End:** the island of end stone and its obsidian pillars, the ender crystals on top that heal the dragon, and the Ender Dragon itself: it circles, charges and dives, breaks what it can on its way (obsidian, end stone and bedrock stop it), and shows its health at the top of the screen. While they are out of sight, triangles at the screen's edge point to the dragon (purple) and to the pillars whose crystals are still whole (pink). Kill it for its experience, the exit portal and the dragon egg. The exit portal shows the End poem and the credits, then takes you home.
- **Saved:** each dimension keeps its own changes, and you come back where you left. Open a world saved in the Nether or the End in NumPlay's NumBlocks and you are in the Overworld where you last stood there, without the items it doesn't have.
- **Not in:** brewing (nether wart grows, but there are no potions), silverfish (the strongholds' stone bricks are plain ones), the other structures, and redstone.

## Controls

| Key | What it does |
| --- | --- |
| Arrows | Look around |
| , π √ x² | Walk forward, left, back, right (like W, A, S, D) |
| shift | Jump (twice in Creative: fly) |
| alpha | Sneak |
| ⌫ | Sprint (or press forward twice) |
| OK | Mine, attack (like the left mouse button) |
| Back | Place, use, eat (like the right mouse button) |
| var | Inventory |
| 1 to 9 | Pick a hotbar slot |
| x,n,t | Drop the item |
| log | Pick the block you look at |
| × | Chat |
| ÷ | Type a command (toolbox finishes words) |
| ans | Pause |
| Home | Save and quit |

You can change these keys in Options, then Controls. In the inventory, the arrows move between slots, OK picks up or puts down, EXE takes or leaves one, and shift with OK moves a whole stack. To spread a stack, hold OK and move over the slots: it splits evenly when you let go (hold EXE to drop one in each). In Creative, the Search Items tab finds blocks as you type, and shift with up or down jumps to the top or bottom of a tab. The game shows these keys the first time you open it, and again from Options, then Controls.

## Get it

It comes inside [NumPlay](https://github.com/Mason363/NumPlay), or on its own as `NumBlocks.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

## Credits

Inspired by Minecraft, but not affiliated with Mojang or Microsoft. Go play the real Minecraft and support the people who made it! The textures, the font, the screens, the End poem and the credits come from Minecraft 1.8.8, and the world is made the way Minecraft 1.8.8 makes it.

Also try [Numcraft](https://github.com/yannis300307/NumcraftRust) by yannis300307, the first Minecraft-like game for the NumWorks.

Made by Mason Chen as part of NumPlay.

## License

The game's code is licensed under the GNU General Public License v3.0: see the `LICENSE` file at the root of the repository. The textures, font and screens come from Minecraft 1.8.8 and belong to Mojang: they are not covered (see Credits).
