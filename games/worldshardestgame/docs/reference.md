# Original reference and data pipeline

Reference: [The World's Hardest Game on Armor Games](https://armorgames.com/play/1043/the-worlds-hardest-game),
by Stephen Critoph / Snubby Land, version 1.0.
The public distributable SWF is available from the author's Newgrounds upload:
[431039_TheWorldsHardestGame.swf](https://uploads.ungrounded.net/431000/431039_TheWorldsHardestGame.swf).

- SWF size: 744,124 bytes.
- SHA-256: `ee125fff640942ee4cb72a4a195e931b8d6a8bb6a3f6fce268fd8c8d7e0f91a8`.
- Stage: 550 x 400 original pixels, 30 frames per second.
- Gameplay labels: `L1` to `L30`, root frames 61, 65, ..., 177.
- Original player half-width: 180 twips (9 pixels); movement: 60 twips per step.
- Original enemy and coin radius: 130 twips (6.5 pixels).

JPEXS Free Flash Decompiler 26.4.0 exports the vector geometry, compiled timeline
matrices, ActionScript and static reference renders. No third-party remake's
levels or approximated enemy patterns are used. SWF binaries, audio, Java and
the decompiler are authoring inputs, not part of the installed game.

`src/levels.c` stores the original visible objects and loop phases, with
translation/symmetry sharing between tracks. Time-linear keys are simplified
with a maximum one-twip coordinate error. Wall corners are sampled from their
quadratic curves at eight subdivisions; their directed fill edges retain holes
and the original point-based collision semantics.

Thirty 320 x 233 static boards share 738 unique RLE rows and a 32-colour palette,
using 50,782 bytes including row lookups. The engine reads these directly from
flash. Original checkpoint bounds and origins are kept independently from the
scaled board. The title alone is rasterised; interactive text is rendered by
NumPlay's native fonts.

The source contains one unused coin behind an opaque wall in stage 9. It cannot
be collected and is absent from the stage's required coin count. The import
removes that hidden object, retaining the original one visible/required coin.

`tests/reference.inc` contains 7,818 original, uncompressed world-position
samples across all stages and animation periods. `tests/wall-reference.inc`
contains 10,560 wall-hit samples from independent original wall renders.
Neither table is linked into the calculator app. [Import counts](import.json)
record the 30 stages, their 1,062 enemies and 148 visible coins.

## Regenerate

Place the original SWF in an authoring folder called `reference/`. Use JPEXS CLI
to export the inputs (Java 17 or newer):

```sh
java -jar ffdec.jar -swf2xml reference/original.swf reference/original.xml
java -jar ffdec.jar -export script reference/scripts reference/original.swf
java -jar ffdec.jar -export text reference/text reference/original.swf
python games/worldshardestgame/tools/prepare_reference.py --reference reference
java -jar ffdec.jar -xml2swf reference/boards.xml reference/boards.swf
java -jar ffdec.jar -xml2swf reference/walls.xml reference/walls.swf
```

Use `-select 61,65,69,73,77,81,85,89,93,97,101,105,109,113,117,121,125,129,133,137,141,145,149,153,157,161,165,169,173,177`
before `-export frame reference/boards reference/boards.swf`, and the same
selection for `reference/walls`. Also export root frame 27 of the untouched SWF
to `reference/screens` for the title. Then:

```sh
python games/worldshardestgame/tools/import_original.py --reference reference
python games/worldshardestgame/tools/icon.py
```

The build itself uses only the checked-in generated sources and icon.
