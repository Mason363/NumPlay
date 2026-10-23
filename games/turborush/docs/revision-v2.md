# Gameplay and presentation audit

The initial implementation had basic state and memory tests, but its presentation
and interaction model needed a deeper review. This revision addresses the
following concrete issues.

| Issue | Change | Validation |
| --- | --- | --- |
| Abrupt lateral movement and steering artwork unrelated to input | Progressive steering, lateral settling, speed-dependent response and actual steering views | Rest, reversal and real ARM key paths |
| Contact area much narrower than the visible vehicles | Wider contact shape, speed-dependent impact and controlled separation | Contact / no-contact fixtures at different offsets |
| One global invulnerability interval allowed passing through other rivals | Independent cooldown for each rival | Repeated contact fixture |
| Braking could still drain and activate nitro | Boost requires throttle and excludes braking | Combined-input fixture |
| Low-charge nitro could alternate on and off | Minimum recharge reserve after depletion | Held-boost depletion fixture |
| Barrier damage could repeat too quickly | Barrier cooldown and inward response | Continuous outward steering fixture |
| Bots could repeatedly reconsider opposing lanes | Periodic decisions, clear-lane scoring, smooth lane changes and following distance | Full races and random-input sessions |
| Finish-line vehicles could remain parked; progress could override finish time | Finished rivals keep moving, finish time takes priority over distance | Arrival-order fixture and six-race ARM series |
| Road bending was based on a crude average of distant curves | Integrate curvature ahead, anchor the camera behind the car and use coherent perspective | Every course, edge positions and dense traffic |
| Decorative minimap could suggest a route unrelated to the bends | Replace it with an upcoming-turn indicator based on actual course data | Renderer reads the same curvature as the simulation |
| Settings and actions shared a dense nine-line menu | Four-action home, separate race setup, records and garage | Every screen and real ARM navigation |
| Small bottom-edge text and crowded HUD | Explicit footer region, opaque dashboard, bounded columns and native fonts | Glyph bounds at all selections and maximum values |
| Primitive cars and roadside geometry limited visual quality | Original panoramas, two car bodies, steering views and eight scenery sprites | Actual indexed-asset renders in every landscape |

The existing 80-byte save format remains compatible. Nothing is uploaded to
GitHub by this local revision. Physical calculator feedback is still needed
to confirm the feel of the revised controls and LCD pacing outside emulation.
