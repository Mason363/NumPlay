# Local validation — 2026-10-23

Artifact: `build/apps/AngryBirds.nwa`.

| Measurement | Result |
| --- | ---: |
| Release NWA | 1,391,696 bytes |
| Installed binary | 1,377,744 bytes |
| Static RAM | 87,068 bytes |
| Maximum observed ARM stack | 2,504 bytes |
| Save record | 2,336 bytes |
| Packed levels | 572,318 bytes |
| Indexed artwork | 686,084 bytes |

SHA-256: `cd53188f82480be7172be3d5aaf8e8f73f626e90a0624180c85d65cef268c30d`.

`build.py` and `make check` succeeded. Release and debug packages produce identical installed bytes. Installation was checked with a 107,674-byte RAM budget.

Publication integration was validated against upstream main `0f232329b6f5d3624e7e50034f32f3b57c139934`: the root standalone Make target and Python builder produce the same release file, Windows builder discovery finds it, the release catalogue lists it under Individual Games, and it is excluded from `games/games.json` and the launcher. The standalone build enables `EPSILON_XL` for the current common firmware helpers. A dedicated workflow builds, tests and uploads `AngryBirds.nwa`.

## Host and asset tests

`make test test-assets` passed. All 443 maps were loaded and simulated for 300 ticks each: **132,900 ticks**, finite body state, bounded arrays and no contact overflow. Maximum observed contact count: **306 / 768**. This is a simulation soak, not proof that all maps have a winning solution under the adapted physics.

Tests cover all seven birds, blue splitting, yellow acceleration, white egg, black explosion, boomerang reversal, sardine-to-Eagle sequence, separate normal/Eagle records, save round trips, legacy save migration and rejection after corruption of every save byte. Asset checks verify 429 campaign stages, 14 bonus maps, 19 storyboards, input hashes, exact packed float32 placements and deterministic regeneration.

Text bounds are asserted across every stage-selection page, all episode pages and each screen, including maximum-width score values. Extreme off-screen camera coordinates exercise the clipped line renderer. Incremental level preparation is compared byte-for-byte against the synchronous implementation for all 443 stages.

The host search clears stage 1 with one red bird at 11 degrees, full power, for **32,460 points and three stars**. The ARM suite repeats the clear through actual keypad events and checks the saved stage-2 unlock and preservation of an unrelated storage record.

## ARM execution

The compiled ARM application passed memory guards, firmware RAM preservation, lock balancing, app RAM cleanup, Home/Power exits, held-key suppression, full-storage retry/menu/unsaved-exit behavior and menu/help/story/pause navigation.

| Profile | RAM budget | Basic draw max | Basic simulation tick max |
| --- | ---: | ---: | ---: |
| N0120 / Epsilon | 153,676 B | 10 ms | 1 ms |
| N0110 / Epsilon | 148,928 B | 26 ms | 1 ms |
| N0115 / Epsilon | 133,652 B | 26 ms | 1 ms |
| Upsilon conservative budget | 107,674 B | 26 ms | 1 ms |

The 350-object `easterspecial-3` fixture also ran without contact overflow or exit violations. Peak draw / simulation-tick costs were **13 / 5 ms** on the N0120 profile and **32 / 8 ms** on the conservative N0110 profile. The latter produced fewer display updates during the collapse. Crowded scenes may slow down on older calculators.

The selection regression opens a level from each of the 13 episodes, retries it through the pause menu and exits with Home, on N0120, N0110 and N0115 profiles. It also checks Back cancellation and Home exit while preparing the 350-object stage, preserving an unrelated record. The preparation steps are distributed between keypad polls rather than running twelve expensive physics passes in one uninterruptible load. These tests address the reported selection-time stalls; an actual hardware closure was not reproduced in the emulator.

These timings are instruction-based emulator estimates, **not physical-device benchmarks**. The test uses a 216-million-instruction/s budget for N0120 and an intentionally slower 86.4-million budget for older profiles; these values are test parameters, not claims about measured CPU clocks or real frame rates. Stack figures are observed test peaks, not a proof of the maximum possible call path. Hardware installation, controls and performance still need a real-calculator playtest.

Screenshots in `docs/` were captured from this ARM build and visually inspected. Full generated reports live in `output/build-report.json`, `output/host-report.json`, `output/asset-report.json`, `output/arm/report.json` and `output/arm/selection-report.json` after running the commands in the README.
