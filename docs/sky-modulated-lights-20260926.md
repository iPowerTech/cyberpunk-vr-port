# Missing world highlights in VRCAM

## Evidence

Capture: `render-doc-capture/highlights.rdc`, Cyberpunk 2.31. The marked roof,
diagonal palm and entrance surfaces receive a bright local-light contribution in
MAIN which is almost absent in VRCAM. These are scene lights, not HUD markers.

At render resolution 1485 x 1485:

- The first fullscreen lighting draws are VRCAM EID 40790 and MAIN EID 85244
  (pixel shader `50a36b372da887c1690eb795716707e3`). The mismatch is already present
  in their t5 input, ResourceId 183561, before fog, DLSS and composition.
- Pixel history at (338,95) attributes the MAIN highlight to compute EID 85029
  (shader `4de0e10297634659b7c229ebe318fdcb`). The corresponding VRCAM dispatch is
  40576. Both run; this is not a missing pass.
- The LightParams buffer, ResourceId 31714, has different light ordering. Matching
  records by position rather than index exposes RGB values exactly 1000 times
  lower for a subset of VRCAM lights. Other fields of those records agree.

Example position (-1788.47876, -2434.55542, 37.19129):

| Buffer record | RGB |
| --- | --- |
| VRCAM 107 | 0.056978527, 0.07859397, 0.09591175 |
| MAIN 93 | 56.978527, 78.59397, 95.91175 |

The RenderDoc replay was closed before live game work. No concurrent game/replay
or second replay instance was used.

## Native cause

Short x64dbg captures in PID 16344 followed the same light through these 2.31 RVAs:

1. `23B4B4` packs visible light records.
2. `23D9A8` applies distance fade; `23DA14` computes light color. A light whose
   proxy byte +DC is 1 samples the sky lookup via `AE2F2C`.
3. `AE2F2C` interpolates a 16 x 16 RGB table at LUT +1A0 and multiplies its
   channels by LUT +DAC, +DB0, +DB4.
4. MAIN and VRCAM use the **same LUT allocation and identical table bytes**.
   The gains change during CPU preparation:

| View | Gains | Sampled sky RGB |
| --- | --- | --- |
| VRCAM | .030000003, .060146496, .093784600 | .00006932489, .00014107973, .00020407868 |
| MAIN | 30.000004, 60.146496, 93.784599 | .069324888, .141079724, .204078674 |

5. A write watchpoint identifies `61849F` in `618030` as the gain writer.
   Its eighth argument is an RGB/intensity float4. Caller `617E80` supplies
   `viewData +440`; it runs from `6170BC` during `219730`'s StorageData preparation,
   **before GPU-node dispatch**.
6. The existing environment mirror covers +440, but runs at node dispatch.
   It therefore corrects GPU constants after CPU sky storage has already consumed
   the native RTT values. A separate early observation at FlagCompute `1D49540`
   confirms the source discrepancy there: MAIN approximately (30,66.535,98.538,1),
   VRCAM (.030,.066535,.098538,1).

Raw evidence and shader dumps: `build/highlights-20260926/`.

## Fix

`ComputeFlagsWithMainAa` now also observes the real MAIN view's sky radiance and
copies precisely the 16 bytes at +440 into the selected VRCAM's own viewData,
before native preparation. This gives CPU sky storage the same authored radiance
that the later GPU environment mirror uses. There is no fixed x1000 compensation,
global exposure change, cross-eye camera matrix, borrowed light list, or new GPU
resource.

The observation requires a running stereo player and the exact MAIN context.
It expires after one native frame and is rejected after player, renderer, camera,
tracking-origin, selected-eye name or resolution changes. Non-finite, negative or
overflowing radiance is rejected; zero is a valid authored value. A missing source
leaves native behavior intact. VRCAM-first ordering uses the preceding MAIN frame,
as the existing environment mirror does.

`CyberpunkVR_SkyRadianceSync` defaults to 1 and permits a live A/B. The three
`CyberpunkVR_SkyRadianceCounters` and eight-float input/source snapshot are only
updated under `CyberpunkVR_RenderParityDebugCapture` or the global diagnostics gate.

## Validation

- Release build passed.
- Render-parity and native cache-thunk ABI tests passed.
- 32 sky-radiance tests passed: both eye orders, missing/future/skipped frames,
  counter wrap, all owner changes, weather/intensity changes, zero, NaN, infinity,
  negatives, and overflow.
- Installed DLL SHA256:
  `2EA86EB0BDC4A2C4FF3A8D05595E9C29A1CA8A6B63718F144406764EDF79E6B5`.
- In-game PID 5808: 156 MAIN observations and 156 VRCAM corrections in 3.6 s,
  zero unavailable/invalid samples. Native VRCAM input remained approximately
  (.030,.067529,.099277,1); the early source was (30,67.528,99.277,1).
  The targeted diagnostics gate was restored to 0. This check used readback of
  the counters without debugger pauses.
- User confirmed: the marked lighting is now visible in both eyes.
- x64dbg is detached; RenderDoc is closed.
