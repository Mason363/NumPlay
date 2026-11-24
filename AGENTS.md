# TwiliPlay — Starlight Edition

## Project mission
Build a lightweight, visually distinctive, performance-oriented fork of [NumPlay](https://github.com/Mason363/NumPlay) for the NumWorks N0120 with official Epsilon firmware. Target 320 x 240 RGB565 and a TwiliPlay.nwa artifact. Preserve gameplay and existing saves. Use existing C and incremental embedded Rust when justified.

## Mandatory agent and token rules
- NEVER spawn, delegate to, or invoke SubAgents, worker agents, agent teams, or background AI agents. All coding, inspection, testing and review must be performed by the primary Codex agent. Ordinary compiler/test processes are allowed.
- Read targeted files and diffs; avoid repeated scans, large generated assets, verbose logs, speculative rewrites and unnecessary dependencies. Reuse findings and report concise actual results.
- Work in small testable changes. Never claim unverified results. Do not discard unrelated changes, force-push, rewrite published history or modify upstream.

## Baseline
Inspect the actual revision, Makefile, games/games.json, launcher/src, games/common, tools/gen_games.py, tools/npmodule.py, tools/module.ld, tools/kit.py, platform, tests and games/tetris/tetris/Cargo.toml. Record upstream commit and toolchain versions. Preserve modular relocatable objects, shared RAM arena, EADK, nwlink, LTO, section GC and strip rendering. Tetris already uses Rust staticlib, heapless and thumbv7em-none-eabihf. Establish an unmodified baseline using documented ARM GCC/newlib, Node/nwlink, Python/Pillow and Rust/Cargo; prefer reproducible WSL or CI if necessary.

## Exact game allowlist (in order)
| Title | ID |
|---|---|
| NumDash | numdash |
| Crossy Road | crossyroad |
| NumBlocks | numblocks |
| NumDrive | numdrive |
| Suika Game | suika |
| Tetris | tetris |
| Chrome Dino | dinorun |
| Doodle Jump | doodlejump |
| Chess | chess |
| Flappy Bird | flappybird |
| Tower Blocks | towerblock |
| Core Ball | coreball |
| Pac-Man | pacman |
| Snake | snake |
| Connect Four | connectfour |
| 2048 | g2048 |
| Minesweeper | minesweeper |
| Block Breaker | breakout |

Exclude all other game code, previews, metadata and exclusive dependencies from the compiled artifact, not just the menu. Preserve upstream sources/catalogue where practical. Doodle Jump is marked default:false and xl:true upstream; verify packaging and official firmware limits, report measured blockers and propose optimization or standalone packaging if needed. Never silently omit it.

## Storage and RAM
- Launcher ideal 50 KiB; maximum design target 100 KiB (102400 bytes), excluding games and existing previews. Measure actual launcher-specific code/resources, shared libraries, alignment, metadata, game assets and total installed size separately.
- Upstream notes 2496 KiB app space on N0120/Epsilon 26.4; confirm actual firmware and available installer capacity. Upstream RAM checks use 148928 bytes for older official Epsilon, 153676 for 25.2 and 133652 for a conservative custom build. Verify applicable stack, heap and arena limits.
- No extra full framebuffer (153600 bytes). Reuse strip buffers, procedural graphics, compact bitmap fonts, palette themes, compressed previews and fixed-capacity storage.
- No firmware modifications, enlarged app regions, deceptive icons or exam-mode bypasses. No unsupported flash writes. Preserve saves and normal Home/Back behavior.
- Measure installed bytes using actual linked/installer output, not merely .nwa file length. Fail applicable memory checks in CI; report budget failures.

## Design
Brand: TwiliPlay / STARLIGHT EDITION. Tagline: A little magic in every game. Original magical/literary star logo, restrained gradients, compact rounded panels, readable native-resolution typography. Inspired by Twilight Sparkle and DDLC without copying protected branding/artwork.

| Theme | Background | Panel | Accent | Text |
|---|---|---|---|---|
| Starlight (default) | #191027 | #33204D | #E89ACD | #FFF1FA |
| Literature Club | #FFF0F5 | #E9ACCB | #BD77B0 | #79456D |
| Midnight Library | #101725 | #30354C | #9C91DD | #EEE4FF |

Additional Starlight colors #7952A3 and #CBB4F6. Share rendering code and resources; verify RGB565 contrast. English default, French if budget permits.
Required screens: fast skippable boot splash; home with selected game preview/title/category/launch hint/index; scrollable library of 18 games; settings for themes, optional animations and supported performance modes/FPS diagnostics; information with version, device, count, credits and measured build size. Only show real storage/battery information if supported, otherwise label build-time figures or omit. Use physical arrows, OK, Back and documented shortcuts. Optional subtle procedural stars, event-driven static menu, bounded animations disabled in performance mode. No touchscreen assumptions.

## Performance and Rust
Profile first. Prioritize NumBlocks rendering/memory, NumDrive physics/rendering, NumDash responsiveness, Chess search and Suika collisions. Preserve gameplay speed, input, physics, difficulty, visuals and saves. Explore clipping, batching, dirty regions, layout, compiler options and fixed-point math only when justified. Compare identical workloads and output. Avoid blanket fast-math.
Use no_std Rust, compatible ARM ABI, panic=abort, LTO and appropriate size optimization. Keep unsafe/FFI small and documented; validate alignment, lengths, ownership and pointers. No unwinding across C ABI or duplicate allocators/runtime. Retain EADK ABI and .nwa relocations. Keep C where it performs better. Performance/Balanced/Quality controls only for tested supported options; never promise FPS or battery gains without measurements.

## Tests and release gates
Inspect and run applicable upstream make check, simulator/emulator and test targets. Add catalogue filtering, navigation, theme persistence, bounded memory and module launch/return tests. Use host sanitizers where appropriate. Report revision, toolchain, commands, firmware assumptions, launcher/per-game/total installed flash, RAM and stack/heap assumptions, comparable timings, tests passed/failed/not run, artifact path and blockers. Clearly distinguish emulator from physical results. Owner tests actual N0120: installation, splash, navigation, all 18 games, saves, settings, returning to launcher, stability and FPS. Never claim physical validation until reported. Never require a factory reset.

## GitHub workflow and roadmap
- v0.1.0 Foundation: baseline, allowlist, reproducible build and size report.
- v0.2.0 Starlight: new launcher and three themes.
- v0.3.0 Rust Core: incremental proven integration.
- v0.4.0 Performance: measured game optimization.
- v0.5.0 Polish: effects, diagnostics and documentation.
- v1.0.0 Release: acceptance criteria and owner hardware validation.
Use focused commits/branches, changelog, CI build/check/size reporting and downloadable artifacts. Preserve GPL-3.0 and individual game/asset notices, credit Mason363 and contributors, distribute corresponding source and build instructions. No implied affiliation with Hasbro, Team Salvato or NumWorks. Verify rights for new assets. Defer web studio until personal edition stable.

## First assignment
Work only on Foundation: inspect fork/revision and build requirements, compile baseline, implement exact 18-game allowlist, compile selected edition, run applicable checks, report actual sizes and blockers, and leave a concise progress note. Do not redesign or wholesale port yet. If toolchain unavailable, report missing requirements and reproducible build instructions. Never spawn SubAgents.
