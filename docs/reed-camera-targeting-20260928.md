# Reed camera detection in q303

Verified live in Cyberpunk 2077 2.31, PID 26464, on 28 September 2026.
The stuck objective was `q303_songbird/06b_paradise_technical/06b_switch_cameras`.
The controlled SurveillanceCamera was EntityID 8110854185791712344; Reed was
EntityID 9013817. No quest facts or journal states were written.

## Cause

The scene `q303_06b_paradise_technical.scene`, node 1397, waits on
`questCameraFocus_ConditionType` for `#reed`, with angleTolerance 15 and
timeInterval 0.1. onScreenTest, useFrustrumCheck and zoomed are false.

Native TargetingSystemUser update at EXE RVA 0x4B9D84 already reads the active
camera transform into user+0x31100. It then copies the default position and
direction from the player's FPP cache into user+0x311F0/+0x31200. During the
takeover these two positions were about 85 metres apart:

- Active camera: (-1838.8783, -2258.7278, 46.7982).
- Default FPP origin: (-1863.1859, -2340.3289, 46.4356).
- Reed's chest: (-1835.4882, -2273.1521, 40.4745).

RVA 0x23D544 uses the default origin/direction for its target rejection. Reed
was behind that ray and farther than its 15-metre near-target exception, so
he was discarded before the quest callback at 0xC03A60 could test the angle.
GetTargetingSet returned Complete and IsVisibleTarget returned false, despite
Reed being about three degrees from the rendered view direction.

There was also a physical camera motor/view offset. A one-shot correction
aligned the motor but did not advance the quest. That correction is not the
persistent fix. TimeSystem.IsPausedState was true while the actual game pause
API was false and simulation advanced; it was not evidence of a paused game.

## Fix

After the original updater returns, the existing WeaponAim hook refreshes only
the local player's targeting position, direction and derived Euler angles from
that same update's active camera transform. The shared FPP component and its
cache are not modified. The native target filtering, quest angle, timer and
progression remain in charge.

Guards require a fresh device-camera lease, an actual published takeover ID,
remote-camera state, active XR session, no menu/overlay input capture, the exact
current player pointer and EntityID, an active targeting user, and a finite
camera transform with a valid quaternion. Each update reads the current camera;
there is no cached remote entity or pose across camera switches.

## Verification And Installation

- All four `tools/story_attention_tests` suites passed, including a regression
  using the captured coordinates, looking away, camera switches, inactive users,
  wrong/replaced owners, invalid transforms, stable repeats and untouched bytes.
- Release DLL compiled successfully.
- A session-only helper chained the already installed updater hook using MinHook
  and the same `TakeoverTargeting.hpp` implementation. Its target signature, PID,
  module range and installed DLL SHA-256 were checked before installation.
- Live IsVisibleTarget changed to true; GetTargetingSet changed to ClearlyVisible.
  The quest then advanced to `06d_scan_open_gate`, followed by `07_enter_lift`.
  The helper reported zero errors. Diagnostic CET references were released.
- The new DLL and matching PDB were installed for subsequent launches. Windows
  allowed moving the mapped old DLL to the backup directory, so the running game
  retained its loaded image and session correction without restarting.
- UserSettings.json and vrik_calibration.ini hashes remained unchanged.

Installed DLL SHA-256:
`13B3CB328FF9E60D456A1D538DA1DB2243D638F9AC88D525563CB8BE20EC9E67`.
Backup, hashes and mapped old image:
`build/reed-camera-20260928/deploy-182657/manifest.json`.
Native investigation, temporary session helper and installation receipts are in
`build/reed-camera-20260928/`. They are diagnostic artifacts, not extra shipping
plugins. Subsequent launches use the fix built into CyberpunkVR_Stereo.dll.
