# Validation

Completed on 2026-10-24 using host C tests and ARM emulation. No physical
calculator has been tested. These results do not establish a physical LCD
frame rate or claim that a human completed all 30 stages on this adaptation.

## Build and resources

- Stripped `.nwa`: **277,244 bytes**.
- Installed payload: **258,380 bytes** on every tested profile.
- Static RAM: **11,525 bytes**, including the 10 KiB drawing strip.
- Maximum observed ARM stack: **636 bytes**.
- Save profile: **72 bytes**; the inert Python backup and record names bring
  total calculator save storage to less than 320 bytes.
- All stage/image/animation tables remain in flash. No heap, decoded atlas,
  audio, full-screen framebuffer or runtime asset download.

| Profile | Maximum active-play render work | Maximum on all tested screens | Stack | Result |
| --- | ---: | ---: | ---: | --- |
| n0120 | 9 ms | 25 ms | 636 B | Passed |
| n0110 | 23 ms | 62 ms | 636 B | Passed |
| n0115 | 23 ms | 62 ms | 636 B | Passed |
| upsilon | 23 ms | 62 ms | 636 B | Passed |

Logic runs at the original 30 Hz, with the same movement speed on each profile.
Drawing targets 30 FPS. Static menus, announcements and tips redraw on changes;
their one-time render cost can exceed the active-play frame budget on old models.
The timings above are emulator estimates in tested scenes, not physical-device
measurements.

## Reference and host checks

- All 30 stages use the source's enemy, coin, checkpoint and finish counts.
- 7,818 independent original Flash position samples match the native track
  evaluator to within one twip (**0.05 original pixel**) per coordinate.
- 10,560 points match independently rendered original wall masks.
- 480,000 random-input engine steps cover all stages, bounded coordinates,
  collected-coin counts and death/transition states.
- Each stage's finish requires its full coin set. Checkpoints survive death;
  coins reset, the death count increments once, and enemy time keeps advancing.
- The three original tip screens wait for input without advancing hazards.
- The real first-stage engine is solved in **118 ticks with zero deaths** by
  a time-aware host search (173,378 visited states). Its actual input replay,
  engine and renderer supply the GIF; it is not a mock-up or a hardware recording.
- Packed saves round-trip; every single-byte corruption, truncation and absent
  save is rejected. Completed records update once.
- Native renders cover all menus, all 30 boards, different enemy phases, all
  announcements/tips and a six-digit death count, with glyph/strip bounds guards.

## ARM app checks

The linked calculator binary is exercised on N0120/Epsilon, N0110/Epsilon,
N0115 RAM and Upsilon profiles. Each test checks real key input, all menu pages,
pause/frozen hazard phase, all 30 active stages, a real enemy collision, death
recovery, a coin, a level transition, the tip/announcement controls, and one-time
completion/records. State injection selects late stages and finish situations;
this is not an automated collision-free playthrough of all stages.

Home/Power exits save the run and return with clean app RAM, balanced Home
locks, unchanged firmware RAM and an unrelated Python-script sentinel preserved.
A reinstall fixture retains scripts only and recovers the active stage from its
backup. Separate cases cover full storage, corrupt data and held launch keys.

## Scope and practical limits

The original levels, animation cycles and gameplay rules are retained. The
screen and palette are scaled/quantised, wall curves use sampled edges, native
fonts replace interactive web text, and web links/online scores become local
help, credits and records. Save/Continue and pause are calculator additions.
Audio is omitted. See [reference provenance](reference.md) and
[controls and credits](../README.md). Physical calculator playtesting is pending.

SHA-256: `fa675bbf90b93830b9185d9b51024139a1727a8796464e8a53f68ac111c1a303`
