# Driving response and seated VRIK — verified 2026-09-24

The installed change preserves neutral after steering beyond full lock, supports
small corrections with either hand or both, and prevents idle hands from
jumping toward the face during a brief camera-publication miss.

Verified baseline plugin SHA256:
`9e2de620a02264f5c643b03f46a6b62bdacec6e2196da30b7bb44956c09da1ca`

Baseline deployment: `build/roomscale-plugin-deploy-20260924-202539/deployment.json`.
Live verification: PID24800, Porsche 911 Turbo, stationary driver seat, game
expanded and foreground. Subsequent prediction testing and the user's acceptance
are recorded in the follow-up note linked below.

The later optional history prediction build is described in
[driving-prediction-20260924.md](driving-prediction-20260924.md). It is disabled by
default; the measurements in this note describe direct steering.
That note also records the subsequent intermittent camera-publication recurrence,
its ledger-rounding correction and grip retention across camera misses. The
latest installed DLL is `61d3684007b2262d923d69f320967e38655df0209277392ac14e38a065b2ee05`,
verified in PID17492; do not treat this baseline note as the latest deployment.

## Steering geometry and scheduling

`WheelSteering.hpp` calibrates the actual grab position and integrates signed
angle changes. One hand uses a virtual hub at the wheel's authored radius from
its grab position; two use their connecting line. Changing from one hand to two,
or releasing only one hand, preserves the current turn. Invalid tracking,
collapsed geometry and recenter are handled explicitly.

The complete physical angle is retained beyond full lock. Only the output
saturates. Discarding excess travel shifted neutral: the previous implementation
stored 90 degrees after a 140-degree movement, then counter-steered when the hand
returned 140 degrees. The regression now checks the complete return after
210-degree excursions, in both directions with all three grips.

Steering uses `TrackedWheelSteering.hpp` and the coherent XR head/controller
publication, before weapon offsets or IK target adjustments. While held, hand
smoothing is bypassed. Controller positions are reconstructed in physical
tracking space so moving/rotating the head does not steer stationary hands.

The XInput poll samples this tracking directly. The animation hook only publishes
grab ownership and nominal wheel radius; it does not compute the steering angle.
A grab epoch prevents stale steering from surviving release/regrab if an input
poll missed the release. A mutex protects the tracking accumulator, and atomic
publications cross the animation/input/script boundaries.

This separation is necessary: PID29752 still drifted -3.51 degrees after the
integrator fix because the *model-space IK target* changed by several centimetres
with the driver's steering animation, despite the same controller command.
Do not reintroduce IK targets, a changing body plane, or the game's camera frame
as steering measurements. Do not clamp the stored physical angle.

## Game input range and response rates

Live user settings were inner/innerFix deadzone 0.35, outer endpoint 0.90 and
steering sensitivity 100. The old 0.18 output floor did not cross the actual game
deadzone. The new response is linear after the configurable wheel deadzone.
XInput encodes the desired axis through the inverse of the game's inner/outer
range. Neutral emits exactly zero. Game settings themselves are not changed.

`CyberpunkVRPort_Driving` reads the gamepad range at most twice a second while the
wheel feature is available in the driver seat. During a fresh grip it temporarily
raises four slew limits in the selected drive-model record:

| Field | Porsche original | While grabbed |
| --- | ---: | ---: |
| turnUpdateInputSlowChangeSpeed | 0.1 | 100 |
| turnUpdateInputFastChangeSpeed | 1 | 100 |
| wheelTurnMaxAddPerSecond | 95 | 10000 |
| wheelTurnMaxSubPerSecond | 128 | 10000 |

The record was `Vehicle.VehicleDriveModelData_911Turbo`, with
`useAlternativeTurnUpdate=true`. Steering-angle limits, speed assists, friction
and other handling fields stay native. Drive-model records are shared, so these
four temporary values apply to that record while held. Originals are restored
on release, driver/model change, pause/overlay, shutdown or bridge failure.
External edits made during the grab are preserved.

An inspection during a held grip in PID29752 confirmed the active bridge and all
four elevated values. The same bridge runs in the final build. PID24800's
post-test readback confirmed the inactive bridge and all four original values.

Bridge SHA256:
`ada3d43f73549266fc5989b14ce8f7c3b03bcaa5dbbc00b577775ad11359afa1`.
The distribution builder includes the new CET folder; the iterative deployment
script refreshes it when installed.

## Idle seated hands

The original 20-second read-only capture in PID18712 reproduced two events:
controller poses stayed constant, the model camera became zero for about 68 ms,
and right/left IK targets jumped approximately 11/15 cm.

The seated path previously fell through to a legacy shoulder-relative solve when
a coherent camera/entity pair was unavailable. Seats now use the bounded missing
camera fallback too. Local arm capture/replay is shared production code: only
arms owned by VRIK are cached, preserving native torso/legs and wheel-owned arms.
A transient miss can retain the last complete solve for up to 250 ms; replay does
not extend that deadline. Explicit invalidation releases ownership. A missing
mounted full-quaternion pair no longer selects an on-foot yaw-only pair or
unpaired scalar coordinates.

## Validation

The Release build passed. All 12 steering CTests passed, covering either single
hand and both hands, asymmetric initial grab positions, translation rejection,
handoff, overtravel and full return, invalid geometry/tracking, inverse gamepad
range, 36 rate/speed/grip trajectories, 1,500 jitter samples, 3,000 stationary-hand
samples under moving heads, tracking expiry, duplicate-pose handoff and recenter.

All 49 VRIK tests passed after the seated fallback change, including mounted pair
misses, selective local arm replay, native bone preservation and bounded expiry.
VRIK production code did not change in the later tracking revision.

A lexical mock test ran the production Driving bridge without live writes and
passed idle/no-write behavior, one application per grab/model, exact restoration,
model switching, partial-failure cleanup and external-edit preservation.
The private simulator transport also passed its eight tests.

Eight final live trials in PID24800 passed:

- Full overtravel and return, right hand, left hand and both hands, both directions.
  Maximum residual input angle: **0.000026 degrees**; all neutral outputs zero.
- +/-4 and +/-8 degree corrections and zero returns with each grip.
  Detectable animated wheel response followed input by approximately **21-73 ms**.
- One/two-hand transitions retained the current turn.
- The same 0.6-second right-hand ramp used in the earlier PID24068 capture:
  first nonzero input to >0.5 degrees of animated rim response changed from
  approximately **199 ms to 44 ms**.

These are sampled simulator timings, not complete headset-to-photon latency.
Animated rim angles come from the native hand positions, not physical road-wheel
angles. The vehicle remained stationary; road-speed feel is for user evaluation.

A final 20-second idle capture had no jumps, with maximum adjacent hand-target
change about 0.062 mm. This is bounded observational evidence, not a guarantee
that every future frame has been sampled.

Final readback confirmed original HMD/controller poses, both grips zero,
steering zero, both grab blends zero, inactive rate override, unchanged runtime
INI, and the restored original OpenXR runtime manifest.

## Evidence and repeatable probes

All evidence is under `build/driving-response-20260924/`:

- `idle-before-18712.json`: reproduced idle camera/hand jump.
- `neutral-right-29752.json`: residual model-space feedback failure.
- `neutral-right-24800.json`, `neutral-mask2-24800.json`,
  `neutral-mask3-24800.json`: complete final overtravel roundtrips.
- `micro-mask1-24800.json`, `micro-mask2-24800.json`,
  `micro-mask3-24800.json`: final micro-correction response.
- `handoff-tracking-24800.json`, `right-tracking-24800.json`,
  `right-focused-24068.json`: handoff and comparable response ramp.
- `idle-tracking-24800.json`, `final-state-24800.json`: final stability/restoration.
- `tracking-tests.xml`, `vrik-tests.xml`, `build-tracking.log`: test/build results.

The simulator was built privately without editing the user's simulator checkout.
The original active runtime manifest was restored as soon as each test process
loaded the private DLL. Pose batches/grips were disabled and original controller
poses restored after each trial. Future launches use the original runtime.

The user explicitly requested being asked before a new steering-test session so
they can expand the game. Wait for readiness. The live probe additionally checks
foreground/minimized state throughout and aborts/restores if focus is lost.
Do not count an input-only background run as native vehicle verification.

Reusable sources under `tools/driving_tests/`: `CMakeLists.txt`, `main.cpp`,
`observe.py`, `prepare_runtime.py`, `live_steering.py`, `bridge_test.lua`.
New tools are ignored by default; force-stage these exact source files when
committing, not generated runtime/build files or Python caches.
