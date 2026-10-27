# Mono Sun Cascades

## Ownership Bug

Skipping MAIN's sun-shadow draws/clear while retaining the same native texture
pointer is not sufficient. The native atlas is a placed resource in an aliased
heap. Live instrumentation observed an invalidating alias barrier each pair at
Low, and additional aliasing while High's four layers are recorded. Descriptor
identity and equal dimensions do not prove the depth contents survived.

This fits the earlier quality-dependent MAIN failures. It is also required by
[D3D12 placed-resource initialization rules](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createplacedresource):
an activated aliased depth target needs initialization, which can be supplied by
full subresource copies. Native aliasing barriers and discards must remain valid.

The saved 10323 capture was opened once for GPU atlas comparison, with the game
closed. Both 1024x1024 layers survived unchanged in replay until MAIN's clear;
MAIN rasterization differed on only 1507 / 1495 texels (about 0.14% each).
That replay alone does not prove live placed-memory preservation. Replay and
capture were closed before subsequent game launches.

## Implementation

- VRCAM remains the canonical producer. Source cascade layers are copied to a
  dedicated committed depth-array texture before their native release/aliasing
  boundary, or at command-list Close when no earlier release occurred.
- MAIN keeps the native graph's resource activation and transitions. Its clear
  is replaced by full-layer copies from the committed atlas. Its duplicate
  RenderShadowCascade node bodies are then skipped.
- Existing fit-record and sampling-matrix synchronization remains in place.
  Local light shadows, per-eye shadow receiving/filtering, geometry LoD, DLSS,
  framegen and async-compute settings are not modified.
- Atlas size, format, array layers and active matrix count come from current
  native resources. Cache reconstruction handles quality/distance changes.
- A view-pair lease needs the complete current producer, observed cameras,
  matching MAIN sampling matrices and all fresh copied layers. Each MAIN cascade
  can consume a lease once. Missing/unknown/incompatible inputs fall back to the
  native renderer instead of consuming a stale atlas.
- DSV creation and copies are tracked, including signatures created before the
  swapchain. Unknown dispatch-style indirect work is not treated as raster work.
- Images and participating resources use CommandResources for recording/GPU
  lifetime. Completed recording entries are removed at Close. No new queue,
  CPU/GPU wait, resolution reduction or second shadow rasterization is introduced.

Control: `xr_mono_cascades=0/1`, exposed as **Shared sun shadows** in GENERAL.
The installed user's setting is enabled. The new-install default remains Off.
Diagnostics: `CyberpunkVR_MonoCascadeReport` and its seqlock export. The legacy
report name `alias_invalidations` counts native alias events, not copy failures.

## Verification

`tools/mono_cascade_tests`: 144 combinations of cascade-count/resolution changes,
resource replacement, stale/repeated lease refusal, partial coverage, camera and
memory-overlap cases. The WARP GPU fixture checks 2/3/4/2 layers, descriptor copies,
intermediate alias barriers, deliberate destruction of native depth between eyes,
restoration from committed storage and per-layer native fallback. D3D12 debug
validation reports no errors. Release build and diff checks pass.

Low live, PID 19620: 2 x 1024x1024. After startup's incomplete pair, MAIN draw
count remained constant; two MAIN nodes were skipped and two source slices
captured each pair, with zero capture failures.

The user's first High visual confirmation occurred on an earlier conservative
candidate that had fallen back to dual rendering. It is NOT evidence that mono
was active at High. Counters revealed the fallback; native aliasing between
already-preserved layers was then separated from committed-cache validity.

High live, PID 22872, DLL
`FC56A074C66F3A340E629FC77DD4B351464FBCA41C22A65CD18B4B171B488342`:
4 x 4096x4096, mask 15 for captured layers/completed producers/cameras. MAIN draw
count does not advance in valid mono intervals. Four MAIN nodes are skipped per
pair, with four captured slices and one restored atlas. Zero capture failures,
signature/binding/camera failures and fallback increments during timed samples.
Both-eye screenshots show retained sun shadows without a dark MAIN image.

Interleaved 25-second samples at High range AND resolution, framegen OFF, VRS OFF,
DLSS Balanced and async compute ON:

| Native | Mono | Native | Mono |
| --- | --- | --- | --- |
| 72.47 FPS | 74.81 FPS | 71.94 FPS | 73.77 FPS |

Means: 72.21 vs 74.29 FPS, about +2.89% in this save. GPU utilization stayed 96%.
The two mono samples skipped 7536 / 7360 MAIN cascade nodes and recorded zero
MAIN shadow draws. This is a scene-specific base-FPS result, not a universal
percentage. Staging-copy cost is included; no framegen frames are counted.
Evidence: `build/gpu-audit-20260930/mono-cascades-high-ab/` and `mono-cascades/`.

An attempted CET settings query used the wrong group `/graphics/quality` and
caused the configuration-registry assert in PID 16116. The real group recorded
in UserSettings.json is `/graphics/advanced`. That crash was caused by the probe,
not by enabling mono, and is not a random-crash/optimization result. The bad query
was not retried. The user changed the shadow settings through the ordinary menu.

Final hardening additionally claims each MAIN index once per source lease and
retains the actual MAIN destination identity for fallback. Its final deployment
and live confirmation are recorded below after verification.

## Whole-Pass Cost Breakdown

The user's question about the earlier approximately 1 ms capture measurement was
checked with bounded in-game timestamps, not inferred from draw counts. The scene
profiler now optionally filters one node and groups across PSO changes. Cascade
indices and cache operations have separate detail tags; discontinuous native
segments are summed and divided by contributing command lists, not segment count.
The profiler remains disabled outside explicit requests.

PID 26720, High 4 x 4096x4096, alternating native/mono, 128 completed command-list
samples per run, no discarded samples:

| Run | MAIN raster | VRCAM raster | Capture to cache | Restore MAIN | Cache clear |
| --- | ---: | ---: | ---: | ---: | ---: |
| Native 0 | 0.706 ms | 0.690 ms | 0 | 0 | 0 |
| Mono 1 | 0 | 0.643 ms | 0.174 ms | 0.166 ms | 0.003 ms |
| Native 2 | 0.688 ms | 0.645 ms | 0 | 0 | 0 |
| Mono 3 | 0 | 0.718 ms | 0.122 ms | 0.168 ms | 0.002 ms |
| Native 4 | 0.764 ms | 0.661 ms | 0 | 0 | 0 |
| Mono 5 | 0 | 0.760 ms | 0.138 ms | 0.197 ms | 0.003 ms |

Each raster figure totals the four cascade-index averages. Copy timings include
their resource transitions. The surviving source raster and overlapping async
work still run. Approximately 0.3 ms of staging offsets the eliminated MAIN
raster work, consistent with the roughly 0.35-0.4 ms end-to-end frame reduction.
These short instrumented samples have variance and are not a new FPS result.
Do not turn earlier per-draw RenderDoc sums into guaranteed whole-frame savings.
Raw evidence: `build/gpu-audit-20260930/mono-cascades-gpu/`.

Installed measurement/final DLL:
`D8E2079ED054331860982682F3ACDDE13D38F672C370CAD06DB11556B5F6A644`.
Mono, GPU-stage and scene-profiler tests pass. Profiling request/group/filter
controls were restored to zero, and `xr_mono_cascades=1` remains enabled. User's
High range/resolution are preserved. No commit or push was made.
