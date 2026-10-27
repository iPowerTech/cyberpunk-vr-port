# Fast HMD translation jitters while WASD is stable

## Reproduction after the accepted turn fix

The user accepted the180degree camera-offset correction, then reported body
judder specifically during **horizontal HMD translation**, not during WASD.
The first12cm/1.5s probe was too small/slow by the user's feedback. The relevant
repro therefore used **80cm in0.5s**, a hold, and a return. The existing head
orientation/height was retained. No simulator focus or keyboard input was used.

PID19332, installed `3bcdd9ee...` DLL. Read-only private-symbol locations were
resolved through the built-in x64dbg MCP. No live memory patches or breakpoints.
The control was a logged0.3s `D` pulse into the game window, with focus restored
and a final release-all. CCT peak speeds were about4.84m/s for the physical repro
and3.85m/s for the native-input control.

## Evidence

[Hash-indexed evidence and validation](roomscale-hmd-translation-timing-20260920.json).

- On the fast HMD run, camera-model lateral X ranged **124.75mm** across the
  outward and return directions; each direction separately ranged72–87mm.
- Native-input control: camera-model X range **0.208mm**.
- The hip model itself also showed larger steps in the physical run; the main
  disparity is between the body's native motion and the camera reference.
- No Lua fallback calls occurred during the captured moving phases. This run
  does not support blaming the remaining jitter on a source switch.
- Directly read raw-runtime and camera-frame poses diverged by up to132.57mm.
  More strongly:25 observations matched the diagnostic CCT `pose_sequence` to
  the exact raw runtime sequence and raw position to consumed distance. Among
  those confirmed consumed raw samples, the rendered-frame pose differed by
  **60.19mm**.

Both the raw XR and frame-pose regions were read twice; stability flags and
per-packet seqlocks are retained. This is a dense asynchronous observation,
not a claim that all memory fields came from one frozen engine invocation.

The data matches the source:

```text
physics: GetRoomscaleSample -> raw m_roomscaleSample -> CCT
camera:  AcquireFrameHeadSample -> LocateHeadPoseAt -> filtered frame pose
```

`HeadFilter=3`, `PredictFilter=3`, `OneSamplePerFrame=1` were read live. CCT/body
already consumed a raw movement that the filtered rendered head had not yet
reached. The view compensates the raw consumed distance with its older/different
head sample, producing a changing body/view displacement. Ordinary WASD moves
camera and entity in the same native transform path, without that moving HMD
residual, which explains why it hid the mismatch.

This is distinct from the model-versus-tracking yaw bug; its fix remains intact.

## Candidate correction

`GetRoomscaleSample` now obtains position from the **existing camera frame
latch**, through `AcquireFrameHeadSample`. The raw packet supplies validity,
tracking age and reference-space generation; it no longer supplies a different
position for CCT.

The latch exposes a monotonically increasing publication sequence and the time
it was sampled, both returned under its existing mutex. Movement deduplicates
by this identity, not the independently advancing raw-XR cycle. Reading a
cached frame again does not refresh its timestamp. A failed frame read does not
invent origin0 and erase an existing consumed-distance correction.

Raw tracking remains available for discontinuity detection. A large raw jump
must not be hidden by the camera filter and reinterpreted as many small walking
steps. The movement accumulator rebases the whole filtered jump until it has
settled within1mm of the new raw position; then ordinary tracking can resume.

No additional low-pass filter or world-position chase target was added. The
existing camera's filtered trajectory now supplies the movement trajectory too.
Thus an already-configured camera-filter settling tail is shared, rather than
rendered relative to a body that took the unfiltered step. The native collision
and velocity-feedback path still owns the resulting requested displacement.

## Offline checks

A new regression was first run against the old raw selector and failed. It
simulates a fast filtered80cm trajectory, differing XR/game cadence and repeated
physics reads within one camera frame. It also checks:

- loss of tracking despite a cached valid frame;
- mismatched origin generations;
- stale tracking/frame timestamps;
- invalid raw coordinates;
- multi-frame filtered response to a raw tracking teleport;
- a failed camera-frame read preserving the old consumption ledger.

All15 checks passed, including the previously accepted yaw-frame tests.
The Release build succeeded. Post-fix live/user acceptance of **fast translation**
is still pending. Settled-position tests alone will not close this issue.

Artifacts in `build/`:

- `roomscale-temporal-19332-addresses.json` (historical, invalid after restart);
- `roomscale-body-temporal-19332-hmd-before.json` (slow control);
- `roomscale-body-temporal-19332-hmd-fast-before.json`;
- `roomscale-body-temporal-19332-hmd-fast-pose-before.json`;
- `roomscale-body-temporal-19332-wasd-before.json` and `.input.jsonl`;
- corresponding `*-analysis.json` summaries.

Tools: `body_temporal_probe.py`, `analyze_body_temporal.py`,
`verify_temporal_before.py`. Fresh private-symbol addresses must be resolved for
the next PID/build, including the changed frame-pose layout.

## Candidate deployed

PID19332 was closed via the built-in x64dbg MCP. DLL-only deployment completed;
settings hash unchanged. The accepted turn-fix DLL is retained in the backup.

- Candidate SHA-256: `e7931050c261569b42e81c3d499ed18f036ca521f518bff5caa39d06948e13a2`.
- Size: **2,482,688 bytes**.
- Backup/report: `build/roomscale-plugin-deploy-20260920-132754/deployment.json`.
- Final tests after all guards: **15/15**, Release build successful.
- Game left stopped; only the user launches it.

Next acceptance: repeat the fast80cm/0.5s translation and compare moving-phase
camera/body variation, not merely the final return position. Resolve the new
frame-pose publication sequence address to verify that CCT now consumes the
latched camera sample. Also check20°/180° and recenter for regression. This
candidate has no post-deployment live result yet.

## Follow-up on e793 (PID2708)

The frame/CCT match was confirmed live: matched frame publication IDs have
identical consumed positions, and the large camera-model translation mismatch
largely disappeared. The user still observed smaller body jumps and clarified
that Bake also moves the body. A further paired-native-base correction and its
candidate deployment are recorded in
[body-base / Bake report](roomscale-body-base-bake-20260920.md).
