# Football FC pixel art

Original recolourable artwork for the calculator's close sideline view:

- `players.png`: 30 frames, 16 x 24 pixels each. Five angles (front,
  three-quarter front, profile, three-quarter back, back), with idle,
  four running contacts and a kick. Left-facing views mirror the same art.
- `supporters.png`: three 32 x 16 crowd tiles, including raised scarves.
- `ball.png`: four 8 x 8 rotation frames. Rotation follows ball travel.
- `bench.png`: covered dugout with seats and substitutes, 64 x 24.
- `steward.png`: high-visibility touchline steward, 12 x 24.
- `camera.png`: camera, tripod and operator, 24 x 24.
- `banner.png`: cloth pattern for upper/lower seating bays, 64 x 12.
- `players-preview.png`: enlarged inspection sheet, not installed in the game.

The source artwork lives in [`../tools/assets.py`](../tools/assets.py).
Run `python games/footballfc/tools/assets.py` from the repository root to
regenerate the PNG sheets and `src/assets.h` deterministically.

Four-bit materials: 0 transparent, 1 outline, 2/3/4 shirt/shadow/light,
5/6 socks light/shadow, 7/8 skin/light/shadow, 9 hair, 10/11 shorts,
12 boots/scarf, 13/14/15 seating. Player palettes are recoloured per team
and individual; images contain no baked-in team colours or text.

The compiled arrays occupy **8,240 bytes** of read-only flash (1,584 more than v7). Rendering reads
them directly in horizontal strips; no decoded atlas or image heap is needed.
Menus, large shootout figures, pitch markings, goals and nets retain native
vector/procedural drawing. No art was extracted from the supplied reference.
