# Camera, starting grid and Free Ride

This records the v3 camera change. The current renderer is described in
[the v4 world-space overhaul](driving-v4.md); Free Ride and grid behavior below
remain applicable.

The camera now follows approximately 45% of the player's lateral position with
gradual settling. The remaining displacement moves the car on screen. A bound
keeps the entire vehicle visible near the barriers. Road and vehicle projection
share this offset; the rear tires remain above the dashboard. Steering changes
the rear sprite's angle, and a subtle speed-dependent bob adds motion feedback.

The race grid uses alternating left/right slots. Adjacent slots are at least
15 metres apart longitudinally and same-file rows are 70 metres apart. The
nearest car starts 40 metres ahead rather than crowding the player's bumper.

Free Ride is the third race mode in Play > Race mode. It starts immediately,
loops the selected course, and uses 3/4/5 traffic cars for Light/Normal/Busy.
Traffic keeps its lane and travels slower than the player's maximum speed.
Cars passed by more than 120 metres are recycled 700 metres or more ahead,
with a gap from existing traffic and a newly chosen lane, body and pace.

Distance and overtakes replace race position and lap counters. There is no
damage-based ending, ten-minute timeout, finish-line prize or record update.
Contacts still reduce speed. The chosen mode is saved using the existing
80-byte profile; older profiles remain readable. Periodic distance rebasing
preserves the scenery's phase and accumulated ride distance during long rides.

Tests cover grid separation, camera bounds, Free Ride beyond the former time
and lap limits, traffic recycling, overtakes, save compatibility and unchanged
race economy. A 160,000-step ride exercises more than 53 minutes of simulation.
