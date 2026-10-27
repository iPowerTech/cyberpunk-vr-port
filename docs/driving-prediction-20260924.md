# Conservative optional steering prediction

The user asked whether previous frames/history could further reduce the perceived
lag, then prioritized avoiding control problems even if the improvement is small.
The chosen mode is optional and **off by default**. It adds only a small output
lead; the measured physical angle, virtual pivot and neutral are never modified.

## Behavior

- Four distinct observations must show a continuing turn in one direction.
- The slowest of the three interval velocities limits the prediction.
- Hard limits are **8 ms** and **0.5 degrees**. The term fades over two degrees
  near the configured center deadzone and full-lock boundary.
- Stops, reversals, grip changes, recenter, invalid tracking and timestamp gaps
  remove the prediction. Duplicate input polls cannot advance the same pose
  farther; the term expires if fresh data stops arriving.
- Current direct input is always available immediately while history warms up.
  No position/angle moving average is added, and a step from rest is not guessed.
- Prediction never accumulates into the physical steering angle. Overtravel and
  return retain the same neutral; disabling the feature returns direct output.

The implementation is a fixed four-element array in `WheelPrediction.hpp`, used
by `TrackedWheelSteering.hpp`. It adds no thread, timer, GPU work or allocation.

CONTROLS -> DRIVING exposes **Steering prediction** and **Prediction horizon (ms)**.
INI keys are `xr_wheel_prediction` (default 0) and `xr_wheel_prediction_ms`
(default 8, range 0-8). Both UI and file application validate the horizon. The
existing runtime INI did not contain these keys and was preserved on deployment.

OpenXR can already provide a pose predicted for the requested time; this small
additional term is for downstream game response, not a replacement for runtime
tracking prediction. See the primary
[OpenXR locating-spaces specification](https://registry.khronos.org/OpenXR/specs/1.1-khr/html/xrspec.html#spaces-locating).
More history does not remove hardware or rendering latency, and future stopping
or reversal cannot be known before its first observation.

## Checks and limits of the result

All **19 steering CTests passed**. New cases cover consistent motion across
30/45/90/120 Hz, half-degree bounds, neutral/full lock, immediate reset on a
fresh stop/reversal, no prediction for a step from rest, duplicate polling,
stale history, config/grip toggles, recenter, physical overtravel return with
prediction continuously enabled, disabled-path bit equality and jitter.

A numerical continuing-motion model used a fixed 44 ms downstream delay. Its RMS
angular lag error changed as follows:

| Pose rate | Direct RMS degrees | Predicted RMS degrees |
| --- | ---: | ---: |
| 30 Hz | 1.19620 | 0.993280 |
| 45 Hz | 1.19598 | 0.987684 |
| 90 Hz | 1.19576 | 0.982593 |
| 120 Hz | 1.19570 | 0.981405 |

This is approximately 17-18% less error in that **synthetic motion model**, not a
measured improvement in headset/game latency. The previously measured 44 ms
response belongs to the direct-steering build. The later live comparison is
recorded below; physical-headset prediction testing is still separate. The bounded term can still be a
small prediction error during an unobserved stop; it is not a no-bugs guarantee.

The Release build passed. Test/build evidence is in
`build/driving-response-20260924/prediction-tests.xml`,
`prediction-tests-build.log` and `build-prediction.log`.

Initial prediction DLL SHA256:
`b92084e3c1b5564dd7ccea96f210420f08223423bffab1512e6fe141443285db`.
Deployment report:
`build/roomscale-plugin-deploy-20260924-210559/deployment.json`.
The game was already closed; it was not launched or killed for this deployment.
Runtime settings and calibration hashes were preserved. The Driving CET bridge
and the restored original OpenXR runtime selection were not changed.

For a future live comparison, ask the user to expand/focus the driver view before
sending steering input, compare enabled/disabled on the same trajectories, and
restore poses, grip inputs and the user's chosen setting afterward.

## Live A/B comparison and the reported VRIK recurrence

The user requested live tests, then reported idle VRIK flashes at approximately
21:14, 21:14:40 and 21:15:20. PID14588 used the initial prediction build with
prediction disabled. Read-only diagnostics established camera publication ages
of 610-750 ms. One 93 ms interval exhausted the existing bounded pose cache.
Stable published IK targets alone did not detect this, because those values
retain their previous contents while the pose is released.

The native eye/center/body ledger contained float-aliasing records: equal Lua
eye coordinates mapped to slightly different independently rounded body bases.
Its strict body-base equality rejected all such candidates. The correction keeps
the existing center tolerance and accepts body bases only when head-minus-body
agrees within one fixed-point unit, 1/131072 m (7.6 micrometres). It does not
increase the camera expiry interval or accept genuinely different body frames.
Captured records are included in the existing `roomscale_eye_float_alias` test;
the rejected different-frame cases still pass. All 42 roomscale tests passed.

An initial exact-offset-only correction was installed as
`fb00b66913c024a18c2570d9279484c2bcc3dbbf657d041b0201d76ed9d864e3`
and tested in PID29212. Two 30-second idle captures had maximum camera age 47 ms
and no exhausted cache. The steering A/B harness generated continuous profiles
at the runtime's requested XrTime, independently of file-IPC cadence. Both
conditions used the same production DLL, same poses and settings except the
prediction switch. Six paired periodic trials alternated order:

| Profile | Off: native phase lag | On: native phase lag | Mean reduction |
| --- | ---: | ---: | ---: |
| 0.5 Hz, three pairs, both hands | 125.69 ms | 120.51 ms | 5.18 ms |
| 1.0 Hz, three pairs across right/left/both | 122.28 ms | 118.01 ms | 4.27 ms |

These are steady periodic *phase* measurements from physical angle to native
animated rim position, not the earlier first-response threshold or complete
motion-to-photon latency. A half-degree cap limits benefit during faster motion.
Observed 99th-percentile prediction lead was about 0.47 degrees at 0.5 Hz and
0.5 degrees at 1 Hz. Separately published angle/output atoms can yield a torn
external sample: the off trace also had one nonzero inferred lead. No inference
from a lone external maximum replaces the production bound/tests.

Stops/reversals, jitter and all three overtravel profiles completed and returned
to neutral; prediction added effectively zero during stationary holds. The jitter
profile emitted zero steering. Handoff passed. However, repeated micro-step tests
found two failures, one on and one off. `micro-verify-0-2.json` proved that a
546 ms camera miss also expired the *grab publication*. The raw controller was
still tracked, so reacquisition incorrectly rebased its neutral at -8 degrees.
This was independent of prediction and must not be reported as all tests passing.

The final fix adds `WheelMaintainGrab` on the missing-camera path: retain only
already-engaged tracked grips, immediately release absent grips, and never allow
a new grab without a normal proximity solve. A production-source test exercises
an expired camera publication, continued grab, release, prohibited regrab and
tracking loss. All 20 driving tests and 42 roomscale tests passed; Release built.

Latest installed DLL SHA256:
`61d3684007b2262d923d69f320967e38655df0209277392ac14e38a065b2ee05`.
Deployment: `build/roomscale-plugin-deploy-20260924-213908/deployment.json`.
The user's runtime INI SHA256 remains
`ca6b0e6ba59cca78fbe909a7e51ffb8ba8fd0079a68498c6711b0cf6c3c44774`.
Prediction was restored to 0 and the four drive-model rates restored before
PID29212 was closed. The user relaunched in the focused driver view as PID17492.

Evidence: `build/driving-prediction-live-20260924/`. It contains raw idle/ledger
captures, sine A/B runs, `comparison.json`, failed and successful micro traces,
test XML/build logs and per-trial original INI copies. Use
`runtime-final-activation.json` to restore the normal OpenXR manifest after
confirming the next process loaded this directory's private test runtime.
The loaded test runtime can keep running after manifest restoration.

Additional reusable test sources: `prepare_prediction_runtime.py`,
`motion_profile.hpp`, `offsets.cpp`, `analyze_prediction.py`, and `grab.cpp`.
The live probe verifies the native prediction field from freshly compiled
offsets, restores the exact INI, releases inputs and restores poses in `finally`.
The runtime's bounded trajectory lease also drops grips if the runner disappears.

## Final verification on PID17492

All 12 final live runs passed: eight alternating off/on micro-step repetitions,
one sinusoidal A/B pair, a stop/reversal profile with prediction on, and an
overtravel/neutral-return profile with prediction on. The maximum observed camera
publication age across these runs was 94 ms; the micro repetitions stayed at
32-47 ms. There were no repeat failures of the neutral/animation settle checks.

The final sinusoidal pair measured 124.84 ms native phase lag with prediction off
and 119.71 ms with it on: a 5.14 ms reduction, consistent with the earlier paired
4-5 ms average. The input's fitted phase lead was 6.33 ms and the 99th-percentile
angular lead 0.474 degrees. This is a small continuing-motion improvement; step
from rest is deliberately not predicted.

The final 30-second idle observation had no large published target jumps and no
exhausted pose cache. Input/grip publications and both controller poses were
read back after the trials. Prediction was restored to 0, steering/grips/blends
to zero, all four original drive-model rates confirmed, the exact original INI
restored, and the standard OpenXR manifest restored. The running process retains
its already-loaded private runtime until exit; later launches use the original.

Final evidence is `final-micro-*.json`, `final-sine-*.json`, `final-stop-on.json`,
`final-lock-on.json`, `idle-verified-17492.json` and `final-state-17492.json` under
`build/driving-prediction-live-20260924/`. The earlier failing traces remain for
provenance and must not be silently counted as passing. Current source validation:
20 driving CTests, 42 roomscale CTests, eight private-runtime tests, Release build.
After these tests the user explicitly asked to enable prediction. The runtime
INI was changed to `xr_wheel_prediction=1`, with an 8 ms horizon and the existing
0.5-degree hard cap, and the native setting was read back as enabled without a
restart. The user then confirmed that the behavior feels good and requested the
commit. The shipped default remains off; their saved runtime choice is on.
