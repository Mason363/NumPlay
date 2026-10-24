# Football FC v8

## Fallible opponents

A direction change of at least a right angle, while moving with the ball above
2 m/s, can deceive defenders within 5 m. The chance varies with difficulty,
skill and fatigue. A successful feint commits the defender to a short continuation
of his existing movement for 13–22 simulation ticks (0.325–0.55 seconds). He
cannot instantly tackle during that recovery. A 36-tick cooldown limits repeated
wiggles. The defender remains physical and other defenders can still cover.

Chasers now read target positions at intervals. Decisions have varied timing,
and opponent passes have skill/pressure/stamina-dependent dispersion. These
errors use the match seed and do not depend on whether the human is winning.

Two hundred identical-context feint fixtures per difficulty produced 148, 117
and 97 successful deceptions. These demonstrate variation and difficulty ordering;
they are not human dribbling success rates.

## Timing and set pieces

The ring around the ball contracts and changes from red to amber to green on
a repeating 1.6-second cycle. Releasing on green reduces shot dispersion; the
keeper, aim, height and power still affect the outcome. Shootouts retain their
run-up, physical ball flight, keeper dive, goal/save/miss/post and kick tally.

Free kicks use aiming, adjustable power, three target heights, signed curl and
the same timing ring. The preview shows a clean strike; its colour/trajectory is
not a guarantee of a goal. Three or four defenders form a nearby shooting wall,
jump, and deflect balls within their reach. Higher balls can clear it. Distant
restarts and OK provide short-pass alternatives.

Twenty-four free-kick fixtures covering power/height/curl variants produce both
goals and defensive contacts. Separate fixtures prove that a jumping wall blocks
a reachable ball while a higher one passes, and that curl affects real velocity.
One hundred paired penalty fixtures verify that good timing reduces aim error.

## Discipline

The previous simple foul system is extended with careless/reckless/dangerous
contact, yellow cards, second-yellow dismissals and straight reds. Fast rear
charges can also be called without pressing tackle. Legal frontal ball-winning
and ordinary shoulder contact remain possible.

Dismissed players are excluded from simulation, rendering, passing targets,
marking, offside lines and restarts. Goalkeeper replacement and kickoff takers
are adjusted. A team with fewer than seven active players forfeits. Cards reset
at the next match; profile storage remains 256 bytes.

A penalty-area foul enters a one-kick animated sequence, then returns to the
match. A goal counts once; a miss gives a goal kick; saves/posts can leave a live
rebound. This sequence is separate from competition shootouts.

The discipline model is an arcade approximation. It does not implement every
professional law, advantage/VAR, deliberate handball or competition suspensions.

## Scenery and resources

New original pixel art adds covered benches and substitutes, high-visibility
stewards, camera operators/tripods and seating banners. All sit around the pitch;
players and match text remain unobstructed. They use the existing packed-material
renderer and its 10 KiB strip. Artwork grows from 6,656 to 8,240 flash bytes, with
no decoded atlas or asset heap.

Runtime match state grows by 204 bytes (612 bytes across current/interpolated/
previous states). The profile/save format remains unchanged. See the measured
[validation report](validation.md) for current RAM, payload, native file size and
emulator timings on all supported profiles.

Tests cover complete matches/random inputs, all earlier regressions, contextual
feints, fouls/card severity, active restarts after dismissals, keeper replacement,
forfeits, single match penalties in both directions, timing dispersion, wall
reach and physical curl. Native ARM keyboard tests also adjust curl/height,
release a shot, trigger a real dangerous-contact red and finish its awarded
match penalty. All eight help pages and the new scenery/card/timing scenes use
glyph and drawing-strip guards. No physical calculator was available for testing.
