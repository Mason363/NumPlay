# World-space driving overhaul

The earlier renderer calculated a different screen-space curve for every road
row. It also rounded progress to whole metres, coupled the horizon and focal
length to an artificial hill cycle, and drew wide scrolling terrain bands.
Moving the player sprite alone could not correct those sources of inconsistent
motion. This revision replaces that rendering model.

## Geometry and motion

- Integrate the existing course's curvature into a fixed 8-metre world grid.
  Rebuilding the local cache changes its coordinate origin, not the road shape.
- Sample the road with tangent-continuous Hermite interpolation. Preserve the
  player's fractional distance through the final perspective projection.
- Use one camera, focal length and ground plane for the road mesh, roadside
  objects, other cars and the player's tire contact point. Clip quads against
  the near plane before perspective division.
- Keep the horizon level. Remove fake hill breathing, scrolling grass/tarmac
  bands, detached sea wedges, artificial vehicle bob and camera shake.
- Let heading settle progressively after steering input; lateral motion and
  forward speed follow that heading. Orient vehicle sprites relative to the
  camera and insert the player into the same depth order as traffic and scenery.
- Interpolate render positions between 50 Hz simulation steps. A traffic
  respawn or distance rebase is not interpolated across the scene.
- Schedule rendering at 20 ms on N0120 and 33 ms on N0110/N0115. Firmware/model
  detection also applies when the game is built as a NumPlay module.

These are still arcade road-relative physics and billboard vehicle sprites,
not a full polygonal car simulation. Course distances, race modes, the Free Ride
mode, garage economy and the existing profile format remain compatible.

## Verification

The motion preview is exported from the actual C renderer and game simulation:
[240-frame preview](driving-v4.gif). It includes acceleration, lane changes, a
car contact, a pass and a bend. The preview runs at about 30 FPS; it is not a
recording of a physical calculator.

Tests check continuity of projected landmarks and road-marking coordinates at
cache boundaries, growth of fixed landmarks while approaching, tire contact
alignment on every course, render interpolation and the absence of interpolation
through traffic respawns. Existing full-race, random-input, save, RAM-guard and
keyboard-path tests remain in place. See [measured validation](validation.md).
