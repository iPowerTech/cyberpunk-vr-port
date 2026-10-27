# Framegen with DLAA and native TAA

## Changes

- CAS now runs once on each exclusively owned real capture before publication.
  Both frame generation and direct XR copies consume that same filtered endpoint.
  Generated/repeated frames are no longer sharpened again. The CAS formula and
  user's strength setting are unchanged; no depth/MV inputs are needed by CAS.
- Native TAA supplies framegen inputs without Streamline evaluation. In 2.31,
  ApplyTXAA at RVA `0x768510` reads resolved depth `DEBF0C27` and motion
  `917D8BC9`. The resource resolver provides their current physical handles.
  The native command-context state ledger supplies the actual read states at
  the draw; the source state is restored after copying plane zero.
- Native camera identity uses the current node's eye, not the DLSS viewport
  override flag. Frame, eye, pose, origin and queue checks remain mandatory.
- TAA's measured inputs are 2560x2560 depth with 1280x1280 motion. DLAA instead
  provides both at 2560x2560. A private preparation pass aligns their sizes,
  keeps depth and motion samples associated, scales jitter to the working grid,
  and preserves normalized displacement. Full-resolution DLAA color is unchanged.
  Full-display-resolution preparation is bounded to half resolution per axis;
  interpolation output remains full resolution.
- Completed GPU input leases are collected every presentation cycle, including
  real endpoints and repeats. Previously they were collected only when another
  generation started, while exhausted input pools prevented that next generation.
  Each eye now has six lazily allocated slots for three GPU jobs, a real endpoint,
  the latest capture and a producer.
- Automatic pacing still targets at most half refresh, but backs off by 4% after
  repeated missed adjacent frames within a second. It recovers slowly after five
  healthy seconds. A single spike/camera transition does not impose a lower cap.
  This avoids rendering DLAA frames that cannot be interpolated before the next
  display selection replaces them. It does not guarantee full refresh under GPU
  load or change the game's own maximum-FPS setting.

## Findings

The original headset report was 15 generated FPS with DLAA. In controlled runs
the reported real rate was about 45 FPS, but generation ranged roughly 19-32 FPS
with frequent priming and input skips. The pair diagnostic showed consecutive
selected captures skipping one native render frame (`N -> N+2`, about 44 ms),
without a camera reset or projection change. Interpolating those using one-frame
engine motion would be incorrect, so that safety check remains intact.

Retiring leases and increasing the pool removed input skips. They alone did not
eliminate lost neighbors under DLAA. A temporary 40 FPS game limit gave about
40 real + 40 generated FPS, validating the need to leave enough GPU/display time
for interpolation. That temporary game limit was restored to disabled/240.
Dedicated normal/high-priority GPU queue experiments did not provide a useful
gain and were removed. The final implementation retains the original game queue.

## Verification

Framegen/CAS-order candidate tested live (before the CAS-default follow-up), SHA-256:
`b39bb915876546b9708af442c0de6ce4c4f2eb56dd80a4bd3fb094de4d7b3fc1`.

- 19 framegen tests passed, including WARP pixel comparisons, mixed input
  resolutions, format changes, independent eyes, input retirement, pacing and
  planar-depth copies. NVIDIA OFA hardware fixtures also passed.
- The CAS GPU test passed 30 queued draws, including in-place real captures,
  separate outputs, image edges, format changes and GPU lifetime checks.
- PID 15940, OpenXR Simulator, native TAA / scaling Off: about 45 real + 45
  generated FPS. A 25-second head-motion probe had zero capture/input-identity
  failures; output remained about 89.5-90 FPS. This preceded the pacing change.
- PID 11728, final candidate, confirmed DLAA (`quality=5`): roughly 38-41 real
  and generated FPS, about 76-81 unique output FPS in this scene. The 25-second
  slow/fast head-motion probe had no input skips, pose-copy changes, capture
  failures or XR submission errors. This is simulator evidence, not physical
  headset visual-quality confirmation.
- PID 12064, final candidate after a cold start with scaling Off: native TAA
  capture advanced in both eyes with zero native-copy failures. Output settled
  at 45 real + 45 generated FPS. A 25-second motion probe had a 90 FPS median
  during slow turns (brief minimum 86.5), no input skips or capture/identity
  errors, and restored its pose. Reports: `build/framegen-12064-native-final.json`
  and `build/framegen-12064-native-motion.json`; stereo image:
  `build/framegen-12064-cas-before.bmp`.
- Captures: `build/framegen-15940-native-motion.json`,
  `build/framegen-11728-dlaa-adaptive.json` and
  `build/framegen-11728-dlaa-motion.json`. Reports named
  `framegen-11728-native-*` were an attempted hot switch, but NGX still ran;
  they must not be cited as native-TAA verification.

One early PID 4232 launch crashed at `Cyberpunk2077.exe+0x1DB1EC` with a
noncanonical pointer. The dump's stack scan did not identify plugin frames,
which does not establish the cause. The report was retained and CrashReporter
closed. It did not recur in the subsequent tests. One later startup produced a
black image despite a world-ready scripting status; that run was rejected and
restarted, not counted as a successful rendering test.

The user subsequently confirmed that the fixes work. FSR/XeSS, ray
reconstruction and hardware-headset performance were not independently measured
by these simulator tests.

Preferences at the end of the live test: framegen enabled, CAS strength 1, scaling Off as
selected by the user before testing, game's maximum-FPS limiter disabled with
its original 240 value retained. Temporary metrics and diagnostic gates were
restored.

## CAS Defaults And DLAA

The user's follow-up changes the CAS default to 0 in the live controls, overlay
snapshot, missing-key fallback and first-launch INI. Existing user preferences
are not overwritten. The original MAIN NGX evaluation reports the game's actual
quality; DLAA (quality 5) suppresses CAS even with fixed foveation disabled. Private
peripheral DLAA evaluations cannot activate this gate. The main graph's upscaler
selection clears stale DLAA state when returning to native TAA or another
upscaler. Until a newly selected DLSS mode is known, CAS is conservatively off.

In DLAA, the overlay displays a disabled zero-strength slider. The saved strength
is retained for other modes. The policy test covers defaults, DLAA and non-DLAA
transitions, unknown modes, MAIN/second-eye isolation and strength restoration.
The follow-up Release build and 40 tests (framegen, CAS, foveation, overlay)
passed. It was not redeployed during this commit-only request; the running game
and installed personal settings were left untouched.
