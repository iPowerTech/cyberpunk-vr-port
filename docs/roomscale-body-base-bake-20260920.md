# Bake moved the body with the camera; remaining small movement jumps

## User report and scope

On the `e7931050...` build the user reported smaller but remaining translation
judder, slight displacement on headset turns, and broken Calibration/Bake.
The decisive clarification was: **Bake teleports the body together with the
camera, whereas only the camera should move**. That report was accepted without
asking the user to prove it again by repeated button presses.

Current simulator process was PID2708. The built-in x64dbg MCP resolved fresh
symbols and the changed frame-sample layout. The fast translation comparison
was80cm/0.5s, not the earlier12cm slow probe.

## The previous sample-selection correction helped, but did not finish the fix

`build/roomscale-body-temporal-2708-hmd-fast-after.json` records the e793 run.
The analysis is in the sibling `*-analysis.json`:

- The28 outbound and34 return observations with matching frame publication IDs
  had **zero frame-position-versus-consumed difference** (apart from a negligible
  subnormal residual). CCT really uses the camera latch now.
- Camera-model X range fell to approximately0.945mm outbound and0.134mm back,
  compared with72–87mm per direction in the earlier raw-pose build.
- The hip still showed individual lateral steps of about6.0/6.6mm.
- Native source was used throughout the moving phases; there were no Lua-source
  switches in that interval.

This matches the user's partial improvement. It does not certify the remaining
hip steps as a GPU-frame measurement: the read-only recorder retains separate
seqlock packets and asynchronous observation flags.

## Two incorrect body inputs

### Bake was explicitly added to the body

`AnimPose.cpp` obtained `camModelPos` from a camera component that already
contained VR/bake translation, then added `SharedPose(91..93)` again. Its body
calculation subtracted the full camera delta once, leaving **one copy of bake in
the body target**. This accounts for the user seeing camera and avatar move
together instead of changing their relative alignment.

The extra add now runs only in the legacy unshifted-camera path. The active
component path leaves the camera value as it was actually observed.

### Body compensation mixed camera and delta timestamps

The body previously took a coherent camera/entity pair but removed the current
global `g_headDeltaFP`, which could already belong to a newer camera write. The
pair itself being coherent did not make that third quantity coherent with it.

The camera writer already knows its native base **before** VR/bake/IPD. That
exact base is now stored beside eye/centre in `EyeCentreLedger`, forwarded in
`LocatedCameraFrame`, and published in both native and Lua VRIK snapshots as
`bodyCameraMinusEntity`.

`VRIK_ComputeCamModel` returns both the rendered camera model position and this
paired native body base. Body placement uses the latter; it no longer subtracts
a separately updated global displacement or re-adds camera bake.

The existing native animation/FK/leg solver stays the consumer. No direct entity
teleport, new root hook or body-position smoothing was introduced.

## Calibration and yaw consistency

- The bake candidate accounts for the calibration already in the observed
  camera: `newBake = activeBake + target - observedCamera` in horizontal model
  coordinates. This preserves absolute replacement semantics instead of
  toggling the correction on a repeated Bake.
- The existing horizontal standing-body target and native height policy are
  retained. The correction is now camera-only.
- The physical yaw follower now reads `AcquireFrameHeadSample`, as CCT and the
  cameras already do, rather than the independently filtered XR-thread cache.
  This removes another input-time difference during turns; headset acceptance
  of the residual turn behavior is pending.
- The Apply/AutoCalibration routes were read. The current right reach value
  `.879` was present in the UI, shared calibration and live `g_VRScaleR`; a
  wholesale parameter-reset claim was not made. AutoCalibration ends by calling
  Bake, so it shares the body/view coupling above. Full T-pose anatomical
  accuracy is not certified by this fix.

The agent opened F10 once to identify its controls and inspected a screenshot.
No automated Bake/Apply/AutoCalibration click was executed. The subsequently
prepared unused click capability was removed after the user's clarification.
No calibration values were manually reset to hide the regression.

## Build and deployment

Release build succeeded;16/16 checks passed, including historical camera-base
lookup, Lua/native base agreement, ambiguous record rejection and repeated Bake
replacement. These are code/contract checks, not a replacement for the user's
visual report.

PID2708 was closed through x64dbg. Only the DLL was replaced:

- SHA-256: `1e8591a8d99d97f2d41f303595c011f4eb27754ce46b4beb513dc5224e90295d`;
- bytes: **2,484,736**;
- backup/report: `build/roomscale-plugin-deploy-20260920-150055/deployment.json`;
- previous DLL/log and `vrik_calibration.before.ini` are retained there;
- runtime INI and calibration-file hashes were unchanged by deployment.

**No post-deployment live success yet.** The user launches the game. First
check the reported camera-only Bake behavior and remaining body motion. Fresh
addresses are required. To read the new telemetry tails, set
`body_base_packets=true` in a fresh `body_temporal_probe.py` address config:
native packet64bytes, Lua payload68bytes, located packet52bytes. Their old
prefix fields remain in place.
