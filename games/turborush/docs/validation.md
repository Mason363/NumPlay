# Local validation

Build and tests completed on 2026-10-23. No physical calculator test was performed.

The revised stripped `TurboRush.nwa` is 275,156 bytes; its installed payload is 243,168
bytes on each supported RAM profile. Static RAM is 18,301 bytes and the highest
observed stack use in the ARM tests is 660 bytes. The save payload is 80 bytes.
These figures exclude firmware-owned memory and filesystem record metadata.

| ARM profile | Maximum measured race render work | Peak stack | Result |
| --- | ---: | ---: | --- |
| N0120 / Epsilon | 10 ms | 660 B | Passed |
| N0110 / Epsilon | 24 ms | 660 B | Passed |
| N0115 / Epsilon | 24 ms | 660 B | Passed |
| N0110 / Upsilon | 24 ms | 660 B | Passed |

Render figures are emulator estimates for the tested scenes, including rain,
not hardware FPS guarantees. N0120 targets a 20 ms frame interval; earlier
models target 33 ms. Simulation advances at fixed 20 ms steps; render positions
are interpolated between the previous and current simulation states.

Host tests completed 288 full races (six tracks, three difficulties, stock and
fully upgraded cars, eight seeds), totaling 1,752,383 simulation steps. A steering
controller drove these races; this verifies completion and engine invariants,
not human difficulty. Another 1,500 random-input sessions exercise control and
state bounds. Targeted fixtures check stationary steering, progressive response,
braking with boost, nitro reserve recovery, contact width, repeated contacts,
barrier cooldown and wrapped distance. Another 270 guarded renders covered every
landscape, mood and effect at several edge positions. Every menu was rendered
and inspected. Text glyph bounds were checked for every screen and selection,
including maximum numeric values, to catch clipping at the LCD edges.

World-space projection tests verify landmark and road-marking continuity at
8-metre mesh-cache boundaries, correct perspective growth when approaching,
tire contact alignment over all six courses, render interpolation and traffic
respawn discontinuity handling. The renderer exports a 240-frame steering and
acceleration sequence for visual review; see [the animated preview](driving-v4.gif).

Free Ride is additionally simulated for 160,000 steps (53 minutes 20 seconds),
including distance rebasing and renewed traffic, with no race finish, damage
ending or race reward. Another 128 seeded traffic-spawn fixtures check spacing.
The new grid has explicit longitudinal and same-file separation checks. Camera
bounds and a visible rightward player displacement are checked in host and ARM.

ARM tests exercised real key paths for the menu, garage, paint, manual shifts,
throttle, nitro, pause and resume. Race-end fixtures then exercised all six Grand
Prix transitions, one-time rewards, purchases and persistence. They also checked
corrupt saves, full storage, a held launch key, firmware RAM preservation,
Home/Power exit and cleanup of the application's RAM. Free Ride is launched
through the setup menu on every profile, driven and paused with real keys, then
checked beyond the previous finish and time limits. Its mode persists while
race credits and records remain unchanged. Race-end fixtures skip
driving the laps already exercised by the host engine tests.

SHA-256 of `TurboRush.nwa`:
`a7cfbe1665bab12f39ace269739172e3837fac4976ee2ca0bf7417b9f8f98460`

## Publication integration

Rebuilt against current NumPlay common helpers with `EPSILON_XL=1`. Root Make and Windows builder discovery export `TurboRush.nwa`. The standalone release entry, README card, Bobydeluxe credit and focused Actions build are included; the default launcher and other games are unchanged.
