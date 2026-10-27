# Single-pass stage replay (2026-10-08)

Continuation of `single-pass-ceiling-20261008.md`. MAIN no longer records its own
RenderElements stages: it re-issues VRCAM's recorded D3D12 commands with its own camera.
It is OFF by default as of 2026-10-19 (`xr_stage_replay=0`). Enabling it replays eleven RenderElements stages. Each of
them passed a frozen-time image check (see "Frozen-time validation") in the city and in an
interior. Stages 10 (deferred decals) and 24 failed that check and are left out: the earlier
13-stage parity claims below were made against moving-scene noise and were too weak. It saves
about 1.5 ms of render-job CPU per frame (2 ms with all 13). At the installed settings (2560x2560
per eye, DLSS Balanced, Transformer) the frame is GPU-bound (96% GPU busy), so FPS does not change.

## Mechanism (`include/Render/StageReplay.hpp`, `src/Render/StageReplay.cpp`)

- **Consumer hook.** `RenderPacketsProbe` (Grading.cpp) routes the packet consumer
  0x1F1208 of RenderElements 0x23A938 through `stage_replay::Consume`. The stage is
  `arguments+0x10`.
- **Capture (source = VRCAM, side 1).** Chained vtable hooks are installed over every
  command-list vtable the plugin knows (`command_list_hook_entries`). They record the
  stage's commands into a `Stream` (32-byte commands plus 8-aligned payload). Slots are
  indexed by stage, partition ordinal and frame parity.
- **Hook lifetime.** The hooks are chained only while a mode or a census is active. With
  mode 0 they are unchained again: a predecessor is put back only where the slot still
  holds our hook. Before, idle chained hooks cost about 4 FPS.
- **Partitions or separate passes.** Several source calls of one stage are either slices of one
  packet array (their spans tile it end to start) or separate passes (stage 24: three calls per
  view in different arrays). `SourcePartitioned` decides per frame. Separate passes pair by
  ordinal: target call k replays source call k, and both views must have the same call count.
- **Replay (target = MAIN, side 0).** For partitions, the first MAIN partition of a frame claims the stage
  (`g_claim`: frame << 2 | in progress/replayed/failed). It acquires all source partitions
  of the same frame, sorted by span address, and replays them in one pass. The other MAIN
  partitions then skip, or fall back natively when the leader failed (`Leader`).
- **Camera (mode 4).** Each source bind of a camera-group root parameter (0 and 18 here)
  stores the context's CPU-table entries of that group (`RecordCameraTable`). Groups come
  from the layout at `state+0x68` and are re-read only when that pointer changes. On
  replay, the source camera descriptor is substituted by the target's
  (`SubstituteCamera`), unique lists are deduplicated, and all of them are copied into
  one reservation from the engine's shader-visible ring (`registry+0x149DE40`, reserve
  0x774558 with the non-waiting nullptr path, code fingerprint `BF311A1C73348164`). The
  context's slice and cursor are never touched.
- **State.** A mirror (`Shadow`) per command list tracks the bound state from
  Reset/ClearState with a generation number. It is also tracked during replay, so
  whatever replay leaves bound is the list's real state for the next stage. After
  replay, `Restore` returns everything the stream touched to a copy taken at entry.
- **Barriers.** Every stage starts with one transition on the view's own per-view buffer
  (COPY_DEST to VERTEX_AND_CONSTANT_BUFFER). With `CyberpunkVR_StageReplayAllowBarriers=1`
  the barrier is not replayed. The target's skipped consumer never records its own
  transition, so the engine's state tracking stays consistent.
- **Fallbacks.** Native recording is used for: no capture, stale frame, overflow, blocking
  ops, foreign list, busy or failed leader, no context or heap, invalid or overflowed
  mirror, camera, capacity, ordinal. Per-reason counters are in
  `CyberpunkVR_StageReplayStats[16+]`; camera diagnostics are in
  `CyberpunkVR_StageReplayCameraDiag[12]`.

### Problems found on the way (runs 817-825)

| Symptom | Cause | Fix |
| --- | --- | --- |
| Mode 3 (raw) stage 6 with four partitions: right eye black | Raw replay binds VRCAM's camera tables, which is only a diagnostic | Mode 4 only |
| Camera fallback on every call (819) | Each native context owns its own layout object, so a pointer comparison never matches | Groups are compared by shape (group, parameter, count, first), not by `start` |
| `NoHeap` on stage 6 (821/822) | Partition lists start without heaps, and the native slice is allocated lazily inside the consumer (heap 0, capacity 0 at entry) | The heap is taken from the capture's `SetDescriptorHeaps`; descriptors come from the engine ring |
| Combined mask: one `NoHeap` per frame (824) | The mirror ignored replayed calls, so a heap bound by one replayed stage was unknown to the next | Track during replay; restore from an entry copy |
| Capture overhead | Each `RootTable` searched 256 commands back for a descriptor copy (old camera path) | Captures skip `Copy` recording and the search; census keeps them |
| Group re-read on every program switch | The layout pointer changes with the program, and the refresh built vectors | Allocation-free read with an 8-entry per-thread cache per layout |
| All 13 stages: `Ordinal`/`Leader` fallbacks on stages 5, 6, 9 in half the frames (831) | Capture slots were capped at 32 by the old private descriptor heap; 13 stages need 36 (stage 6 in four partitions, stage 24 in three calls, two frame parities) | Private heap removed, cap 96, stream memory reserved lazily |

## VRCAM G-buffer partitions

`sub_141D43040` builds the scene graph. It sets the GBuffer_Solid part count to
`min(workers, dword_14347F790)`, but `cmovz edi, ebx` at 0x1D43B37 forces it to 1 when
`view+0xF94 == 1`. That is VRCAM's AA mode, and every other render-to-texture view's (the optics
feed, ...).

Until 2026-10-18 `CyberpunkVR_VrcamGbufferPartitions=1` rewrote the ModRM byte at 0x1D43B39
(FB to FF), which lifted the clamp for all of those views. Now the 15 bytes at 0x1D43B2B
(cmp / mov / cmovz) jump to a trampoline, installed at boot (`[hooks] VrcamGbufferPartitions`).
The trampoline keeps the worker count only when the graph's camera name equals
`CyberpunkVR_VrcamGbufferPartitionsKey`. Every other view keeps the native 1.

The name is the subview's +0x28 hash, the one FlagCompute and RenderParity key on. The builder's only
caller (0x219D4A) passes `subview+0x17D0` as the fourth argument, which sits in r12 for the whole
function (set once at 0x1D4306B), so the name is `[r12-17A8h]`. The poll publishes `g_vrcam_ctx_key`
into the key while the setting is on and 0 while it is off, so toggling is a data store and needs no
code write. State is in `CyberpunkVR_VrcamGbufferPartitionsState`: 0 native, 1 VRCAM partitioned,
2 site not installed. Live check 2026-10-18: hook `ok`, log "follow the worker count (VRCAM only)".

While patched, `stage_replay::GraphSalt()` is XORed into the render-graph cache key at
0x219B32 (RenderParity.cpp). Cached graphs would otherwise hide the patch or keep it
after it is removed. Without the salt, run 817 showed no effect at all.

| | VRCAM stage 6 |
| --- | --- |
| Native (817) | one call, 3.42 ms span |
| Patched (818) | four calls, 1.35 ms span |

The image is unchanged (819 screenshot). A/B in 818 measured 70.3 vs 72.5 FPS (+3.0%, not
separated).

## Validation

`tools/single_pass_tests/stage_replay_sweep.py` runs this loop for each stage:

1. Mode 4 and a screenshot.
2. Native recording and a second screenshot.
3. A per-eye luma difference between the two shots.

VRCAM (left half) never changes, so its difference is the scene's own noise. A stage
passes when MAIN stays within 1.5x of that noise (or +0.5), replay ran, and fewer than 5%
of attempts fell back.

Run 824, stage by stage (mean absolute luma difference, MAIN / VRCAM; every stage replayed
145-161 times in the 2 s window, 0 fallbacks):

| Stage | 3 | 4 | 5 | 6 | 7 | 9 | 10 | 20 | 21 | 22 | 23 | 30 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| MAIN | 1.00 | 1.02 | 1.05 | 1.09 | 1.08 | 1.07 | 1.08 | 1.06 | 0.94 | 0.99 | 1.12 | 1.15 |
| VRCAM | 0.92 | 0.95 | 0.99 | 1.10 | 1.05 | 1.05 | 1.05 | 1.04 | 0.93 | 0.98 | 1.07 | 1.11 |

All twelve stages together (mask `0x40f006f8`):

| Run | Replays | Fallbacks | MAIN / VRCAM difference |
| --- | --- | --- | --- |
| 825 | 1932 | 0 | 0.86 / 0.81 |
| 828 (final DLL) | 1920 | 0 | 1.02 / 0.99 |

Stage 24 (three calls per view, separate passes) pairs by ordinal. Alone (831): 462 replays in
2 s, 0 fallbacks, MAIN 1.09 / VRCAM 1.02. All thirteen stages (832, mask `0x41f006f8`): image
check passed, 3459 paired replays, 18 fallbacks over the whole run.

Culling: VRCAM's camera field of view is 110 degrees and MAIN's is 100, offset by half an
IPD (LodFov.cpp). So VRCAM's frustum covers MAIN's beyond about 0.27 m. LOD selection is
already made equal for both eyes by the FixLod clamp.

## Measurements

### CPU per frame

From the census event timeline (`stage_replay_live.py census`, `cpu_per_frame`). The
values are medians of 116 frames and include census overhead in both modes. CPU columns
are consumer CPU; the last column is the span over both views.

| Run 826, round | Mode | MAIN CPU | VRCAM CPU | Total CPU | Both-view span |
| --- | --- | --- | --- | --- | --- |
| 1 | 0 | 6.62 ms | 6.55 ms | 13.30 ms | 8.16 ms |
| 1 | 4 | 3.30 ms | 8.14 ms | 11.39 ms | 7.87 ms |
| 2 | 0 | 6.57 ms | 6.51 ms | 13.11 ms | 8.15 ms |
| 2 | 4 | 3.13 ms | 8.06 ms | 11.25 ms | 7.99 ms |

- Replay costs about half of MAIN's native recording.
- Capture adds about 1.5 ms to VRCAM: the hooks record every command, and the mirror tracks
  every list.
- Net: -1.9 ms of job CPU per frame, -0.2 to -0.3 ms of recording span.

Run 832, all thirteen stages, with the allocation-free group cache:

| Round | Mode | MAIN CPU | VRCAM CPU | Total CPU | Both-view span |
| --- | --- | --- | --- | --- | --- |
| 1 | 0 | 6.66 ms | 6.75 ms | 13.48 ms | 8.51 ms |
| 1 | 4 | 3.31 ms | 8.28 ms | 11.42 ms | 8.12 ms |
| 2 | 0 | 6.66 ms | 6.66 ms | 13.32 ms | 8.44 ms |
| 2 | 4 | 3.07 ms | 8.31 ms | 11.30 ms | 8.18 ms |

- Net: -2.0 ms of job CPU, -0.3 to -0.4 ms of span.
- Capture costs about 100 ns per recorded command (stage 6: +1.1 ms for 9574 commands).
- The group cache did not change that. Worker stack samples (830) put StageReplay's own code at
  about 1.1% of all worker time (about 2 ms per frame including the replay loop): `Enter`
  0.5% (with the inlined replay loop), `Push` 0.14%, `Record` 0.11%. The rest of the capture
  cost is the hook path itself. Single-buffered warm capture memory measured no cheaper (834:
  VRCAM 7.9-8.1 ms either way), and that variant was removed again.

### Frame time, not FPS

`CyberpunkVR_PresentTiming` (SwapChain.cpp) sums the QPC time per game Present in these
parts: interval, GPU-frame finish plus framegen pacing, inline XR pump, DXGI Present, and
the whole hook. `control_ab.py` now reports these per interval, plus GPU ms, CPU ms, and
worker and game-thread milliseconds per frame. Worker CPU milliseconds per frame are
dominated by job-thread spinning (about 85 ms per frame) and are not a usable cost.

Run 824, all twelve stages, ABBA x3:

| Arm | Present interval | FPS | GPU busy |
| --- | --- | --- | --- |
| A, native | 12.62 ms | 79.7 | 95-96% |
| B, replay | 12.51 ms | 79.8 | 95-96% |

- Waits inside the hook total under 0.3 ms.
- Median GPU frame time is 12.2-12.3 ms in both arms.
- The frame is GPU-bound.

Other checks:

- **Desktop tearing (825).** The 12.5 ms equals three vblanks of the 240 Hz desktop
  monitor, so the swapchain was created with ALLOW_TEARING and sync-interval-0 presents
  used `DXGI_PRESENT_ALLOW_TEARING` (3526 tearing presents counted). Interval: 12.71 vs
  12.70 ms. DWM was not pacing the game, and the change was removed again.
- **1280x1280 per eye (827, temporary `vrport-launcher.ini`, restored to `14BCC070...`).**
  GPU busy 81-83%, interval 11.88 ms native vs 12.02 ms replay (no gain). GameThread
  stacks (12 s, 150 Hz): 47% is spent waiting for the job graph in `exe+14AB30` via
  `sub_1402933F0`, called from `sub_14026C044` and `sub_140245738` (the game-system tick
  phase), while executing stolen render jobs. CET's Lua bridge, used by the test
  tooling, takes about 20% of the GameThread. With the GPU off the critical path, the
  frame is bound by the job graph, not by MAIN's stage recording.
- **DLSS mode switch.** Switching DLSS through CET (`SetIndex(5)`, Ultra Performance)
  changed nothing measurable. The setting was restored to Balanced, and
  `UserSettings.json` equals `21139C86...` again.

### GPU per node and view

Run 825, six grouped scene profiles averaged. Per-frame values divide by once-per-frame
nodes. VRCAM covered 1.2 ranges per frame in those samples, so divide its columns by 1.2
when comparing per pass.

| Node | MAIN ms | VRCAM ms | Notes |
| --- | --- | --- | --- |
| ApplyDLSS | 1.61 | 1.98 | per range 1.61 vs 1.65 |
| RenderElements | 0.61 | 0.63 | |
| ScreenSpaceReflections | 0.50 | 0.69 | |
| RenderLightsIntegrate | 0.37 | 0.43 | |
| VolumetricFog | 0.29 | 0.33 | |
| Present | 0.57 | - | about 8 full-screen passes of 0.09-0.1 ms each (828, per PSO) |
| DrawConeAO | 0.17 | 0.36 | per range 0.17 vs 0.30, the only stable asymmetry |
| RenderShadowCascade | 0.16 | 0.32 | VRCAM renders the cascades; MAIN reuses them |
| ReflectionProbes | 0.01 | 0.10 | done by VRCAM only |
| GlobalIllumination | 0.09 | 0.02 | done by MAIN only |
| Total | 6.16 | 6.63 | |

- Shared work is already split between the views.
- Per-PSO profiles (830) of DrawConeAO, ScreenSpaceReflections, FlattenNormals and
  ApplyDLSS show the same PSOs and command counts in both views. Only the duration of
  identical dispatches differs, in both directions: FlattenNormals 0.153 ms in MAIN vs 0.052
  ms in VRCAM, ConeAO's main pass 0.108 vs 0.154 ms. That is overlap with other GPU work,
  not different settings, so there is nothing to align.
- DLSS is about 30% of the GPU frame.
- Geometry sharing is bounded at about 3%, as before.

### GPU: the port's own passes

`tools/single_pass_tests/port_gpu_stages.py` reads the plugin's GPU stage profiler
(`CyberpunkVR_GpuStageRequest`, src/Render/GpuStageProfile.cpp). Run 834, city roof, mean per
occurrence:

| Stage | us | Notes |
| --- | --- | --- |
| CaptureSecond / SecondBlit | 138 / 134 | eye 1: identity blit of the VRCAM snapshot |
| HudStyle / HudBlur / HudSprites / HudSubmit | 92 / 63 / 35 / 27 | HUD panel (glow pyramid, style) |
| SubmitColor / SubmitDepth | 45 / 34 | per XR frame |
| CaptureDepth / CaptureMain | 41 / 34 | eye 0 is a CopyResource |

Eye 1's fallback blit (no HUD composite) samples the `_UNORM_SRGB` snapshot into a
`_UNORM_SRGB` target of the same size, which is an identity apart from alpha = 1, and the
projection layer ignores alpha. `CyberpunkVR_SecondEyeCopy=1` (new default) replaces it with a
CopyResource when the size and the 8-bit RGBA family match.

- Run 835: SecondBlit 134 to 43 us, CaptureSecond 138 to 50 us, about 0.09 ms GPU per frame.
- Simulator window captures, copy/blit/copy/blit: the difference between modes (0.54-0.56
  mean luma) was the same for both eyes and within the time noise of one mode (0.36-0.69).
  Brightness was unchanged.

Frame-time A/B, run 837 (ABBA x3, Present interval, GPU 93-96% busy):

| Control | A | B | Present interval, mean | Present interval, median |
| --- | --- | --- | --- | --- |
| `CyberpunkVR_SecondEyeCopy` | 0 (blit) | 1 (copy) | 12.763 / 12.760 ms | 12.666 / 12.682 ms |
| `CyberpunkVR_StageReplayMode` (13 stages) | 0 | 4 | 13.237 / 12.980 ms | 12.972 / 12.687 ms |

In both rows the median GPU frame time tracks the interval. Neither difference is outside the
noise of this session (single intervals reached 14.85 ms). The 0.09 ms the copy saves is real in
the stage profile but too small to show in frame time.

The engine's `Present` node (MAIN only, about 8 full-screen passes in the scene profile) is the
swapchain present path (`sub_14021CDC8`: renderer vfuncs +56/+104/+144), and the plugin's
Present hook runs inside it. Its command ranges are the plugin's own work: eye captures, and
the HUD panel's nine-pass blur chain (`cmds 387/43` per frame). They are not extra engine
passes.

The port's passes now total about 0.56 ms per game frame; the remaining ones are features. The
test runtime recommends 2064x2208 per eye, while the launcher renders 2560x2560 (43% more
pixels). That supersampling is a user setting and the largest GPU lever left; DLSS cost scales
with it.

## Default on, frozen-time validation and view descriptors (runs 838-847)

### Default configuration

In the runs below, `xr_stage_replay=1` in `vrport.ini` calls
`CyberpunkVR_StageReplayConfigure`: source VRCAM, mode 4, VRCAM G-buffer partitions, barrier-only
streams allowed, mask `0x40F002F8` (stages 3-7, 9, 20-23, 30). The setting is applied only when it
changes, so unrelated ini edits never reset live test controls. The plugin's settings writer adds
the key, so `vrport.ini` gains one line (`xr_stage_replay=1`, new hash F2316475).

With replay running from startup, the per-list shadow table filled up (838: 491 of 512 slots, 4.5
million lookups missed after a full probe run, 8% of replays refused as `ShadowInvalid`). The
engine creates and destroys command lists, and slots were never freed. Now: 1024 slots, 64 probes,
and a slot whose list has not been Reset for 240 presents is taken over by a new list (census
summary [7] counts takeovers). Result (839, 846): 0 misses, 0 fallbacks in steady state.

### Frozen-time validation

`Game.GetTimeSystem():SetTimeDilation(CName.new('cpvr_freeze'), 0.0001)` stops animation,
crowds and traffic. Then native and replay screenshots differ only by DLSS jitter and video
screens. VRCAM is never replayed, so its half of the image is the noise floor; the check counts
pixels whose luma differs by more than 20 in each half.

| Scene, stage | VRCAM pixels | MAIN pixels | Verdict |
| --- | --- | --- | --- |
| Afterlife (-1455.07, 1010.98, 17.82), stages 3-7, 9, 21, 30 | 4300-5300 | +/-220 of VRCAM | clean |
| Afterlife, stage 10 | 5016 | 8146 | decals missing in MAIN |
| Afterlife, stage 24 (camera only) | 4814 | 5764 | ceiling and particle glow differ |
| City roof, stages 3-7, 9, 20-23, 30 | 120-400 | -4 to +24 of VRCAM | clean |
| City roof, stage 24 | 279 | 32484 | distant city hazed, hologram colour changed |
| Default set, roof / Afterlife (846) | 331 / 5174 | +23 / +186 | clean |

The moving-scene checks of runs 824-832 had passed stages 10 and 24: the city's own motion hid
them.

### View descriptors (stage_table_diff.py)

`CyberpunkVR_StageReplayTableDump=1` makes census streams carry, for every root-table bind, the
descriptors of the context's CPU table behind it. The layout's slot maps decode them
(`sub_1401F3978`): two bytes per slot at layout+0x4C3 + stage*0x100 (SRV), +0x8C3 + stage*0x26
(UAV), +0x453 + stage*0x1C (CBV) and +0x95B + stage*0x20 (sampler); the first byte is the group,
and CPU index = group start + slot - group first. Matching MAIN's draws to VRCAM's (stage 10, 841)
shows these per-view entries:

| Slot (vertex and pixel stage) | Root parameters | Scope | Replay |
| --- | --- | --- | --- |
| CBV 1, camera | 0[1], 18[1] | view | substituted (before) |
| CBV 0, view buffer | 0[0], 18[0] | view | substituted (843); fixed stage 24 in Afterlife |
| SRV 37, view texture | 7[5], 25[5] | view | substituted (844); null-descriptor guarded |
| CBV 3, 5, 7, pass buffers | 0[3], 18[3], 2[0], 3[0], 21[0] | render context (+0x80, +0x88, +0x78) | must stay the source's |

- Substituting the pass buffers too (842) broke every draw of stage 3. They describe the source's
  packets: `startInstance` differs between the views in 25% (stage 3) and 55% (stage 10) of the
  matched draws, so each view writes its own instance ranges.
- With camera, view buffer and view texture substituted (845), the replay reads exactly the
  values native MAIN binds (`CyberpunkVR_StageReplayViewDiag` against the census). Decals still
  vanish, so their per-view data is in the instance data itself, which replay cannot recreate.
- Stage 24 on the roof also stays wrong with all three substituted.

`ReadView` reads the view descriptors at consumer entry for every capture and every replay
target (`ReadBound`, layout slot maps). A table that holds any of the source's is recorded with
its contents and rebuilt with the target's (`SubstituteView`). A table with a source descriptor
the target lacks falls back to native.

### CPU with the default set (846)

Census timeline, 116 frames per round, consumer CPU:

| Round | Mode | MAIN | VRCAM | Total | Both-view span |
| --- | --- | --- | --- | --- | --- |
| 1 | 0 | 6.77 ms | 6.71 ms | 13.45 ms | 8.38 ms |
| 1 | 4 | 3.94 ms | 8.14 ms | 12.00 ms | 8.06 ms |
| 2 | 0 | 6.83 ms | 6.71 ms | 13.52 ms | 8.34 ms |
| 2 | 4 | 3.90 ms | 8.12 ms | 11.93 ms | 8.05 ms |

-1.5 ms of render-job CPU and -0.3 ms of recording span per frame. Present-interval A/B (ABBA x3)
in the same session: medians 13.64 / 13.26 ms, GPU-bound, within noise.

## Controls

| Export | Meaning |
| --- | --- |
| `CyberpunkVR_StageReplayMode` | 0 off (hooks unchained), 1 census only, 2 skip target, 3 raw replay (diagnostic), 4 replay with target camera |
| `xr_stage_replay` (vrport.ini) | 0 (default) = off; 1 = the configuration below |
| `CyberpunkVR_StageReplayStages` | u64 stage mask; `0` while disabled, `0x40F002F8` (11 stages) when enabled. `0x41f006f8` replays all 13 but breaks decals (10) and stage 24 |
| `CyberpunkVR_StageReplayTableDump` | 1 = census streams carry table contents (diagnostic) |
| `CyberpunkVR_StageStreamContextDump` / `...StateDump` / `...ContextAddr` | census stream's render context and binding state after the call |
| `CyberpunkVR_StageReplayViewDiag[2][8]` | last view descriptors read for a capture and a replay target |
| `CyberpunkVR_StageReplaySource` | 1 = VRCAM is the source |
| `CyberpunkVR_StageReplayAllowBarriers` | 1 = replay streams whose only blocking ops are barriers |
| `CyberpunkVR_StageReplayWaitUs` | bounded wait for source partitions |
| `CyberpunkVR_VrcamGbufferPartitions` | 1 = patch 0x1D43B39 (VRCAM GBuffer parts follow the worker count) |
| `CyberpunkVR_StageCensusRequest` / `...Rows` / `...Ops` / `...Events` | census window, per stage rows, op counts, event timeline |
| `CyberpunkVR_PresentTiming[8]` | Present time split (diagnostic, always on, a few QPC reads) |
| `CyberpunkVR_SecondEyeCopy` | 1 (default) = eye 1 CopyResource when nothing is composited; 0 = the old blit |

## Tools and tests

New tools:

- `tools/single_pass_tests/stage_replay_live.py`, with subcommands `census`, `set`,
  `stats`, `shot`. It decodes events, barrier payloads, order per stage and
  `cpu_per_frame`.
- `tools/single_pass_tests/stage_replay_sweep.py`: per-stage and combined image checks.
- `tools/single_pass_tests/control_ab.py`: Present timing and frame-time costs.
- `tools/single_pass_tests/port_gpu_stages.py`: GPU time of the port's own passes.
- `tools/single_pass_tests/stage_table_diff.py`: per-view table entries of a stage, and where
  each lives in the render context or binding state.

Tests: `stage_replay_test.cpp`, `stage_replay_live_test.py`, `stage_replay_sweep_test.py`,
`cpu_ceiling_tools_test.py` (Present timing split, frame-time costs at flat FPS),
`port_gpu_stages_test.py` and `stage_table_diff_test.py`. `stage_replay_test.cpp` covers
`SubstituteView`.

## Verification

- **Local tests.** 223/223 CTest, 1065 Python single-pass, 37 QuickBoot.
- **Normal launches.** 829 (DLL E419F0B2...), 833 (DLL 113DF7C3...), 836 (DLL E289F61B...) and
  847 (final DLL, replay on by default): 9/9 checks each, no crash or reporter, preview restored.
- **Settings.** Game, plugin and launcher settings hashes equal the 814 manifest, except
  `vrport.ini`, which gained `xr_stage_replay=1`:
  - `vrport.ini` F2316475 (was A2A457FD)
  - `vrport-launcher.ini` 14BCC070
  - plugin `UserSettings.json` 9750F55B
  - game `UserSettings.json` 21139C86
- **Installed DLL.** `D00CB558F4CF434A6278DDE07BD130D1EECEF082137102B04A81D2B821924007`.
- **Evidence.** `build/single-pass-20261008/city-816..846`, `normal-829`, `normal-833`, `normal-836`,
  `normal-847`, and
  decompiled job-wait callers in `engine_re/cpu_critical_path/`.
- No commit or push was made.

## Conclusions and next steps

1. Stage replay is on by default for the eleven stages that pass the frozen-time check. It
   removes 1.5 ms of render-job CPU per frame; FPS does not change while the GPU or the job graph
   bounds the frame. Its own cost (VRCAM capture about +1.4 ms, replay about 57% of native) is
   what is left to cut: issuing replayed calls below the plugin's own hooks, and tracking
   mirrors only for lists that reach a target stage.
2. Replay is bounded by what the source records: per-view instance data (decals) and the
   source's visibility (MAIN drew 1-2% more objects than VRCAM in stages 6 and 24 of the
   interior) cannot be recreated. The view-instanced single-pass of the Codex sessions meets
   the same two problems (per-view bindings beyond the camera, union visibility).
3. New checks of a stage need frozen time; moving-scene noise hides real defects.
4. Frame-time gains at the installed settings must come from the GPU: DLSS (about 3.2 ms
   for both eyes), SSR, lighting and fog, plus MAIN's Present (about 0.6 ms). Only the
   geometry share (about 3%) is a single-pass problem. The per-view asymmetries are GPU
   overlap, not settings. The engine applies no lens visibility mask; one would save only
   G-buffer pixel work (about 0.1 ms per eye), since the heavy passes are compute.
