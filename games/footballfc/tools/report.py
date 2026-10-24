"""Collect verified artifacts and measured local validation results."""
from pathlib import Path
import hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[1]
out=ROOT/'output';docs=ROOT/'docs';docs.mkdir(exist_ok=True)
sizes=json.loads((out/'sizes.json').read_text());report=json.loads((out/'arm/report.json').read_text())
engine=json.loads((out/'engine-report.json').read_text());play=json.loads((out/'gameplay-report.json').read_text())
refinement=json.loads((out/'refinement-report.json').read_text())
assert len(report)==5 and all(x.get('clean_exit')for x in report[:4])
data=(out/'FootballFC.nwa').read_bytes();digest=hashlib.sha256(data).hexdigest()
for name,src in {
 'menu':'arm/n0120-menu.png','gameplay':'arm/n0120-match.png','penalties':'arm/n0120-penalties.png','squad':'arm/n0120-squad.png',
 'result':'arm/n0120-result.png','free-kick':'arm/n0120-free-kick.png','setup':'setup.png','career':'career.png','table':'table.png','cup':'cup.png','market':'market.png','training':'training-menu.png','slalom':'arm/n0120-training-2.png','settings':'arm/n0120-settings.png','help':'help-0.png','help-penalties':'help-4.png','penalty-dive':'penalty-dive.png','close-view':'close-view.png','free-kick-wall':'free-kick-wall.png','stadium-touchline':'stadium-touchline.png','penalty-timing-good':'penalty-timing-good.png','penalty-timing-poor':'penalty-timing-poor.png','yellow-card':'yellow-card.png','red-card':'red-card.png','free-practice':'arm/n0120-free-practice.png','free-practice-result':'free-practice-result.png'
}.items():shutil.copyfile(out/src,docs/(name+'.png'))
(docs/'arm-results.json').write_text(json.dumps(report,indent=2)+'\n')
rows='\n'.join(f"| {x['profile']} | {x['max_match_render_ms']} ms | {x['max_render_ms']} ms | {x['peak_stack']} B | Passed |"for x in report[:4])
text=f'''# Validation

Completed on 2026-10-24. These tests use host simulation and ARM emulation;
no physical calculator has been tested.

## Build and memory

- Stripped `.nwa`: **{len(data):,} bytes**.
- Installed payload: **{sizes['profiles'][0]['installed_flash']:,} bytes**, on every supported profile.
- Static RAM: **{sizes['profiles'][0]['static_ram']:,} bytes**, including the 10 KiB drawing strip.
- Maximum observed ARM stack use: **{max(x['peak_stack']for x in report[:4]):,} bytes**.
- Profile: **256 bytes**, compatible with v4; its previously unused stadium byte
  defaults to daylight in older saves. Its inert Python-script backup and filesystem metadata
  bring the total save storage to less than 1 KiB.
- No full-screen framebuffer, heap allocation, audio or downloaded assets.

| ARM profile | Maximum match render work | Maximum render work on all tested screens | Peak stack | Result |
| --- | ---: | ---: | ---: | --- |
{rows}

Compared with v8, static RAM grows by {sizes['profiles'][0]['static_ram']-13001:,} bytes and installed code/data by
{sizes['profiles'][0]['installed_flash']-87528:,} bytes. Original packed pixel art accounts for 8,240 bytes;
there is no decoded atlas or new full-screen buffer.

These timings are emulator estimates in the tested scenes. They do not establish
physical LCD frame rates. Scheduling targets are 30 FPS on N0120 and 25 FPS on
the earlier models; the match simulation always uses fixed 25 ms steps.

## Host tests

- 96 complete matches: all 32 sides across three difficulty settings;
  **{engine['steps']:,} simulation steps** and {engine['goals']} goals in the scripted-controller runs.
  These verify completion and state bounds, not human difficulty.
- 256 random-input runs of 1,800 steps each.
- Swept goal-plane crossing, one-time goal counting, missed shots, throw-ins,
  directional movement and charged-shot release.
- Every pairing appears exactly once in a seven-round eight-team season;
  league table, season advance, rewards, training, contracts, recruitment
  and coach purchases are checked.
- A complete seven-match World Cup win with every one of the 32 selectable
  sides, including group qualification and all four knockout stages.
- Penalty shootout completion and early clinch.
- Directional and backward passes, remembered release direction, through-ball
  lead targets, lob height, varying physical dribble gaps, sprint stamina,
  deterministic front tackles and fouls from behind.
- {play['received']} of 16 isolated ground passes received; 64 isolated finishing
  fixtures exercise both goals and saves, including finesse spin.
- Twelve additional scripted matches exercise possession and finishing.
  Their results do not measure human difficulty or balance.
- Interpolation keeps phase changes discrete while blending continuous movement.
- Sideline projection maps pitch length horizontally, pitch width vertically
  and player/ball height upwards. Screen-relative controls preserve action keys
  and keep shootout controls unchanged. Camera edge fixtures keep the ball in
  view near both goals and both touchlines. The minimap and painter order rotate
  with the view. The side-view mapping and existing save layout remain compatible.
- Profile round trips during competition progress; single-byte corruption at
  every position is rejected. Wrong-size and absent saves are rejected.
- All 32 team-selection cards, compact power/restart HUDs and pause/help screens
  are also rendered with glyph and strip bounds checks.
- Guarded native rendering of all menus, selections, match phases and several
  camera positions; every drawn glyph must fit the 320 x 240 screen.
- 240 native engine/render frames exported for the animated preview.

## v9 receiving, red misses and third-person free kicks

- All 36 isolated ground/through/high receptions complete; another 36 complete
  with a direction held. Eight post-reception steering ticks retain each ball.
  A defender still intercepts a pass through its actual lane. The intended
  human receiver has a 1.33 m horizontal/1.75 m high control window, an immediate
  first touch and a short protection interval; this does not magnetise the ball.
- Four hundred red-timed human penalties physically miss: their target crosses
  outside the post or above the bar, with zero goals. Green remains more precise
  than amber and does not bypass the keeper. High shots above the upright are
  correctly classified as misses rather than woodwork.
- Third-person free kicks use the actual ball, wall and goal geometry. Yaw keeps
  the goal centred from either side/half; painter order, player scale, grass and
  aiming dots share that projection. The field camera resumes after the attempt.
- A dedicated Free kicks menu entry repeats unlimited shots with varied positions,
  goals/attempts, new-position input, pause/help and clean exit. Forty-eight host
  kicks reset correctly; the profile receives no match rewards or progression.
- Linked ARM tests enter this mode, fire and repeat, choose another position,
  leave through confirmation and retain profile statistics on every profile.
  They also fire a real red-timed penalty and confirm that no goal is awarded.
- Perspective grass is clipped and filled in spans; the camera basis is reused.
  No framebuffer or decoded asset buffer is added. The home caption requested
  for removal is absent. See [v9 details](gameplay-v9.md) for controls and scope.

## v8 opponent errors, set pieces, discipline and scenery

- Contextual direction changes can wrong-foot nearby opponents. Two hundred
  fixtures per difficulty produce 148 / 117 / 97 deceptions: variable results
  and a clear difficulty ordering, not a human success-rate measurement.
- Delayed chaser reads, varied decision intervals and skill/pressure/stamina-based
  pass dispersion replace perfectly updated reactions.
- A contracting red/amber/green ball ring controls strike accuracy on penalties
  and free kicks. Paired fixtures verify an actual reduction in shot dispersion.
- Three/four-player walls jump and block reachable balls. High balls can clear
  them. Twenty-four physical free-kick fixtures produce goals and defensive
  contacts. Curl changes the real trajectory; eight help pages document keys.
- Careless contact, reckless yellows, second-yellow dismissal and dangerous
  straight reds are tested, including an automatic rear-charge foul. Sent-off
  players cannot intercept, mark, receive or restart. A replacement keeper is
  positioned at goal and preserved at kickoffs. Fewer than seven players ends play.
- A penalty-area foul enters a one-kick penalty sequence and returns to the match
  with one goal, a goal kick or a live rebound; both attacking directions are tested.
- The linked ARM app uses actual keys for curl, height, shot release and all eight
  help pages. A physical rear collision awards red and a match penalty, which is
  then played to completion on each profile.
- Benches/substitutes, stewards, camera operators and seating banners add 1,584
  flash bytes to the original art pack. No decoded atlas or heap is added.
- See [v8 details](gameplay-v8.md) for mechanics and practical limits.

## v7 close view, pixel art and gameplay corrections

The [gameplay audit](gameplay-audit-v7.md) records findings, causes and corrections.

- Close sideline projection follows both pitch axes and anticipates the ball,
  leaving room ahead of the carrier. It never follows a newly selected receiver.
  A 9 m circle radius spans about 84 pixels at centre; a 1.75 m player about 16.
  Both share the same perspective. Edge fixtures keep the ball above the lower HUD.
- Original player art: five angles, idle/four running contacts/kick, mirrored
  as needed. Three animated supporter tiles and four travel-based ball frames
  complete the initial 6,656-byte v7 art pack. Materials recolour per team and individual.
- Fixed remote carrier actions, empty tackles blocking loose-ball receptions,
  stale receivers after parries, blind forward kickoffs, distant free-kick shots,
  named-receiver-only completion statistics and partially clamped pass power.
- Receptions predict a reachable flight; arrows adjust its point and XNT/ALPHA
  restore full manual steering.
  Vector acceleration and ball-speed limits avoid diagonal advantages, while
  sharp turns trigger fresh physical touches.
- {refinement['sprint_turns_retained']}/{refinement['turn_ticks']} sprint-turn ticks retain ownership, with maximum
  observed gap {refinement['max_dribble_gap_q']/256:.2f} m. Pass fixtures receive
  {refinement['ground_received']}/12 ground, {refinement['through_received']}/12 through and {refinement['high_received']}/12 high balls.
- Dedicated fixtures check owned-dribble keeper collection, actual control of
  the carrier, loose-ball OK, kickoff reception in both halves and restart choice.
- Six settled-support fixtures cover three formations in both directions:
  wide defenders, a holding midfielder, short options and the actual wide-player
  roles of 4-4-2/3-4-3. One chaser engages while the others maintain their shape.
  Passing-drill players return to separate stations between receptions.

## Stadium, penalties and accessibility retained from v6

- The field is 110 x 74 world metres (previously 105 x 68). The opening is
  8.125 m wide and 2.65625 m high, approximately 11% wider and 9% higher.
  These are arcade adjustments, not regulation dimensions.
- The elevated sideline view is closer in v7. Edge fixtures
  keep the ball visible at both ends and touchlines. Goal frames and marking
  rectangles use the same dimension constants as collision detection.
- Large shootout silhouettes retain articulated limbs, kit shading, hair/skin
  variation, directional heads, boots and planted/striking legs. Stands use
  original small spectator tiles, raised scarves, stairs, rails and roof trusses.
  No downloaded image assets were added.
- The dark match-programme interface uses charcoal, ivory and restrained green;
  home slogans and the floating jersey panel are removed. Eight task-based help
  pages pair keys with a short action and explanation. The static home no longer
  continuously redraws, and purchase messages clear when leaving a menu.
- Shootouts have a 12-tick run-up with a stationary ball, a power-dependent
  parabolic flight, height aiming, committed keeper motion with reaction time,
  a landing animation and physical post/goal/save/miss results. First-five kick
  marks preserve the actual order. The ball keeps moving after contact.
- Host regressions check run-up/contact timing, monotonic ball travel, bounded
  keeper motion, separate outcomes, one-time awards and 300 deterministic shots
  over the three difficulties. These are fixtures, not human success rates.
- Opponents move more slowly, tackle less automatically and leave longer decision
  intervals. Their keeper reacts later, dives more slowly and has a smaller
  catching radius. Easy/Normal/Hard remain distinct.
- Kickoff clearance is recomputed after putting players in their own halves,
  preventing an opponent being left next to the ball. An extra-width shot
  confirms the enlarged opening is used by scoring.
- The native renderer exports a complete shootout animation and checks every
  help page and animation stage under strip/glyph bounds guards.

## Earlier regressions and content

- Both teams stay in their own half at either kickoff.
- Swept goalkeeper contacts are resolved before a goal, an unreachable outfield
  player cannot mask a reachable catch, and hand-catching height is limited to
  the keeper's own penalty area.
- A held tactic key changes strategy once per press. All four approaches finish
  matches in all three formations without invalid positions or stamina.
- All three training modes reach their time limit without half-time. Passing
  completes ten physical receptions; slalom completes all six gates through
  steering inputs. Six goal crossings finish the shooting drill; a separate movement/charged-shot
  controller reaches all six with real movement/charged-shot inputs. A parried loose ball times out safely
  instead of stalling that drill. Training does
  not change the profile's match statistics, credits or competition progress.
- A real v4 save fixture still loads; all three venue values round-trip and an
  invalid venue is rejected. The explicit save remains 256 bytes.
- All training screens and three lighting variants pass native glyph and drawing
  strip guards. There are no heap allocations or full-screen buffers.

## ARM tests

The actual linked app is driven with its real keyboard path on N0120/Epsilon,
N0110/Epsilon, N0115 RAM and N0110/Upsilon profiles. It checks menu setup,
sprint/movement in both sideline screen axes, switching, charged shooting,
through-ball and lob keys, sideline free-kick aiming and power, ALPHA
finesse shooting, pause/help/resume, a one-time
result reward, career fixtures, training and contracts, shootout aiming and
goalkeeper movement and height selection, a real run-up/flight/dive/result, all
eight help pages, all three training menu/result/retry/leave paths, a real
training pass and shot, tactic-key latching, saved stadium selection, safe Home/Power exits and clean app RAM.

A reinstallation fixture retains only Python scripts, then verifies that the
app restores its career from the backup. The other calculator files and firmware
RAM remain unchanged. Corrupt saves, full storage with a safe explicit exit,
and held launch keys are also checked. Match-end fixtures skip the full match
already covered by the host tests.

README screenshots of the home, gameplay, squad and penalties come from the ARM
app. The close-view still is a host scene exercising the supplied reference's
proportions. The animated match and shootout previews are scripted host runs of the same
native engine and renderer, not recordings from physical hardware.

## Scope

The game was recreated from the public reference's screenshots and description.
Its code, pixel art, procedural graphics, fictional player data and career economy are
new interpretations. It is not a verified identical port. Competition suspensions,
substitution and extra-time systems are outside this arcade version; see the
[README](../README.md).

SHA-256: `{digest}`
'''
(docs/'validation.md').write_text(text,encoding='utf-8',newline='\n')
target=REPO/'build/apps';target.mkdir(parents=True,exist_ok=True)
for name in ['FootballFC.nwa','FootballFC-v9.nwa']:shutil.copyfile(out/'FootballFC.nwa',target/name)
assert (target/'FootballFC.nwa').read_bytes()==data
print(json.dumps(dict(sha256=digest,nwa_bytes=len(data),installed_bytes=sizes['profiles'][0]['installed_flash'],static_ram=sizes['profiles'][0]['static_ram'])))
