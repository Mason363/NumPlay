# Football FC v9

## Receiving passes

Intended human receivers accelerate towards a predicted interception and get a
larger physical control window (1.33 m horizontally, 1.75 m high). The first touch
immediately follows the receiver's movement rather than leaving a fast incoming
ball on its previous course. A brief receiving buffer prevents instant repeated
tackles during that touch.

Holding a direction nudges the receiving point without making the player run
away from the pass. Hold XNT or ALPHA for full manual steering. Defenders retain
their normal physical interception opportunities; possession is never awarded
to a receiver while the ball is still distant.

Regressions receive 12/12 ground, 12/12 through and 12/12 high passes while idle,
and another 36/36 with a direction held. Each controlled ball survives eight
subsequent steering ticks. A separate defended lane is still intercepted.
These isolated fixtures are not match-wide passing success rates.

## Penalty timing

Red timing now produces a physically off-target kick, either wide of the post or
above the bar. Amber can still be on target but has more dispersion than green.
Good timing still requires sensible aim/power and can be saved. Four hundred
red-timing fixtures cross outside the goal frame and award zero goals.

A related post test is corrected: a ball far above the crossbar cannot hit an
infinitely tall upright. The actual run-up/flight/result, rather than only a
result label or score override, follows the mistimed target.

## Free kicks from behind the player

Both match free kicks and the new practice mode use a camera behind the taker.
It turns towards the centre of the goal, including wide positions and the second
half. World markings, defenders, the keeper, ball height, shadows and the dotted
preview share the same perspective and depth ordering. The shot continues with
the existing wall, spin, gravity and goalkeeper physics.

| Key | Action |
| --- | --- |
| Left / Right | Aim across goal |
| Up / Down | Set power |
| 8 / 9 | Left / right curl |
| 7 | Remove curl |
| 5 | Low / middle / high flight |
| Hold EXE, release | Shoot at the chosen timing |
| OK | Short pass in a match; quick shot in practice |
| 6 in practice | Pick another position |
| Back | Pause and access help/leave |

The ordinary match camera is preserved during the free kick and resumes after
the ball is controlled. The front view stays aligned during a goal/result.
Projection fixtures cover central/wide positions in both halves.

## Dedicated practice

Choose **Free kicks** beside **Penalties** in the home menu. Select teams and
difficulty, then take unlimited attempts against a wall and keeper. Positions
vary, goals/attempts count within the session, and each result leads to a new
kick. OK skips the short result wait. There is no career reward or match record.

Forty-eight host practice attempts across all difficulties reset without entering
a league match, halftime or full-time flow. Held new-position input triggers once.
The real ARM app also enters, shoots, repeats, changes position, pauses and leaves
through its menus with unchanged match statistics and credits.

The home's former “MATCH PROGRAMME” caption is removed. Eight help pages document
the revised controls. The camera uses the existing drawing strip, clips ground
rows into grass spans, and reuses its projection basis to limit CPU work. The
asset pack and save format are unchanged; resource/timing results are in
[validation](validation.md). Testing uses host simulation and ARM emulation;
physical calculator testing remains outstanding.
