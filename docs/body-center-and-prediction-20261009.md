# Body centre and tracked-turn prediction (2026-10-09)

Installed and checked with a targeted live pass. This continues the native body motion source change; the old
camera-input injection is not restored. Authoritative live output is under
`build/body-center-prediction-20261009/`.

## Measured centre displacement

`centre-before.jsonl` decomposes native camera position, entity position,
`AnchorRecipe`, rig neck mount, headset position and solved body joints.
The native camera mount was `(0, 0, 1.59999847)` in entity space. The extra
horizontal radius was the immutable neck calibration, `0.129890963 m` forward.
Headset position and native entity position did not change in this test.
The calibration was rotated by the physical body yaw, moving the optical centre
along a circle. Its predicted 90-degree chord is 183.694 mm and its full diameter
is 259.782 mm. Measured endpoints include the Hybrid cone residual and world
float quantization; the 180-degree measurement was 259.763 mm.

The offset itself is intentional: male neck +15 cm forward, eyes +10 cm high
(female forward offset is 10 cm). Removing it would break the calibrated avatar
placement. Its rotation must be assigned to the avatar orbit, not to the camera.

`AnchorRecipe` now carries a fixed centre-reference yaw for the current player.
HMD translation still uses the current tracking yaw, while the calibrated optical
centre stays fixed under physical turns, stick turns and tracking recenter.
`BodyOrbitModel` expresses the corresponding horizontal avatar placement in
current entity coordinates. The existing VRIK pelvis solve consumes it; rotations
and native leg-local animation are preserved. Hand targets and the bend target
use the same recipe. Native movement still owns the entity quaternion and capsule.
Vehicle and other non-owned camera scopes use their existing recipe.

The live1 centre run (DLL `816d453d72eb4c4ae1730e96cf0e2518fa773d2187505461270d665ee1dc2a4f`)
recorded 805 turn/hold observations with zero optical-centre position change at
the recorded world-float precision. At 180 degrees the measured pelvis model
shift matched the expected approximately 26 cm orbit; height stayed fixed.
This is a centre/pose measurement, not a claim that the capsule translates.

## Turn dynamics

The original estimator reset its 45 ms coherence evidence on every accepted
reference refresh (>1.05 rad). The new reference used raw rotations/positions,
but retained a positional filter from before the rebase. Together these caused
periodic pauses and catch-up even for a perfectly uniform input trajectory.
At 90 Hz, 90 degrees/s, the original output ranged from 13.397 to 181.115 deg/s,
with 3.010 degrees mean and 6.398 degrees peak lag after startup.

Coherent reference refresh now keeps confirmed evidence and rebases all
observation fields to the same instant. Short raw witnesses bridge the positional
filter warm-up. Fresh hand travel must support a new wrist turn; old displacement
relative to an earlier reference cannot authorize a gesture after stopping.
Independently moving arms remain supported by the existing multi-device checks.

`BodyYawPrediction` consumes only accepted yaw. Stable angular motion permits
bounded compensation of the known discrete low-pass lag. Between observations,
extrapolation covers at most one measured interval, 15 ms and 3 degrees. It is
not integrated indefinitely and does not add an assumed display prediction time
on top of OpenXR. Stops, reversals, invalid tracking, origin changes and confidence
loss disable or limit extrapolation. Body motion consumes the result; camera
orientation and controller poses do not.

## Offline evidence so far

- 32 body-rotation groups pass, including the existing noisy idle/gesture/turn,
  controller loss, discontinuity and Hybrid cases.
- 360 steady multi-revolution trajectories preserve constant angular speed across
  reference refreshes at 30–144 Hz.
- 400 asynchronous source/native cadence trajectories: peak steady error 0.5294°.
- 160 repeated reversal/stop trajectories: peak measured overshoot 0.0000274°.
- 320 accelerating, sinusoidal and rapid-reversal trajectories: moving peak 2.918°.
- 60 noisy wrist/head gesture sequences after real turns: peak false yaw 2.829°.
- 50 full-rig groups pass, including a new optical-centre/orbit fixture checking
  rigid placement, unchanged native leg locals and repeated-solve idempotence.
- 45 roomscale groups and native hook signature checks pass.

These bounds describe the tested trajectories, not a waist tracker or a claim of
perfect anatomical inference. Input noise can still limit certainty about torso
orientation. See the targeted live evidence below for the installed version.

## Live workflow

The game launches only through `tools/quickboot_tests/launch_to_save.py`.
The grouped runner `build/body-center-prediction-20261009/run-live-suite.ps1`
installs the test DLL, selects a private coherent simulator runtime for the one
process, restores the runtime manifest after launch, then runs centre, continuous
turn, matrix, negative, mixed and Hybrid scenarios. Its `finally` terminates its
exact game process and restores runtime/INI even after a failing test. The user's
simulator checkout and installed runtime DLL are unchanged.

## Installed targeted pass after reboot

DLL SHA-256: `64e64e02420534702195f163bf86288362ae93667250c8df47dd77adc1bd7702`.
The built and installed DLL hashes match; the plugin directory contains one DLL.
The final changes use a local six-observation angular-rate fit, preserve held
input publications without integrating stale motion, and classify noisy physical
bends using accepted height segments. Animation's `BodyYawFollowActive` query is
now read-only: the native movement step alone seeds mode transitions from the
committed body angle.

The user requested a short final verification rather than another broad matrix.
Sixteen relevant local regressions passed (8 prediction/lifecycle, 4 bend/native
ABI, 4 rig/bend/orbit). One game launch through QuickBoot ran four prediction
cases, five Hybrid cases and the centre lifecycle probe. The process (PID 9408)
was terminated immediately afterward; INI and runtime manifest were restored.
Evidence: `live3-targeted/` and `run-live-targeted.ps1` in this work's build folder.

At 180 degrees/s in the same low-ready simulator scenario, measured native body
yaw versus the published HMD yaw had the following mean absolute errors:

| Direction | Previous live2 | Current live3 |
| --- | ---: | ---: |
| Negative | 3.180 degrees | 0.109 degrees |
| Positive | 4.034 degrees | 0.215 degrees |

These are independent live runs with unique tracking frames and the first 300 ms
excluded, not a deterministic display-latency benchmark. Peak current errors were
2.983 and 5.093 degrees. Stop overshoot was at most 0.0118 degrees; final errors
were below 0.00006 degrees. Exact rows and the comparison are retained.

Physical following added less than 0.00002 degrees to the paired camera base yaw.
Optical-centre XY displacement was zero at recorded world-float precision over
415 lifecycle observations, including physical following, recenter and mode off/on.
The intended native snap injection was not consumed (observed body-yaw delta zero),
so the checkpoint named `native_snap` does NOT establish native stick/snap coverage.
No extra game launch was made for that unexercised case. Real headset comfort,
collision/climbing and long-session behaviour were not newly revalidated here.

Both level-head bend directions were recognized after the turn sequence: roughly
37.86 degrees forward and 36.05 degrees sideways for a commanded 40-degree hinge.
Both Hybrid routing cycles and pitch hysteresis passed.
