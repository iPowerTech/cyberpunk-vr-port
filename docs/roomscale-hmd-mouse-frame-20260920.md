# HMD turns put the camera behind the body: matched mouse/HMD comparison

## Why the IPD work did not settle the complaint

The user explicitly reported that mouse rotation works and HMD rotation does
not, most visibly at180degrees. The preceding `7c4f953c...` DLL still exhibited
this. The earlier eye/centre and source-selection changes were not sufficient
and must not be described as having fixed this complaint.

PID **13216**, EXE base **0x7FF744420000**, plugin base **0x7FFA3EC60000**.
All addresses were resolved anew through the built-in x64dbg MCP. Breakpoints
were not needed. Diagnostic/CCT reads and the shared offset mapping were
read-only. Body snapshots used `Game.VRBodyBonePos` / `Game.VRCamModelPos`.

The simulator head remained at `(0,1.7,0)` throughout the comparison. Its
scripted yaw command used no window activation or keys. Actual mouse input
used two bounded400-count relative pulses into the identified game HWND,
with original focus restored; head yaw remained0. The game accepted each pulse
as approximately20degrees of body rotation. Returning the input restored the
original heading.

## Measured difference

| Quantity | Mouse −20° | HMD −20° | HMD180° |
|---|---:|---:|---:|
| CCT position change | 0 | 0 | 0 |
| Entity yaw change | −20.00003° | −20.00007° | 179.99995° |
| Physical realign change | 0 | −20.00000° | −179.99999° |
| Camera/head offset change, in body coordinates | **0.109mm** | **40.885mm** | **235.506mm** |

In the180degree HMD sample the camera's forward offset from the head changed
from **+0.09771m to −0.12948m**: in front became behind. That is the reported
third-person-like placement. The CCT and native camera-base position were
stationary; the erroneous quantity is the rendered anatomical camera offset.

## First wrong transform

The existing body solver publishes `[116..118]` as:

```text
eyeBakeModel = headModel + (-.02, +.10, +.15) - bodyCameraModel
```

These are **model-space metres**, confirmed by `VRIK_PlaceBodyUnderHMD` and
the captured shared-memory values (approximately `(-.021,.116,.2655)`).
`LocateCamera`, `PatchCamera` and the coherent hand-anchor builder combined
this with the physical HMD position and rotated everything by:

```text
trackingYaw = entityYaw - physicalRealign
oldDelta = R(trackingYaw) * (headResidual + trackingOffsets + modelBake)
```

With mouse turning, entityYaw and trackingYaw advance together. With HMD
turning, realign cancels the entity turn from trackingYaw. Consequently the
anatomical camera offset stays in the room while the body turns underneath it.
At180degrees it points behind the avatar.

The formula reproduces each captured endpoint's actual camera-centre-minus-
native-base delta within **0.015mm**, using the independently read shared bake
and body/tracking yaws. This is not an IPD-sized guess or a proposed hip move.
The unmodified half-IPD eye separation does not enter this comparison.

## Correction

`ComposeAnchorTranslation` keeps the terms in their source coordinate systems:

```text
newDelta = R(trackingYaw) * (headResidual + trackingOffsets)
         + R(entityYaw)   * modelBake
```

The camera-bake and eye-bake come from the rig/model; physical residual and
existing user tracking/vehicle offsets retain the tracking frame. World-scale
is still applied only to the physical residual as before. MAIN/VRCAM and the
coherent hand anchor call the same production helper.

The old loose recipe globals were replaced by a single mutex-protected
`AnchorRecipe` publication containing both frames, offsets and scale. Camera
writes refresh the two yaw bases at the write instant; the hand publisher uses
the coherent recipe snapshot. The body/animation pose solver, turn accumulator,
CCT implementation and simulator-reset path are not changed by this patch.

**Important acceptance metric:** after a turn, the calibrated camera/head
relationship must remain in front of the avatar. Requiring a constant world
camera centre while a nonzero model-space eye calibration turns would test the
old incorrect frame again. Physical movement still uses trackingYaw rather than
being rotated with the body.

## Verification so far

- A new `roomscale_anchor_frames` regression was first run against the old
  combined transform and **failed at the180degree case**.
- The production helper was then corrected and wired to all three consumers.
- **14/14 tests passed**: matched mouse/HMD transforms,180degrees, yaw wrap,
  preserving physical translation, height and zero-model-offset vehicle path,
  plus all prior movement/ABI checks.
- Offline application of the new frame to the captured data reduces the
  predicted20degree discrepancy to **0.067mm** and180degree discrepancy to
  **0.060mm**. These are predictions, not post-fix live results.

Raw artifacts in `build/`:

- `roomscale-live-13216-mouse-comparison-before.json` (+`.input.jsonl`);
- `roomscale-live-13216-hmd-matched-before.json`;
- `roomscale-live-13216-hmd-180-before.json`;
- `roomscale-live-13216-turn-frame-analysis.json`;
- `roomscale-live-13216-turn-180-analysis.json`.

`tools/roomscale_tests/analyze_turn_comparison.py` verifies the formulas and
hashes each raw capture. `simulator_camera_probe.py` now records the shared
offsets and accepts a real mouse comparison. After deployment, repeat20° and
180° and request the user's visual check. No post-fix live success yet.

## Deployment

Release build succeeded and `git diff --check` passed. PID13216 was closed
through the built-in x64dbg MCP. Only `CyberpunkVR_Stereo.dll` was replaced;
runtime settings were unchanged and the game was not restarted.

- Installed SHA-256: `3bcdd9ee38376d40c3d0dc9abeec3a042fabdac10b2cd0383e11b7e3c5db6200`.
- Size: **2,482,176 bytes**.
- Previous DLL, run log and deployment verification:
  `build/roomscale-plugin-deploy-20260920-120036/deployment.json`.
- Final native input cleanup: release-all140/140, focus restored, log entry
  `aa7833f72d5149c0912be007c2ed9b37` in `build/roomscale-native-input-13216.jsonl`.
- Hash-indexed comparison: [JSON](roomscale-hmd-mouse-frame-20260920.json).

Waiting for the user's next launch; post-fix20°/180° and visual acceptance pending.

## Live acceptance — PID19332

The user launched the deployed `3bcdd9ee...` DLL. Its installed SHA-256 was
rechecked; hooks ready, fresh camera/diagnostic addresses and an empty breakpoint
list were confirmed through the built-in x64dbg MCP.

The same background simulator probe was repeated with physical head position
fixed. Results, in **body-local camera-minus-head coordinates**:

| Head command | Offset before | Offset after | Change |
|---|---|---|---:|
| −20° | `(-.020007,.100010,.150001)` | `(-.020085,.099975,.150004)` | **0.085mm** |
| +180° | `(-.020021,.100042,.150041)` | `(-.019982,.099981,.149968)` | **0.103mm** |

The camera stays about **10cm forward of the head** rather than changing to
12.9cm behind it. At180degrees the old body-relative error was235.506mm.
CCT position and injected-movement count did not change during either turn.
The first20degree command rotated the body15degrees because it started inside
the5degree free-look region; the180degree command produced180degrees of body
rotation. These are recorded, not assumed equal.

`verify_turn_fix.py` rechecked694 coherent diagnostic snapshots and6 settled
endpoints, the new two-frame formula and the calibrated `(-.02,.10,.15)` offset.
The returned head pose can leave the body at the opposite edge of its5degree
deadzone; a nonzero world-camera return displacement is not labelled an error
when the **body-local head/view relationship is preserved**.

The user then manually checked the turn and explicitly answered **«Теперь
нормально»**. This accepts the reported HMD-only behind-the-back placement issue
on this build. It does not expand the prior terrain/vehicle coverage.

Artifacts:

- `build/roomscale-live-19332-hmd-20-after.json`;
- `build/roomscale-live-19332-hmd-180-after.json`;
- [verified before/after index](roomscale-hmd-frame-live-19332-20260920.json).

No C++ changes or redeploy were needed in this validation session. The automated
probes restored their initial simulator pose; the user subsequently continued
manual movement/turning. Game left running, no test breakpoints.
