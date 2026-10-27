# Periodic distant-background flicker in both eyes

## Reproduction and cause

The user reported roughly half-second bright/dark switching in the distant
Badlands landscape, affecting MAIN and VRCAM. They explicitly confirmed that it
predated FogHistorySync. That setting was left unchanged for this investigation.
RenderDoc was not used or launched.

The existing `Detour_SkyWork` already supports skipping VRCAM's duplicate sky
producer, but `CyberpunkVR_SkyReuseMode` defaulted to 0 after an older experiment.
The native `7818F8` worker updates and publishes an amortized sky cache. Both
camera paths were advancing that shared cache.

A short x64dbg MCP trace in PID 18084 confirmed:

| View | Context | Sky manager | Record index |
| --- | --- | --- | --- |
| VRCAM | `19747CCC0D0` | `193256EB520` | 0 |
| MAIN | `1972178A950` | `193256EB520` | 0 |

The addresses identify this recording only. They are not part of the fix.
The native record contains a dirty flag at manager +72, a build cursor at +80,
and its publication timestamp at +88 (plus index *32).

## Reversible live comparison

Stationary captures used the existing game and OpenXR Simulator windows via
PrintWindow, without moving focus. RGB ROI means were converted to 0–255 luma;
a step means an absolute consecutive-sample change greater than 2. Samples were
taken about every 80 ms. The windows were not modified.

Only `CyberpunkVR_SkyReuseMode` changed between these recordings:

| Mode / recording | Duration | Distant terrain maximum step | Steps >2 |
| --- | ---: | ---: | ---: |
| 0, baseline | 10 s | 27.36 | 8 |
| 1, MAIN-only update | 10 s | 1.26 | 0 |
| 0, restored baseline | 10 s | 29.82 | 20 |
| 1, repeated MAIN-only update | 12 s | 1.04 | 0 |

The restored baseline's median step interval was 0.401 s. Near rocks and the
upper sky had no comparable jumps. The return of the effect at 0 and its removal
again at 1 distinguish the fix from an unrelated weather transition.

Two intermediate recordings named `sky-main-only` and `sky-main-only-1` remained
at mode 0: the debugger refused their flag writes while running. Their per-frame
mode readback records this, and they are not counted as enabled trials. Subsequent
export writes used the project's PID/DLL-verified memory helper. No render thread
was left stopped during the recordings.

Both-eye verification used a second set of ROIs in the simulator:

| View / mountain ROI | Baseline maximum step / count | Fixed maximum step / count |
| --- | --- | --- |
| VRCAM | 19.58 / 18 | 0.36 / 0 |
| MAIN | 19.44 / 16 | 0.38 / 0 |

Baseline duration was 8 s, fixed duration 12 s. The simulator's PrintWindow
readback sometimes returned an empty client image: 13 of 100 baseline reads and
24 of 150 fixed reads. Those reads were excluded using the same rule (all ROIs
must contain nonzero image data), with the raw samples retained. The separate
game-window A/B/A/B captures had no blank reads.

The correction does not freeze sky updates: 500 passive native-record reads
over 10.16 s observed 41 distinct publication timestamps, dirty flag values 0/1,
and build cursor values 0–4. Cloud positions also continue changing in the saved
images. The test covers this stationary scene; it does not claim a complete
weather or story-camera regression sweep.

## Change and validation

`CyberpunkVR_SkyReuseMode` now defaults to 1 in `ViewReuse.cpp`. The existing
native hook lets MAIN drive the shared sky cache and skips only VRCAM's duplicate
producer. The other eye still consumes the published sky. This adds no resource,
readback, counter, allocation, or per-frame work to normal rendering. The opt-out
value 0 remains available for diagnostics.

- Release build passed.
- No new implementation-mirroring unit test was added for a default-value change;
  the reversible live experiment verifies the actual rendering behavior.
- x64dbg was detached after the bounded trace; RenderDoc remained closed.
- Artifacts, raw samples, ROI definitions and summaries:
  `build/background-flicker-20260926/`.
- Deployment details are recorded in that directory's `deployment.json`.
- The built PE export was verified to contain the new default value 1. The game
  was closed after the live tests, and the DLL was installed with matching SHA256
  `DA0EE2C8BABDB1226A53379D0FBAD2788E8CBBDF7439358EADB5AA5FFCA9D08F`.
  The previous DLL is backed up outside the game. The game was not relaunched;
  the live verification above used the identical existing hook with its flag
  switched to the new default.
- The user subsequently confirmed the installed fix works.
