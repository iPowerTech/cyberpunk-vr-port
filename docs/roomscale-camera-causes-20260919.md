# HMD turn / simulator reset: why CCT-only acceptance was insufficient

User observed body/camera displacement during HMD rotation and a different
result from Simulator Reset View versus the mod's F7 recenter. The native RE
reports were re-read, including locomotion, main graph, mapping/FK, Hips/Spine
constraints, camera binding, job order and render-camera construction.

Backup audit: **52 cp2077-native files**, none missing;51 identical to
`backup/native-roomscale-re-20260919` (`8ca2fb40`), only the current chain index
differs. The native evidence verifier passed. Prior notes are also preserved
in that backup, including `notes/vrik_roomscale.md` and `notes/vrik_gamegait.md`.

## 1. Body reference contained the stereo eye displacement

Native RE establishes that animation model/FK, camera-component transforms and
render eye positions are separate stages. CCT position alone cannot prove that
VRIK is under the rendered head centre.

Live PID9388, old DLL `ce9e3baf...`:

- Pure HMD turn: CCT did not move; camera-pair centre changed **0.089mm**.
- Published head bone world position changed **43.79mm**.
- Converting the published hips from model to world gives exactly the offset
  from the cyclopean centre to **MAIN's eye**, before and after the turn.
  `verify_camera_diagnosis.py` checks this using the captured quaternions and
  fixed-point camera positions, not an inferred camera radius.

Cause: PatchCamera writes IPD into the camera component. SerializeSetup copies
that position into LocateCamera's buffer. The buffer was nevertheless labelled
"head centre" when published to the native VRIK pair. Removing the HMD delta
from that pair left **half an IPD in the body anchor**, which rotated on head yaw.

Change: the component writer records exact eye/centre fixed-point pairs in a
bounded `EyeCentreLedger`. LocateCamera uses the matched centre for the VRIK
publication; the rendered eye buffer is not moved. Match/miss counts are exported
as `CyberpunkVR_BodyCentreMatch` / `CyberpunkVR_BodyCentreMiss` for live validation.

## 2. Simulator Reset View is a raw pose reset, not F7

The existing simulator menu was inspected without activation. Tools → Reset
View is command1402 (Home). Invoking that exact command changed the raw head
pose to `(0,1.7,0)`, identity orientation, but the plugin's tracking generation
did not change. The old implementation therefore interpreted the reset as
motion: **CCT shifted130.7mm and the camera centre283.5mm**.

Change: a narrowly qualified simulator-window bridge observes its existing
Reset View/Home command. It checks process, runtime-module window procedure and
the actual menu label/ID. Before forwarding the command it gates pose
publication; after the original runtime handler returns, the XR loop captures
a new yaw-only base before publishing again. The game keeps its current CCT
position. The bridge does not press keys, focus the window, reset the game, or
modify the simulator DLL. It is removed on OpenXR shutdown.

`TrackingResetGate` rejects poses located across the reset and prevents an old
completion from clearing a newer reset. The camera uses its coherent cached
pose while the reset is pending. F7 keeps its existing path.

This adapter addresses the **reported simulator operation**. Real-runtime
reference-space change timing/pose conversion is not certified by this test.

## Verification and deployment

- **12/12** model/ABI tests passed, including stereo-centre history and reset
  interleaving. Release DLL built successfully.
- The first VS build after source-glob regeneration missed the new translation
  unit at link time; the next build compiled it and linked successfully.
- Game PID9388 closed; **only the plugin DLL** redeployed.
- SHA-256: `57a66866995cd276d8bea560177754c19f419f242c913a3a8deb4fc88b852a93`.
- Bytes: **2,479,616**.
- Backup/report: `build/roomscale-plugin-deploy-20260919-152745`.
- Runtime INI unchanged. No source commit or push made.

**Pending:** user launches the game, confirm the reset-bridge install log and
centre match counters, then repeat the same head-versus-camera and actual Reset
View probes. No post-fix visual/live success is claimed yet.
