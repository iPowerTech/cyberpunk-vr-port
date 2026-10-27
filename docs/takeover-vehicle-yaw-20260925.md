# Moving takeover camera yaw

The user reported the same yaw hitch in MAIN and VRCAM, also visible on the
monitor, when the DLC car turns or makes steering corrections. This extends
the previous vehicle yaw transport fix, not the HMD pose matching or Framegen.

## Live scene and capture

PID 23636, DLL `684e69c82353...`, Virtual Desktop / real headset. The game is
in scene tier 3, vehicle state 7. The controlled object is `SurveillanceCamera`
EntityID 10849869; the player is mounted in `Vehicle.q304_get_away_car`, EntityID
9016035. The selected lens is `cameraComponent`; its `entHardTransformBinding`
binds to `senseComponent`, whose binding uses the `FacingDirection` slot.
`DevCamInLocate=2` and the takeover path are already active, as for the AV turret.

At the user's instruction, opened/closed the native pause menu using the
in-game menu controller via WolvenKit/CET. Recorded 8,132 read-only snapshots
over 20 seconds, then restored pause. Diagnostic gate was restored in `finally`.
One lens identity and one director remained selected; 833 takeover pushes and
1,666 final camera callbacks occurred. Each final eye's position advanced
without reversals. The VRCAM *component* temporarily returns to its player
attachment during native updates, but those excursions did not appear in the
sampled final cameras; they are not evidence of visible three-metre teleports.
Snapshots and matching PDB-resolved addresses are in
`build/moving-device-20260925/`.

## Change

`UseNativeMainHeading` deliberately excludes device cameras. As a result the
vehicle-owner rotation could request a shared recomposition for ordinary
vehicle/scene cameras, while takeover still used a cached lens yaw once per
HMD epoch. A moving device owner did not participate in that shared key.

Added a separate `VehicleStereoHeading` instance for the exact takeover lens
and its owner. Only a real lens callback observes native aim relative to its
owner's full rotation, before VR writes. Other player/VRCAM callbacks transport
that relative aim through the device owner's latest rotation; they cannot
replace it with their player attachment. A changed device owner rotation can
now recompose the shared quaternion while preserving the same HMD sample.
Switching/releasing takeover resets this source. The AV route without a lens
callback retains its existing published-aim fallback.

Locate takeover, camera positions, Framegen and pose-identity lookup remain on
their existing paths. One new recomposition counter is behind the runtime
debug gate. No setting or per-frame logging was added.

## Validation

Seven targeted tests pass: native camera pair, frame aim, vehicle stereo/owner
identity, moving vehicle yaw, scene camera heading and the new takeover test.
The takeover regression covers three callback orders, different player/device
owners, four turning speeds plus alternating microcorrections, reused HMD
samples, unavailable owner reads, camera switching and takeover exit. Simulated
old maximum yaw lag is 5.08178 degrees; transported error is 0.0000546415 degrees.

Release build and `git diff --check` pass. Candidate DLL SHA256:
`c5ea238521dcd8b5a3758f31700ff0b2683b0f0b1d3a1a1a379b9b68c7d8a479`.
After restarting with this DLL, the user confirmed the yaw fix works in the
moving car takeover. No further pose/Framegen changes were needed.
