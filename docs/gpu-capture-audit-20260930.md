# GPU Capture Audit, 2026-09-30

## Scope And Provenance

The supplied series is `10frames_10319.rdc` through `10frames_10328.rdc`,
recorded September 27 at 13:48-13:49, not the current September 30 renderer.
The existing `build/render-perf-20260927/frame-*.json` files already contain
explicit GPU-duration counters for these exact paths and frame numbers.
All ten reports were aggregated again, checking counter units, event coverage,
finite nonnegative values, sums, file availability and modification times.
This is metadata validation, not a historical whole-file hash comparison.

Only frame 10323 was GPU-replayed in this investigation. A single session
collected input primitives, vertex/pixel/compute invocations and passing samples,
joined them to the saved event timings, and reconstructed PSO ownership from
command-list-local recording state. The capture and replay device were closed
in `finally`. Separate CPU-only inspection exported two shader pairs; those
cached capture files were also closed. The game was not launched, and was not
running during replay. No render passes, graphics preferences or installed
files were changed. No FPS gain is claimed.

New evidence and reproducible scripts:

- `build/gpu-audit-20260930/series.json`: all ten frames, node/target groups.
- `build/gpu-audit-20260930/frame-10323-detail.json`: new pipeline counters,
  PSO groups, per-event data, resource names and CopyResource inventory.
- `build/gpu-audit-20260930/inspect_capture.py`: one replay session, with a
  game-process guard and deterministic teardown.
- `build/gpu-audit-20260930/summarize.py`: offline aggregation of all ten
  saved profiles, requiring no capture opens or GPU device.
- `build/gpu-audit-20260930/pso-152910` and `pso-152895`: captured shader blobs.

## Series Ranking

These are medians of summed instrumented replay event durations, in milliseconds.
They rank recorded workloads, not live pass boundaries, FPS or removable cost.
Per-draw instrumentation can disproportionately penalize many small draws.

| Recorded group | MAIN | VRCAM | Draws MAIN / VRCAM |
| --- | ---: | ---: | ---: |
| RenderElements, 4 color targets | 4.280 | 4.221 | 581 / 589 |
| RenderElements, 3 color targets | 2.942 | 2.946 | 390 / 396 |
| RenderElements, 2 bound color targets | 2.160 | 2.136 | 434 / 433 |
| RenderElements, depth only | 0.242 | 0.275 | 81 / 87 |
| Shadow cascade 1 | 0.936 | 1.011 | 191 / 205 |
| Shadow cascade 0 | 0.386 | 0.413 | 85 / 94 |
| DrawComposition | 0.266 | 0.287 | 34 / 34 |
| Bloom/tonemapping | 0.240 | 0.239 | 16 / 16 |
| VolumetricFog | 0.195 | 0.196 | 1 / 1, plus 24 dispatches each |
| DrawConeAO | 0.186 | 0.184 | 5 dispatches each |

Aggregate RenderElements medians remain 9.742 ms MAIN and 9.600 ms VRCAM.
There is no broad evidence here of VRCAM doing substantially more scene work
than MAIN. Sharing screen-space outputs would change parallax and temporal
history; matching costs are not evidence that those passes are redundant.

The unmarked bucket must not be assigned to the scene renderer wholesale.
Simulator preview command list 3356 performs 19 copies in each odd-numbered
capture and 39-44 in the even captures. In 10323, those 19 preview copies alone
sum to 6.462 ms; 38 preview draws into the 1920x633 preview surface add
1.092 ms. These are capture/runtime-preview workloads, not 19 game frames or
19 necessary per-eye scene renders. Total replay sums of 40.75-56.24 ms are
therefore particularly unsuitable as live frame times.

No `DispatchRays` action occurs in this series. This does not exclude every
possible inline ray-query shader, but it provides no basis to optimize the
current RT/FG path. The recorded engine CAS pass is not the port's later CAS
filter. Current native fixed-foveated DLSS and the latest framegen changes
also postdate these captures.

## New Material-Level Finding

Pipeline counters below were measured now for frame 10323 only; their scope
must not be extended to all ten frames or the present live scene.

| PSO | Eye | Draws | Zero passing samples | PS invocations | Passing samples |
| --- | --- | ---: | ---: | ---: | ---: |
| 152910 | MAIN | 433 | 371 | 179,052 | 6,093 |
| 152910 | VRCAM | 432 | 373 | 186,316 | 6,299 |
| 152895 | MAIN | 433 | 392 | 4,832 | 1,216 |
| 152895 | VRCAM | 432 | 396 | 4,612 | 1,206 |

Together these two PSOs account for 1,730 draw calls, with 1,532 producing zero
passing samples in this replay. Matching counts and primitive totals do not
prove that the two PSOs can be merged or that every draw addresses the same
objects. The saved duration sums are 6.101 ms for PSO 152910 across both eyes,
and 4.230 ms for 152895; these are NOT a 10.331 ms attainable saving.

PSO 152910 has four G-buffer outputs, depth writes and stencil replacement.
Its pixel shader includes discard and exports a computed `SV_Depth`, with
depth derived from sampled material data and camera transforms. The shader
is byte-identical to the previously exported `13306-Pixel.dxil`:
SHA-256 `19a5b3f22600fcddb3ea1bf63a3b5c3feecc91a33481913de868a19a7e40a991`.
Its VS also matches the existing 13306 fixture. This is not a depth-only
draw that can use the existing validated single-pass depth prototype unchanged.

Under the [DirectX VRS specification](https://microsoft.github.io/DirectX-Specs/d3d/VariableRateShading.html#export-of-depth-and-stencil),
exporting depth/stencil forces fine-rate pixel shading. Merely applying a
coarser VRS map cannot accelerate this shader's pixel invocations. Removing
the depth export or forcing early depth/stencil would alter its semantics.

Zero passing samples is not an occlusion-only classification: depth/stencil,
discard, clipping and coverage can all contribute. Querying that result after
rendering and dropping the draw next frame would introduce disocclusion errors
under head movement. A cached zero result is not a safe visibility predicate.

## Optimization Priorities

1. Verify these material families in the current build with whole-group GPU
   boundary timestamps and native packet identities. Focus on reducing small
   draw submissions through compatible batching and conservative per-eye
   visibility. Preserve material bindings, instance data, depth/stencil and
   draw order; do not assume identical PSO means mergeable draws.
2. Extend exact-output stereo sharing only after resolving scheduling and
   material coverage. The existing depth-only prototype saved about 0.056-0.059
   ms for one instrumented group, not overall FPS. Its diagnostic waits must
   not become production synchronization. See `single-pass-stereo-20260927.md`.
3. Use current-build measurements for foveated DLSS and FG input preparation.
   These old files cannot assess them. Foveated DLSS reduces upscaler work,
   not the small geometry draw calls identified above.
4. Treat simulator preview/readback cost separately from headset rendering.
   A preview optimization must not be advertised as a headset scene-FPS gain.

Do not re-enable local-shadow reuse, copy MAIN fog grids into VRCAM, remove
second-eye postprocess passes, force VRS over depth-exporting shaders, or enable
the experimental single-pass route based only on these replay sums. Existing
visual-correctness fixes and all user settings remain intact.

This completes the supplied-capture analysis, not the broader GPU optimization.
Production changes and an equivalent-scene live A/B are still required to
demonstrate an actual performance improvement.

## Continuation And User Constraints

The user explicitly requires meaningful gains in base/game FPS with framegen
OFF, no reduced detail or quality, and no time spent on microsecond-level HUD
changes. Async compute must remain enabled; do not repeat the disabling test.
No significant production optimization has been achieved in this continuation.

- Framegen barrier and sparse-HUD experiments were fully withdrawn from source.
  Neither was deployed to the game. Do not count their fixture results as game
  FPS improvements or reintroduce those changes.
- A default-off whole-range scene GPU profiler was added and tested, including
  query lifetime across reset, nonblocking retirement and replay rejection.
  Installed diagnostic DLL is `a529c37a0c656b38ec238a18453dc47e1b48a94497f51cd58f323b9cb3cb454e`.
  It contains the profiler, not either withdrawn optimization.
- Valid base-FPS sample in PID 23624: about 73.15 FPS, FG=0, median reported GPU
  interval 13.84 ms. Both eyes and scenery were visually verified in
  `build/gpu-audit-20260930/baseline-23624.bmp`.
- PID 12760 completed 256 native command-list samples with zero discarded
  recordings. Whole-range results are `scene-12760.json`. Ordinary DLSS is
  around 1.35 ms per VRCAM evaluation in that sample; the old per-draw RenderDoc
  sums greatly exaggerate the apparent cost of tiny-draw groups. The sample
  includes partial frames and outliers: do not claim exact frame-wide totals
  by dividing every node by one common frame count.
- CompositionPostProcess is required for VRCAM: `final_frame_copy_now` extracts
  its finished image. Older comments saying composition is unused do not describe
  the currently active capture path. Do not remove it as redundant HUD work.
- Async-compute-off runs producing 140-150 FPS had no second-eye capture and a
  black image. They are INVALID measurements, not an improvement. A later
  async-on run, PID 15192, produced valid stereo capture at about 71.82 FPS.
  The native renderer can remain in menu mode after QuickBoot reports
  `world_ready`; script readiness alone is insufficient. A simulator primary
  button press allowed that async-on run to proceed without desktop focus.
- The original engine config and `vrport.ini` were restored. FG and its metrics
  overlay are off. All task-owned game and benchmark processes were closed.
  `async_ab.py` is disabled at entry to prevent accidental reuse.

Further work must target material amounts of duplicated scene processing or
GPU scheduling without disabling async compute, changing settings quality,
foveation or framegen. The existing single-pass prototype remains experimental;
its late visibility wait and tiny one-group savings are not a production solution.

## Hardware Trace, Counters Enabled

After the user enabled NVIDIA performance-counter access, Nsight Graphics
2026.3.1 successfully captured 12 frames from PID 26360. Framegen, foveated DLSS
and port CAS were off; async compute stayed on. Readiness was confirmed by
native second-eye GPU capture and a nonblack stereo screenshot before recording.
The game and trace viewer are now closed. No clock or quality settings changed.

Capture: `build/gpu-audit-20260930/nsight-counters-enabled/Cyberpunk2077_2026_09_30_14_02_19.ngfx-gputrace`.
The instrumentation-free reference and trace timings are different quantities:
the trace averages 16.2621 ms/frame; it is not an FPS A/B result.

### Direct, CPU-Only Decoder

The installed MCP only registers WRPV traces and reads the automatic TSV export.
It does not decode their event timeline. The investigation now has a direct
reader in `build/gpu-audit-20260930/read_gpu_trace.py`, using `lz4.block` and the
actual protobuf descriptors extracted from the installed `WarpVizPlugin.dll`.
This opens no UI, starts no replay, and runs without the game.

For this version-10 file, the first section contains three LZ4 blocks totalling
156,228,937 decoded bytes, parsed as `NV.WarpViz.PbTraceData`. Magic, version,
block bounds, exact decoded sizes and initialized messages are checked. All 12
decoded frame durations match `FRAME.xls` within 0.0001 ms, verifying nanosecond
timestamp units independently of assumptions about the binary layout. Later
sections are not needed for the queue/call/context inspection performed here.

`analyze_trace_ranges.py` ranks bottom-of-pipe timestamp intervals by queue,
command type and captured PSO. `context-intervals.json`, `decoded-summary.json`,
`timed-ranges.json` and `range-summary.json` retain the evidence. Run these scripts
with the existing Nsight MCP virtual environment, which has LZ4/protobuf and the
schema-extraction helper installed. No dependency installation is required.

### Findings

- The main game graphics/compute TSG has context residency of approximately
  11.3602 ms/frame (channels 124, 126 and 130). Residency includes idle time
  inside a scheduled context; it is NOT a direct GPU-busy measurement.
- Every one of the 12 presented frames has exactly two command streams containing
  DLSS/NGX SDK work. There is no observed third/fourth evaluation from an active
  foveated path or frame generation. The two streams contain 19 and 14 SDK calls;
  differing internal kernel counts alone do not prove redundant work.
- Completed-command intervals containing NVIDIA SDK work total 3.2783 ms/frame
  wall time, or 2.5466 ms/frame after intersecting with the same TSG's residency.
  The latter removes unrelated context intervals, but can still include work
  from a concurrent queue in the same TSG. It is a ranking estimate, not isolated
  shader execution time or an attainable saving.
- Other leading categories on queue 0, under the same interval accounting:
  indexed draws 2.1656 ms/frame; other draws 1.9916 ms/frame; indirect work
  1.2984 ms/frame; dispatches 1.2068 ms/frame. Queue 1 dispatches account for
  approximately 0.89 ms/frame. Queues overlap: do not add them into a frame time.
- Pure state/barrier intervals are approximately 0.242 ms/frame on queue 0.
  These do not include barrier work folded into a preceding draw/dispatch range.
  There is no evidence here for a multi-millisecond removable port barrier.
- Background ChatGPT GPU-context residency is approximately 0.1304 ms/frame.
  Unknown/system context intervals remain unattributed. Do not claim their
  removal as a game optimization or attribute them to a particular application.
- Nsight's displayed GPU-idle percentage is an oracle estimate based on maximum
  engine activity, not a measured collection of freely removable idle spans.
  It must not be used to promise an FPS gain.

The exploratory `analyze_trace_waits.py` was disabled: multiple waits can share
one timestamp interval, and a signal's next timestamp is only a completion bound.
Its original `wait-summary.json` double-counts some intervals and is INVALID for
timing claims. Fence identities/recorded queue calls are observable; assigning
an exact stall duration from those bounds requires additional evidence.

The preview-isolation experiment was withdrawn at the user's direction. Its
off-preview launch did not pass stereo readiness, produced no valid FPS result,
and was closed. Preview rate was restored to 90 Hz; preserve subsequent settings
writes rather than blindly replacing the current file with the earlier backup.
Do not retry this experiment or turn a simulator-only gain into a headset claim.

No production GPU optimization or FPS gain has been achieved by this analysis.
The useful new result is direct access to the full captured command/timestamp
data without GUI automation. Preserve normal rendering and asynchronous compute;
do not remove temporal/postprocessing passes or change DLSS quality/model based
only on the ranking above.

The user has explicitly redirected away from waiting/idle and simulator work:
treat the task as GPU-workload optimization of the common renderer. Do not spend
further iterations on queue-wait or simulator-preview experiments. Investigate
actual shader/pass costs and demonstrably redundant work without reducing quality.

## Native Shader Candidate: Terrain Tiling Lookup

CORRECTION: the exploratory shader-map.json driver-hash attribution below is
NOT authoritative. Later stage verification found a pixel hash linked to vertex
bytecode (1d303ffff045edfd). Per-module blob ordering cannot prove those links;
match_trace_shaders.py is disabled. Decoded native bytecode/SHA matches and the
terrain replay tests are independently valid, but do not attribute their cost
to f91443c9a3b5f384 without an explicit, verified module/program link.

One additional CPU-only batch read of frame 10323 exported 239 unique native
shader blobs from 198 pipelines to `build/gpu-audit-20260930/native-shaders`.
The file was closed after that batch. No game was running during GPU replays.

The Nsight shader-module table can now be correlated with exact native bytecode.
Three independently captured identical shaders validate a 27,227-byte prefix of
the report's SIMPLE-obfuscation mask. Only confirmed bytes are decoded; no key
extension is guessed. ShaderModuleHash values are recovered from ELF metadata.
Per-module blob ordering supplies a candidate module/program link, not an
explicit protobuf foreign key. All programs in a span must agree, DXBC bounds
are checked, and exact SHA-256 matches against the RenderDoc inventory are
retained. The output includes 215 decoded modules and 143 native bytecode matches
in `nsight-counters-enabled/shader-map.json`; ambiguous/unsupported records are
not used as proof.

The expensive indexed-draw pixel shader `f91443c9a3b5f384` maps to native PSO
153462, SHA-256 `9a4947a3ec0eb3d4605d88b663bb27a0bc809fe8a3d91c4ae67886bd77148db8`.
Its DXIL loads 16 floats into the invocation-private TerrainTilingMapValues array,
then reads just one element at an index clamped to 0..7. The offline transformer
in `tools/gpu_shader_tests/terrain_tiling.py` replaces that staging with one
direct structured-buffer read of the identical value. It changes no arithmetic,
texture sampling, precision, resource bindings, detail settings or view data.
It fails closed on extra uses, escaping pointers, UAVs, incomplete initialization,
unexpected addressing or an unproven index bound.

Verification:

- Three Python tests pass, including refusal cases and exact address/bit-pattern
  selection. These are transformer/address tests, not a GPU-output substitute.
- The real rewritten shader assembled and passed the DXIL validator.
- Repeated baseline replay was deterministic. On both MAIN and VRCAM, all three
  material targets plus depth matched the candidate byte-for-byte: zero different
  bytes across 88,209,000 compared bytes. Evidence:
  `build/gpu-audit-20260930/terrain-capture-validation.json`.
- The full shader inventory contains only two matching variants. The second,
  native PSO 165740, was assembled but was NOT GPU-output validated or deployed.
- An interleaved material-only replay timing test, with both warmups excluded,
  gave baseline sums 1.472992 / 1.473600 / 1.460000 ms and candidate sums
  1.472288 / 1.460896 / 1.463712 ms over 185 target-material draws. These heavily
  instrumented per-draw sums are NOT whole-frame GPU times. Their ranges overlap;
  the approximately 0.0093 ms median difference is not evidence of a meaningful
  game-FPS improvement. The larger initial cold/warm difference is not a gain.

Verdict: this candidate preserves the tested image but is not accepted as a
performance optimization. No runtime shader-replacement hook, shader asset,
optimized DLL deployment or FPS-gain claim was made. Keep the offline proof and
results to avoid repeating this experiment. Do not spend more work on deploying
this single material variant unless materially new performance evidence appears.

## Additional Shader Experiments, Rejected

The following offline candidates were assembled/validated but never installed in
the game. These are instrumented affected-event sums, not live frame times.
Evidence is under `build/gpu-audit-20260930`.

| Candidate | Correctness | Performance / Decision |
| --- | --- | --- |
| Vectorized structured-buffer loads | 223,858,160 bytes exact | About 4.44 vs 4.46 ms; no gain |
| Uniform material-loop early exit | 194,059,800 bytes exact | About 2.773 vs 2.772 ms; no meaningful gain |
| Material mask pruning, preserving common tail layer | 194,059,800 bytes exact | About 2.775 vs 2.836 ms; slower |
| Generic material FP16 | Depth/velocity exact, some normals materially changed | About 5.704 vs 5.812 ms; reject |
| Lighting FP16 | Severe HDR/color differences | About 0.266 vs 0.318 ms; reject |
| AO 16x8 / 8x8 / 8x16 tiles | Both eyes exact | All slower than native 16x16 |
| AO loop unroll 2 / 5 / 10 | Hash differences; not numerically resolved | No timing acceptance; parked |

Post-DLSS replay inputs are black in the supplied capture. Zero error in those
postprocessing images is NOT evidence of visual correctness, so the tonemap
FP16 candidate was not accepted. The generic FP16 lowering also introduced
conversion overhead; FP16 alone is not a demonstrated optimization here.

The user's subsequent proposal explicitly allowed testing peripheral VRS and
discussed LoD. Work therefore moved to a separate opt-in native VRS mode; it does
not silently change the earlier exact-quality baseline. See
`docs/fixed-vrs-20260930.md`. Frustum-culling reuse remains disabled, both views
still use native culling, and cascade skipping/geometry LoD have not been changed.
