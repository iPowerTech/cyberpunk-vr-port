# Basilisk camera aiming and head-following HUD

The user reported that the Basilisk appeared to fire along the barrel instead
of the view and suspected the VR PhysicalRay override. They also requested a
zero free-look cone for the Basilisk HUD, following every head rotation.

## Baseline investigation (PID21252)

During the requested firing window, PhysicalRay recorded 2,115 calls but zero
player claims, origin writes, forward writes or hand recoil. The other VR shot
redirects were disabled and their mutation counters were also zero, including
the orientation-provider override. The published handheld muzzle was invalid
and the handheld weapon flag was zero while driving this tank. The observations
do not support blaming our handheld PhysicalRay override for these shots.

The native targeting system's crosshair and default crosshair directions agreed
with CameraSystem's active camera forward to float precision. This demonstrates
agreement at the targeting input, not proof of the final projectile impact.
The vehicle exposes a cannon, missile launcher and two countermeasure launchers.
No global weapon-aim setting was changed during investigation.

Evidence: `build/basilisk-fire-20260925/before-21252.json` and
`aim-state-21252.json`; native inspection outputs are in the same directory.
The diagnostic gate was restored after the probe.

## HUD change

`hud tank` now uses its own HUD capture/composition channel. Its quad copies the
full predicted HMD orientation (yaw, pitch and roll) and places its centre on
the HMD forward ray at the existing HUD distance. This implements an actual
zero cone with immediate following, bypassing both angular catch-up and the
three-second rest timer. Main HUD and interaction/loot channels keep their own
anchors and controls. Existing Basilisk element layout settings still apply.

The additional channel is captured only when it has native sprites. Its consumer,
masking handshake and shutdown are independent. The XR submission array accounts
for both eye layers of every HUD channel plus the scene, FPS and settings layers.

All 34 HUD tests passed, including a new D3D12/OpenXR-quad test for sub-cone head
movements, combined rotations, immediate panel position/orientation, independent
texture identity, and eye visibility. Release and live aiming validation follow.
Actual impact alignment must be checked with the head-locked reticle before
making a separate change to the native vehicle firing path.

Release built and installed at 12:34 MSK. SHA256:
`6cdfd5c3dcf99719417137c970094a7d3410c76f24980b88f790ea03b7ef6fc3`.
Matching DLL/PDB: `build/basilisk-fire-20260925/candidate-01-hud`.
Backup/deploy report: `build/roomscale-plugin-deploy-20260925-123402`.
The native firing path is unchanged in this candidate so the visual comparison
isolates HUD following. The user confirmed HUD following works, but shots still
miss the reticle. A separate native vehicle aim correction is required.

## Native weapon target

In the subsequent real-headset run (PID428), the tank's target at owner+0xB14
was 44.26 degrees off the current MAIN camera direction. The completed stereo
camera pair remained coherent. `native-target-428.json` records both vectors.
The vehicle was player-driven and the external gaze steering hook was active.

`vehicleTankBaseObject` vtable+0x3C8, RVA0x25FE354, returns owner+0xB14 for the
cannon. Weapon index1 uses the salvo target cached at +0xBA0 with native spread;
vtable+0x3C0 (RVA0x25FE66C) copies the current target into that salvo cache.
The firing loop at RVA0x6669CC consumes this target and calls RVA0x25F9A7C.
The radial branch at vtable+0x3B8 emits a five-way pattern and must not be
indiscriminately redirected (countermeasures use separate weapon entries).

## Native targeting correction

Tank vtable+0x3A0 (`0x25FA8E4`) performs the real collision query and writes
the target at +0xB14. It gets its camera from the vehicle camera manager's weak
handle at +0x528. Calls at `0x25FAA04` / `0x25FAA39` read that manager's raw camera
position (`0x2611A30`) and quaternion (`0x2611AFC`). Those getters read the native
camera components, before the generic MAIN serializer adds the HMD transform.
This explains why the general targeting-system crosshair was correct while the
tank's private target was not.

The new hook scopes one completed MAIN pose around that native tank targeting
call. Only the two verified getter callsites receive the replacement position
and quaternion, and only for the player's currently driven TankBaseObject. The
position is the eye-centred MAIN position; orientation includes full HMD pitch,
yaw and roll. Original getters still execute, and only their caller-owned output
buffers change. The engine's ray range, collision filter, hit resolution, weapon
spawn position, missile salvo cache and spread remain in the native path.

The scope is thread-local, masks an outer scope during unrelated nested calls,
and restores itself on return. Other camera-manager readers and NPC vehicles do
not inherit it. No raw camera/entity address is retained for dereferencing.
Player-state refresh locks the existing mounted-vehicle weak handle while
publishing its identity; both steering and aiming compare their engine-owned
vehicle argument against that identity. MAIN aim publication is independent of
the external-view mirroring gate, so FPP can supply aim without acquiring TPP
steering or changing the VRCAM route. Missing/stale (100 ms) MAIN data fails closed.

12 camera tests and 19 pose/ABI tests passed. New tests exercise eye centring
under pitch/yaw, both eye assignments and multiple IPDs; nested/inactive scopes,
exception unwinding and isolation from another thread. All 26 native byte
signatures and both camera-getter call destinations match the installed 2.31 EXE.

Release installed at 13:00 MSK. SHA256:
`77c8a5d870cc4d3be42c815a68f777c562d049da95347d2f0976cc5758374541`.
Checkpoint: `build/basilisk-fire-20260925/candidate-02-aim`.
Backup/report: `build/roomscale-plugin-deploy-20260925-130039`.
Runtime target-angle and impact verification is pending the user's relaunch.

The first candidate at 13:00 did **not** include the new PanzerWeaponAim.cpp
object. CMake regenerated its source list, but that running MSBuild invocation
compiled the previously loaded project items. The missing PE export and missing
boot registration exposed this before a usable live measurement. A second build
compiled the file. Future installation in this investigation explicitly verifies
the aiming exports from the PE export table, in addition to checking the hash.

## Cannon launch convergence

RVA0x25F9A7C creates the shot's direction provider from muzzle+barrelForward, while
the separate target argument becomes projectile targeting data. The camera target
alone therefore does not guarantee an unguided cannon round follows the view.
The new launch hook directs the cannon from its native muzzle onto the native
collision-tested camera target, preserving the other launch arguments. It is
restricted to the exact targeted-launch caller at 0x1ECD51C, the player's Tank,
and the first active weapon's identity. Live slot inspection confirmed index0 is
the cannon, index1 the missile launcher (both targeted), and indices2/3 the radial
countermeasure launchers. Missile launch direction and radial patterns remain
native; the missile target still uses the corrected camera query and native salvo
latch/spread. `currentMainPoseId` in shot diagnostics identifies the fresh MAIN
available at firing, not an asserted receipt for the earlier target query.

13 camera/scope/convergence tests and 19 pose/ABI tests passed. All 28 native byte
signatures and all three relevant call destinations match. The ready DLL contains
both aim and cannon diagnostic exports. Installed at 13:23 MSK:
`9fc7cd76dce415c8cbd5528f1d6e77dafb3685542434f7e8d06b6f18011ddd5c`.
Checkpoint: `build/basilisk-fire-20260925/candidate-03-full-aim`.
Backup/report: `build/roomscale-plugin-deploy-20260925-132320`.

## Final live result

PID25704 loaded the verified DLL and logged `PanzerWeaponAim ok`. The user tested
in a real HMD and confirmed the HUD and shooting work correctly.
Over the 20-second diagnostic capture, 821 of 823 native targeting updates used
the completed MAIN pose; two updates retained native behavior because a fresh
MAIN pose was unavailable. Maximum target-ray deviation was 0.0000556 degrees.
All five observed cannon launches used the corrected direction, with maximum
deviation from muzzle-to-target of 0.00000121 degrees. The diagnostic gate was
restored and independently read back as zero. Evidence:
`build/basilisk-fire-20260925/aim-live-25704.json`.
