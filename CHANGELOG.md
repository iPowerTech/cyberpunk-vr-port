# Changelog

## 0.0.6 — Motion-controlled VR hands (full-arm IK) + in-headset overlay

This release adds proper 6-DoF VR hands driven by the motion controllers, reworks the in-headset
settings into tabbed sections, and fixes a number of input/locomotion/logging issues.

### VR Hands (new)
- **Floating-hands VR with full shoulder → elbow → hand IK.** Hands follow the controllers in 6-DoF;
  the forearm no longer stretches and the wrist orientation is preserved.
- **Correct bone math.** Fixed the `QsTransform` slot layout the hand code used (translation `@+0`,
  rotation `@+16`, scale `@+32`) — previously position and rotation were written to swapped slots,
  which caused the "palm-up / flat stretched forearm" artifacts.
- **Bone chain auto-resolved by name** (Head / Shoulder / Arm / ForeArm / Hand, per side) from the
  rig's bone-name table instead of hard-coded indices, with a parent-index table copied for FK.
- **Head-independent tracking.** Controller poses are delivered HMD-local; the proxy now also exports
  the HMD-relative orientation through shared memory so the plugin can cancel head motion. Turning or
  tilting the head no longer drags the hands.
- **Anatomical elbow.** Ported the VRArmIK elbow-swivel heuristic (used by FRIK / DragonIK /
  ue5-vr-fullbody): the elbow swivel is driven by the **hand position** in body space, not by wrist
  orientation. Added a degeneracy-free bend reference (down + back blend) so the forearm stands
  vertical in the bicep-curl pose, plus a bend gate so only genuinely flexed-across-the-chest poses
  swing the elbow outward.
- **Per-hand calibration** for reach scale, height, elbow swing, elbow pole and wrist-rotation offset.
- **Weapons are tracked** correctly with the new hand path.

### Performance
- **Removed the dead hot-path hook** on `IComponent::Update` that ran a `VirtualQuery` per skeleton
  per frame and collapsed FPS to 10–15 with tracking enabled. Only the working `AnimPoseApply` hook
  remains; tracking now runs at ~70 FPS.

### In-headset F10 overlay
- Reorganised into tabs: **VRIK**, **HUD**, **Debug Gizmos**, and **Tracking / Camera**.
- **VRIK tab** — start/stop hand tracking, live IK calibration sliders, Log VR Diag.
- **HUD tab** — compact one-row-per-element layout (X / Y / Size together) instead of three rows each.
- Removed the standalone *VR Hands Control* CET window and the weapon-offset controls from the old
  Hand Proxies section (debug gizmos kept).
- The F10 "Start hand tracking" toggle now requests the full-IK mode (it previously requested the old
  direct-write fallback, which looked broken).

### Locomotion / input
- **Head-oriented locomotion (Movement Control: HMD).** On-foot movement can follow the head yaw;
  implemented an ABI-safe `OnFootMoveXY` hook that rotates the input vector by the HMD yaw, gated to
  on-foot and non-menu state (driving untouched). The mode is now actually plumbed through
  `vrport.ini` (`xr_movement_control`) so the overlay selection persists.
- **Disable Mouse-Y (pitch)** toggle exposed in the overlay, **enabled by default**, bridged to the
  CET mod so the mouse stops fighting head pitch.
- **Auto-recenter on save load** — the CET mod signals `OnGameAttached`; the proxy recenters ~1.5 s
  later (after the load fade) so the recenter captures a settled pose.

### VR HUD
- Fixed the broken HUD settings: the proxy now writes `hud_layout.ini` into the CET mod folder (where
  CET resolves relative paths), so the HUD tab values actually apply and persist.
- Fixed the slider bug where dragging one element re-applied the **entire** HUD layout — per-widget
  value caching now moves only the active element.

### Logging / housekeeping
- Renamed the proxy log `CyberpunkVRProxy.log` → **`cyberpunkvrport.log`**.
- All runtime `.txt` diagnostic dumps now write next to the game exe instead of the desktop.
- **Verbose-log toggle** (F10 → Debug): per-frame spam (cursor/window hooks, telemetry dumps,
  hand/XR per-frame, depth-diag) is off by default for clean tester logs; the file is truncated each
  session.
- Renamed the CET mods: `CyberpunkVRPort_Hands_Test` → **`CyberpunkVRPort_VRIK`**,
  `CyberpunkVRPort_HUD_Live` → **`CyberpunkVRPort_HUD`**.

### Repository
- Moved runtime `.txt` dumps into `dumps/` and ignored them.
- Removed the `build-vs-aerv2/` experimental build tree.
- Restored the root `CMakeLists.txt`; added the RED4ext plugin sources (`CMakeLists.txt`,
  `vrik_solver.h`, `vtable_dumper.h`) and the redscript HUD sources to version control.
- Ignored vendored SDKs under `third_party/` (fetch separately — see README "Building").

---

## 0.0.5 — earlier baseline
- SteamVR pose-lock auto-detect, canted-display FOV, AER stereo-depth fixes, Mono render-pose
  jitter fix. See git history for details.
