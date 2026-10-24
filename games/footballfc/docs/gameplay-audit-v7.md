# Football FC v7 — gameplay and rendering audit

The supplied football screenshot is the reference for camera framing, world
proportions and player distribution. The existing dark menus and compact match
interface remain. This revision changes the native game locally; it does not
publish a PR or modify NumPlay's launcher.

## Findings and corrections

| Finding in v6 | Effect | Correction in v7 |
| --- | --- | --- |
| Projection ignored `cam_x` and included almost the full 74 m width; player size used a separate exaggerated scale. | Oversized players on a distant field, little local detail. | Closer projection follows both axes. A 9 m circle radius spans about 84 pixels and a 1.75 m player about 16 pixels at centre. Both now share the same world scale. |
| Switching during possession moved the selected player, but action keys still operated the actual carrier. | A different player could charge or kick remotely. | Possession consistently selects and controls its carrier; defensive switching remains available. |
| OK on a loose ball called `fc_tackle`, setting a 30-tick cooldown before checking whether an opponent owned it. | Empty tackles disabled receptions, including the first kickoff pass. | OK only requests a standing tackle against an opponent in possession. A loose ball remains collectable. |
| The receiving assist overrode directional movement and chased a fixed destination. | Steering felt unresponsive; slowed ground passes and descending lobs could be missed. | Idle assistance searches a decelerating ballistic flight for a reachable interception. Directional input takes control immediately. |
| Independent per-axis acceleration and ball-speed clamps favoured diagonals. Sprint touches could continue along an old heading after a sharp turn. | Uneven acceleration and loose balls on ordinary steering changes. | Vector acceleration and speed limits; a sharp turn prompts an earlier physical touch. |
| Keeper collection only ran for unowned balls. | A dribbled ball could pass through a keeper's feet. | A keeper can collect an exposed dribble on physical contact inside his own penalty area. The small reach and v6 accessible reaction/movement settings remain. |
| A keeper parry retained the original receiver and control lock. | The receiving assist could keep following a deflected opponent ball. | Parrying clears receiver and lock. |
| Kickoff passes used the normal directional target search while every teammate was behind the ball. | Holding forward could launch a blind pass into the opposition half. | A nearby legal layoff partner receives the kickoff, in either half. |
| Every free kick triggered a shot, including distant restarts taken with OK. | Unwanted long shots and lost possession. | OK passes; EXE shoots. Opponent automatic distant restarts use a pass. |
| Completion statistics only credited the named receiver. Pass power was only partially clamped. | Valid teammate receptions were omitted; direct calls could use excessive speed. | Credit any teammate completing a real pass; clamp power before all speed/lead arithmetic. Training still requires the marked recipient. |
| Support targets collapsed towards similar depths; formation roles assumed four backs and three midfielders throughout. | Flat attacking lines and incorrect 4-4-2/3-4-3 behaviour. | Separate holding/diagonal/wide/forward targets; formation-specific roles; staggered offside support; marking prioritises nearby threats. |
| Several nearby defenders could converge on the carrier. | Crowds and less room for passing. | One designated chaser, with explicit teammate press for the human; other players keep positional duties and spacing. |
| Passing-drill receivers slowly converged at successive interception points. | The triangle lost its structure after repeated passes. | Unselected training players return to their stations. |
| Screen-space grass grain and a fixed empty band at the top of the field. | Crawling diagonal texture and a visible strip unrelated to the stadium. | Clean world-space mowing bands and a full-height inverse projection. Seating follows the projected far touchline. |

## New artwork

Original packed sprites replace small procedural match figures: 30 player
frames (five angles and six poses), three supporter tiles and four ball frames.
Team kits, shorts, hair and skin are palette materials. The renderer reads the
four-bit art directly from flash and clips to its current 16-row strip.

The artwork totals **6,656 bytes**, with no decoded image atlas in RAM.
The editable generator and exported sheets are in [assets](../assets/README.md).
Larger shootout figures retain their run-up, strike, dive and landing animation.

## Verification

- 320 consecutive sprint-turn ticks retain possession, with the physical ball
  gap bounded below 3 m; vector acceleration is checked against diagonals.
- Real reception fixtures: 12/12 ground, 10/12 through and 12/12 high passes.
  These are isolated fixtures, not success rates against defending teams.
- Carrier/action consistency, loose-ball OK, teammate completion statistics,
  pass-power bounds, both kickoff directions, free-kick choice and a keeper
  collecting an owned dribble have dedicated regressions.
- Settled support is checked for all three formations in both attack directions:
  width, a holding option, multiple nearby passes and correct wide-player roles.
- Camera tests verify reference proportions, actual cross-pitch tracking and
  usable ball visibility near both ends and both touchlines. Native menus,
  action states and scenes keep glyph/strip guards.
- Existing complete-match, random-input, cup, save, training and physical
  shootout regressions run again. The linked ARM app is exercised through its
  real key path on all supported RAM profiles; see [validation](validation.md)
  for measured build, timing and memory results.

## Practical limits

These are host simulations and ARM-emulator tests, not tests on a physical
calculator. They establish state, input, drawing and storage behaviour, not a
human difficulty rating or physical LCD frame rate. Match timings are scheduling
targets of 30 FPS on N0120 and 25 FPS on the earlier models.

This remains an arcade 11-a-side game: compact fictional-player squads, simplified
body/ball contacts, automatic goalkeepers and no full professional substitution,
card or extra-time system. The screenshot guides composition; this is not an
identical reproduction of the reference engine.
