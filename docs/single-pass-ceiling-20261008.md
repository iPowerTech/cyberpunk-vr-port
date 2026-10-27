# Single-pass ceiling and the stereo CPU critical path, 2026-10-08

Continuation of the single-pass work in `single-pass-input-ownership-20261005.md`.
Before adding another layer to the prepared-pair path, this session measured what
single-pass can gain at all, in the GPU-bound QuickBoot scene and in a dense city
scene, and acted on the largest measured cost. All numbers are same-launch interval
samples of the installed DLLs listed below; nothing here is extrapolated from replay.

## State inherited from the Codex sessions

The prepared-pair path renders the final stage-9 G-buffer tail (167 draws, both eyes)
as view-instanced pairs into private texture arrays, exports them to the native
targets and proves bitwise equality (approved finer common VRS). The native scene
graph can be split into prefix/tail for both eyes (157-node graph, live 22/22 and
23/23). Native draws were never suppressed and no FPS gain was ever measured:
every step so far ADDS recording, snapshots, comparisons and copies.

## GPU ceiling: GeometryCostProbe

`include/Render/GeometryCostProbe.hpp`, wired into the draw/ExecuteIndirect hooks of
`src/Stereo/CommandListCensus.cpp` and the scissor hook of `src/Stereo/Dlss.cpp`.
Control `CyberpunkVR_GeometryCostProbe` (default 0): bits 0..1 action, 1 = forward
with an empty scissor (IA/VS/setup still run, no raster/PS/ROP), 2 = do not forward;
bit 4 MAIN, bit 5 VRCAM; node `CyberpunkVR_GeometryCostNode` (0 = RenderElements
0x23A938). The engine's own scissor is restored after every probed draw.
`CyberpunkVR_GeometryCostHits` proves engagement. Screenshots confirmed side 0 = MAIN
(right eye), side 1 = VRCAM (left eye).

Runner: `run_launch_tests.py --geometry-cost` or `geometry_cost_live.py` +
`geometry_cost_report.py` (7 intervals: baseline, vertex/skip per eye, both vertex,
baseline again; grouped scene GPU profile + frame counter per interval).

| Scene | RenderElements MAIN / VRCAM, ms per frame | vertex-only remainder | baseline FPS |
| --- | --- | --- | --- |
| Badlands QuickBoot save (run 802) | 0.79 / 0.93 | 0.36 / 0.49 (46% / 56%) | 74 |
| Jig-Jig Street roof (run 803) | 0.65 / 0.60 | 0.27 / 0.24 (39% / 51%) | 71 |

A view-instanced pass shares at most the non-pixel part of ONE eye's geometry:
0.25-0.5 ms of a 13-14 ms frame, 2-4% before any overhead of its own. FPS during the
probe intervals is NOT this ceiling: an empty G-buffer also makes deferred lighting
cheaper (sky everywhere), so only the RenderElements range time is valid. Badlands
whole-frame breakdown (ms/frame, MAIN/VRCAM): ApplyDLSS 1.66/2.01, RenderElements
0.79/0.93, RenderShadowCascade 0.17/0.79, VolumetricFog 0.26/0.37, DrawConeAO
0.16/0.46, total 5.9/7.4. Codex had already measured the depth prepass group at
0.073 ms native versus 0.3 ms for its paired version (descriptors doc, run 166-168).

## The city scene is CPU-bound

On the roof above Jig-Jig Street (CET teleport -664.98, 847.75, 75; god mode for the
landing) removing the geometry of one eye lowered GPU usage from 88% to 69-79%
but did not raise FPS. Turning the second view off (bridge `vrcam_enable.txt` = 0,
`second_view_cost.py`) did:

| Run | both views | MAIN only | second-view cost |
| --- | --- | --- | --- |
| 803 | 67.3 FPS | 107.6 FPS | 5.56 ms |
| 804 | 71.5 FPS | 113.2 FPS | 5.15 ms |
| 808 (after the fix below) | 79.1 FPS | 119.8 FPS | 4.30 ms |

So in the city the second view costs 4-5.6 ms of CPU critical path, far more than
any GPU geometry sharing could return. Runtime re-enabling of the VRCAM component
does NOT resume the view (803: component `isEnabled=true`, `Toggle(false/true)` and
`VrcamSel.reload()` all left node hits at 0). The overlay's Disable/Enable VRCAM
button therefore cannot come back without a restart; `second_view_cost.py` runs the
MAIN-only interval last for that reason.

## Where the CPU time goes

`thread_stack_sampler.py` walks live thread stacks with dbghelp StackWalk64 (50-150 Hz,
round-robin over named threads, PDB names for our plugin, .pdata folding for the exe,
raw call sites). `thread_cpu_sample.py` gives per-thread CPU load. `busy_profile.py`
drops idle samples and attributes plugin self time and lock waits.

GameThread (run 804/805) is ~92-96% busy, but ~50-60% of its samples spin in
`sub_14014AB30` = redJobs2 `JobDispatcher::FlushCounter`. In stereo it waits at two
places inside the world frame `sub_140292A54`: the world tick jobs (`0x292BD6`, via
`sub_14026C044`) and, almost only in stereo, the render-frame sync (`0x292C33`, vfunc
`+0x310` of `engine->vfunc_0x450()`). The render job chain is the critical path.

The 15 `redDispatcher` workers were busy 36% of the time. Of their busy samples, 9.2%
were waits on mutexes in OUR hooks, 36 of 68 of them in
`GetOriginalMethod<CopyDescriptors>`: every hooked `CopyDescriptors`,
`CopyDescriptorsSimple`, `ResourceBarrier`, `OMSetRenderTargets`, `Create*View`,
`ExecuteCommandLists` and queue `Signal` took one process-wide `std::mutex` and searched
an `unordered_map`. Threads were asleep in `RtlSleepConditionVariableSRW` under it.

## Fix: lock-free original-method lookup

`include/Hooks/OriginalMethodTable.hpp`: append-only open-addressing table mirrored
from `g_originalVtableMethods`. `PatchVtableMethod` (the only writer, under
`g_vtableMutex`) publishes the map's value before the detour becomes reachable; value
is stored before the key is released. `GetOriginalMethod` probes without a lock and
falls back to the locked map for any key the table could not hold, so capacity never
changes behavior. `CyberpunkVR_FastOriginalLookup` (default 1) selects it; 0 restores
the old locked lookup for same-launch A/B only.

Test `original_method_table` (tools/single_pass_tests): first-original-wins, zero
key/value, capacity/probe overflow, 8 readers racing a publishing writer (never a
wrong value), and a contended microbenchmark: 8x400000 lookups 78.6 ms with
mutex+map versus 4.2 ms published (18.8x).

`control_ab.py` (ABBA x2, 6 s intervals, same launch, city roof):

| Run | lookup 0 (locked) | lookup 1 (lock-free) | delta |
| --- | --- | --- | --- |
| 806 (DLL B1DDFBD5) | 70.7 FPS (69.7-73.1) | 80.3 FPS (80.2-80.5) | -1.71 ms, +13.7% |
| 812 (DLL 8BD25A92) | 72.5 FPS (69.7-74.3) | 78.6 FPS (77.1-81.3) | -1.08 ms, +8.5% |

Both ranges are separated. Lock waits fell from 9.2% to 3.2% of busy worker samples
and the second view's cost from 5.2-5.6 ms to 4.3 ms. This is the first measured
FPS gain on the stereo path; it is a CPU contention fix that affects every player,
not single-pass rendering, and it is not expected to matter in GPU-bound scenes.

Rejected: a lock-free membership mirror for the cascade-reuse recordings
(`cascades::Close/Reset/BeforeBarriers`, ~1.6% of busy samples). A/B 808 first showed
-2.2%, the label-swapped repeat 78.1 vs 77.9 FPS with overlapping ranges: no
measurable effect inside the 75-81 FPS scene variation, so it was reverted.

## CPU single-pass target

`node_cpu_audit.py` (new `CyberpunkVR_ProfDumpRequest` trigger for the existing
per-node profiler; `tools/node_audit_md.py` parses the dump). Run 812, city roof,
stereo, self time per view-frame summed over job threads:

| Node | MAIN ms | VRCAM ms |
| --- | --- | --- |
| RenderElements 0x23A938 | 2.14 | 1.92 |
| SceneDrv (all scene passes) 0x1EC1D0 | 1.18 | 1.17 |
| RenderShadowCascade 0x153844 | 0.00 | 0.86 (shared cascades) |
| Synchronize 0x58DA9C | 0.35 | 0.36 |
| ApplyDLSS 0x37D5C4 | 0.27 | 0.28 |
| PrepareRenderElements 0xC26DB8 | 0.19 | 0.19 |
| all nodes | 7.16 | 7.02 |

Recording the second eye's geometry costs ~3.1 ms of job CPU per frame. A
single-pass that only REPLAYS recorded draws cannot remove it; only not recording
VRCAM's geometry at all (one instanced recording for both eyes into per-eye array
slices, union visibility) can. That needs native target arrays and changed culling,
the architecture the prepared-pair path was meant to justify but never reached.

### Upper bound: VRCAM records no geometry (run 814)

`node_cut_ab.py` applies the existing node-cut table remotely
(`CyberpunkVR_NodeCutRequest`, packed RVA/rtId/mode, applied once per Present by
`CyberpunkVR_NodeCutPoll`; `CyberpunkVR_NodeCutEnable` still gates it, default 0).
Cutting `RenderElements` 0x23A938 for VRCAM only (left eye loses all geometry; the
MAIN image, including the shared cascade shadows, stays intact in the screenshot),
ABBA x2 on the city roof, DLL `E216D597...`:

| Interval | FPS mean (range) | node-cut skips |
| --- | --- | --- |
| A, VRCAM records geometry | 77.1 (75.3-79.6) | 0 |
| B, VRCAM records none | 88.1 (86.7-88.8) | 46-47 thousand per interval |

-1.61 ms, +14.2%, separated. This is the most a perfect geometry single-pass could
return in this CPU-bound scene: it also drops VRCAM's geometry GPU work, while a real
instanced pass would still rasterize and shade the second view. Together with the GPU
ceiling (2-4% in the GPU-bound Badlands scene) it bounds the whole single-pass project:
roughly 3% when GPU-bound and at most ~14% when the render jobs are the critical path.
The lock fix above already returned +8.5-13.7% in the same scene.

The second view's remaining 4.3 ms is therefore mostly NOT geometry recording:
ApplyDLSS, Synchronize, the lighting/post nodes and their GPU work make up the rest.
Cull-reuse modes (`CyberpunkVR_CullReuseMode`, older experiments, some marked unsafe)
remain off and were not evaluated here.

## Other changes

- QuickBoot `focus_window`: when Windows refuses the foreground (user active in
  another window), one synthetic ALT tap then a second `SetForegroundWindow`.
  Launches 809/810/811 failed on that refusal; 812 and 813 passed with it.
  Tests updated and one added (`test_foreground_lock_is_released_by_one_alt_tap`).
- `geometry_cost_live.py` prints the launch helper's stderr.
- Launch 807 crashed while loading at `EXE+0x1DB1EC` (non-canonical RCX, no plugin
  frames). The same site is recorded since 2026-09-30 in fixed-vrs, framegen-dlaa and
  single-pass-descriptors docs; its cause remains unknown. The retry passed.

## Verification

- Local: 222/222 C++/GPU CTest, 1042 Python single-pass, 37 QuickBoot tests.
- Normal launch 813 on DLL 8BD25A92 and normal launch 815 on the final DLL (all
  diagnostics off): 9/9 checks each, no crash/reporter, preview restored, game closed.
  Game, plugin and launcher settings plus the DLL hash equal
  `build/single-pass-20260927/deploy-nodecut-814/manifest.json`; bridge
  `vrcam_enable.txt` is 1.
- Installed DLL `E216D597745724496B1B1F86344F874AB1407DB38F71CED3CC7B40C7FD44B31F`.
- Evidence: `build/single-pass-20261008/` (geometry-cost-802, city-803..814,
  normal-813/815), decompiled callers in `engine_re/cpu_critical_path/`.
- No commit or push was made.

## Next steps

1. Ship the lock-free lookup (default on). It is independent of single-pass.
2. Decide whether the full single-pass architecture is worth its cost: at most ~14%
   in CPU-bound city scenes and ~3% when GPU-bound, and it requires per-eye G-buffer
   targets as slices of one array resource, union visibility for MAIN, view-instanced
   PSOs for every G-buffer shader and skipping VRCAM's geometry recording.
3. The remaining second-view cost (~4.3 ms in the city) is mostly outside geometry:
   profile VRCAM's DLSS/Synchronize/post nodes the same way before choosing.

## Follow-up

`single-pass-stage-replay-20261008.md`: MAIN replays VRCAM's recorded RenderElements stages
with its own camera (12 stages, image parity, zero fallbacks). It saves about 1.9 ms of job
CPU per frame. After the lock fix the city roof is GPU-bound (95-96% busy, Present interval
12.5 ms), so FPS does not change. The GPU breakdown per view and the Present-time split are
in that document.
