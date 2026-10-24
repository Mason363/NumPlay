"""Collect native captures, validation and the installable local artifact."""
from pathlib import Path
import hashlib,json,shutil
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[1];out=ROOT/'output';docs=ROOT/'docs';docs.mkdir(exist_ok=True)
sizes=json.loads((out/'sizes.json').read_text());arm=json.loads((out/'arm/report.json').read_text());assert len(arm)==5 and all(a['clean_exit']for a in arm[:4])
data=(out/'WorldsHardestGame.nwa').read_bytes();digest=hashlib.sha256(data).hexdigest()
for name,source in {'menu':'arm/n0120-menu.png','gameplay':'arm/n0120-level-01.png','dense-stage':'arm/n0120-level-12.png','coins':'arm/n0120-level-18.png','final-stage':'arm/n0120-level-30.png','help':'arm/n0120-help.png','tip':'arm/n0120-tip.png','credits':'credits.png','pause':'pause.png','finish':'finish.png'}.items():shutil.copyfile(out/source,docs/(name+'.png'))
(docs/'arm-results.json').write_text(json.dumps(arm,indent=2)+'\n')
raw=(out/'motion.raw').read_bytes();size=320*240*2;frames=[Image.frombytes('RGB',(320,240),raw[i:i+size],'raw','BGR;16')for i in range(0,len(raw),size)]
sheet=Image.new('RGB',(320*3,240*2))
for i in range(6):sheet.paste(frames[i*(len(frames)-1)//5],(i%3*320,i//3*240))
palette=sheet.quantize(colors=128,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE);frames=[im.quantize(palette=palette,dither=Image.Dither.NONE)for im in frames]
frames[0].save(docs/'gameplay.gif',save_all=True,append_images=frames[1:],duration=[33,33,34]*(len(frames)//3)+[33]*(len(frames)%3),loop=0,optimize=True)
sheet.save(out/'replay-sheet.png')
rows='\n'.join(f"| {r['profile']} | {r['max_play_render_ms']} ms | {r['max_render_ms']} ms | {r['peak_stack']} B | Passed |"for r in arm[:4])
text=f'''# Validation

Completed on 2026-10-24 using host C tests and ARM emulation. No physical
calculator has been tested. These results do not establish a physical LCD
frame rate or claim that a human completed all 30 stages on this adaptation.

## Build and resources

- Stripped `.nwa`: **{len(data):,} bytes**.
- Installed payload: **{sizes['profiles'][0]['installed_flash']:,} bytes** on every tested profile.
- Static RAM: **{sizes['profiles'][0]['static_ram']:,} bytes**, including the 10 KiB drawing strip.
- Maximum observed ARM stack: **{max(a['peak_stack']for a in arm[:4]):,} bytes**.
- Save profile: **72 bytes**; the inert Python backup and record names bring
  total calculator save storage to less than 320 bytes.
- All stage/image/animation tables remain in flash. No heap, decoded atlas,
  audio, full-screen framebuffer or runtime asset download.

| Profile | Maximum active-play render work | Maximum on all tested screens | Stack | Result |
| --- | ---: | ---: | ---: | --- |
{rows}

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

SHA-256: `{digest}`
'''
(docs/'validation.md').write_text(text,encoding='utf-8',newline='\n')
target=REPO/'build/apps';target.mkdir(parents=True,exist_ok=True)
for name in ['WorldsHardestGame.nwa','WorldsHardestGame-v1.nwa']:shutil.copyfile(out/'WorldsHardestGame.nwa',target/name)
assert (target/'WorldsHardestGame.nwa').read_bytes()==data
print(json.dumps(dict(nwa_bytes=len(data),installed_bytes=sizes['profiles'][0]['installed_flash'],static_ram=sizes['profiles'][0]['static_ram'],sha256=digest,gif_frames=len(frames))))
