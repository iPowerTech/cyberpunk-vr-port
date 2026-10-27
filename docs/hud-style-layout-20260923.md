# HUD appearance, tracking and per-element layout

The panel previously copied the separate UI textures without the game's final HUD
composite. This lost its literal x2 color gain, coverage shadow and glow. The new
capture composes sprites, builds five raw mips and a half-size blurred pyramid,
then runs the existing reconstructed native HUD shader in transparent-layer mode.
Its output is premultiplied color with alpha `1 - cover * shadowTransmission`.
Available native HUD/frame/exposure buffers supply the same parameters; fenced
default buffers handle missing constants during startup. Each producer slot owns
its descriptors, intermediates and buffer references through its GPU fence.

The original scene-luminance attenuation of glow cannot be reproduced exactly in
an independent XR overlay. Here that small modulation uses a black reference;
brightness, shadow and glow have dedicated controls. Ordinary world composition
continues using the original shader path.

Tracking changes:
- Follow steps use nanosecond predicted display time, replacing the coarse
  GetTickCount64 delta that alternated between zero and large yaw steps.
- HUD head/eye poses are located just before submission at the actual display time,
  without the optional prediction lead used elsewhere in the game camera path.
- Near-vertical head directions preserve the last usable heading.
- Default stereo mode uses left/right-only quads. Adding each eye's translation to
  its quad makes both see the same angular direction, while preserving their common
  orientation and the runtime's canted/asymmetric view optics. Optional Stereo depth
  restores a shared physical plane at the selected distance.
- Follow can use the head's independent cone or the physical body's tracking-space
  yaw. The latter is the recenter-base yaw plus the existing suspension-aware body
  follow offset.

All HUD settings now live in the ImGui HUD tab. Thirty named elements expose X/Y
offsets as percent of canvas, centre-based size, opacity and visibility, plus resets.
Settings persist in vrport.ini as `hud_element_<key>=x,y,scale,opacity,visible`.
Hidden elements keep their original slot masked. Display status identifies elements
that are not currently visible. Camera/menu settings stay in General.

The minimap intermittently shifted 391 pixels left. Live inspection found the
hidden `minimap_q305` still reserving that width in its horizontal container:
`TopRightMain` measured 1181 pixels instead of 790. Restoring the slot's raw
`affectsLayoutWhenHidden` bit did not invalidate its ancestors; calling SetVisible
with the already fake-hidden value also skipped the native callback. RestoreMask
now delivers the real visibility transition and invalidates layout when restoring
that flag. IDA verified SetVisible RVA 2E367C, ImageWidget callback 4FC34C and layout
invalidation 2EFEC8; the new callable address has a signature guard. A live toggle
of the same layout flag immediately restored the correct 790-pixel container.
No arbitrary minimap offset was added. The layout latch also holds the previous
rectangle during a partial hierarchy rebuild.

The reported eye difference was observed in OpenXR Simulator Preview. The HUD
quads' directions are correct for the runtime's -54/+40 and -40/+54 degree optics,
but that did NOT establish correctness of the final preview. A fresh composed
capture on PID 12008 confirmed the mismatch. The simulator blits the game's raw
projection images (both symmetric +/-54.27 degrees) without reprojecting them to
those optics, then formerly drew quads through the different runtime frusta.
This mixes two cameras within each eye. The correction is in the simulator's
mirror compositor, which now uses the submitted image pose/FOV for its overlays.
The application's physically correct XR quad geometry and the configured headset
profile remain unchanged. See hud-preview-20260923.md for the separate runtime
patch, regression tests and deployment. A fresh composed screenshot on PID 27216
and the user's visual check confirm aligned HUD placement in Preview after that
runtime update. Later physical-headset testing found monocular duplication with
this two-layer angular mode; switching to one shared quad removed it. See
`hud-headset-20260923.md`. Physical-headset acceptance applies to stereo-depth
mode1, not the simulator-validated two-layer mode0.

Verification: twenty-one C++ tests pass, including GPU pixel readback for native x2 gain
and a black shadow outside the source rectangle, 64-sprite descriptor isolation,
capture leases, XR timeout/recenter/tracking, eye alignment across 80 moving/turning
poses, body/head mode independence, precise display-clock steps, vertical look,
layout transitions, transforms and settings serialization. Log: build/hud-cone/v3-tests.log.

Live motion check on PID 24880 captured 803 distinct HUD submissions during a
160.43-degree head sweep with translation and pitch. There were 477 consecutive
frame pairs: display-clock error at most 4.64e-10 seconds, no zero deltas, maximum
follow speed 3.000006 rad/s against the 3 rad/s limit, zero settled yaw drift, and
maximum eye-translation cancellation error 2.39e-7 metres. The simulator pose was
restored; no keys, focus changes, INI changes or game-memory writes were used.
Evidence: build/hud-cone/v2-live-motion.json; harness tools/hud_tests/live_motion.py.

Deployment: build/roomscale-plugin-deploy-20260923-122956.
DLL SHA256: `0135c967a8047546dd8e5286361ad5cb44d0a29cb2fdb7e0b0a6c5286b747230`.
PDB SHA256: `e9eb3c2ff18a6859260f6eb81e54128e85a1e5a5ffa59dd0e8c626eecf7cfe33`.
INI/calibration preserved. The user confirmed restored highlights in the previous
style build. PID 12008 verified the minimap fix after a cold start: 23 sources,
TopRightMain width 790, minimap x=2142 and quest x=1995. A bounded attempt to show
the alternate slot was immediately hidden by its native controller, so it did
not sustain a second visible tile; the restored layout remained at width 790.

`CyberpunkVR_HudPoseDebug` with `CyberpunkVR_HudPoseDebugSeq` publishes display time,
head/panel yaw, step delta, mode, eye positions and cancellation error for read-only
motion checks. Read an unchanged even sequence around the diagnostic block.
