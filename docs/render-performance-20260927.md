# RenderDoc series and original-method lookup

Checkpoint before this work: `d3a9aea6`, committed without a co-author trailer.
Installed baseline DLL: `4C04C0FFA7F8454884F5727E94076B30D695C819B801B0691A5DED4AC87E0676`.

## Capture analysis

The user supplied ten consecutive captures, `10frames_10319.rdc` through
`10frames_10328.rdc`, under the game's `render-doc-capture` directory. All were
processed with the game closed. Replays were opened sequentially and shut down
after each file; the RenderDoc MCP controller is also closed.

The MCP performance helper has two relevant limitations in this installation:
it labels unconverted EventGPUDuration seconds as milliseconds, and its pass
summary excludes dispatches. It can also substitute triangle counts for missing
timings despite reporting real timing globally. Its output was not used as a
quantitative performance result.

`build/render-perf-20260927/profile_capture.py` requests the exact built-in GPU
duration counter, verifies its seconds/64-bit result description, and converts
to milliseconds without heuristic fallback. Node markers are associated with
their actual command lists. Virtual ExecuteIndirect children inherit ownership
from the action-tree parent. Timings are retained for draw, dispatch and other
GPU actions. The second metadata pass corrected indirect ownership using the
existing counter results rather than collecting new timings.

Full per-event data and ten-frame aggregation are in `frame-*.json` and
`summary.json` in that build directory. Event timings on replay are not live
frame duration; summing them includes instrumentation effects and separately
executed work. The simulator's repeated readbacks and the port's XR submissions
are separate command lists, not an engine lighting/geometry cost.

The dominant geometry work is nearly symmetric: median RenderElements event
sums are about 9.74 ms MAIN and 9.60 ms VRCAM on replay, with 1487 and 1506 draws.
These are ranking data, not a claim that either eye takes that long in gameplay.
The largest geometry categories are GBufferVelocity_Discard, Transparents and
GBuffer_Solid. Most corresponding post-processing nodes have similar counts
and durations. No shader/pass removal or shadow-reuse change is justified by
these data alone. Simulator behavior and quality settings remain unchanged.

## Next CPU candidate

The draw/command density also reaches the depth/swapchain hook family.
`GetOriginalMethod` took one shared mutex and searched an unordered_map for
every barrier, target binding, queue submission and signal. Repository-wide
inspection found exactly one writer: PatchVtableMethod inserts an original only
if absent, before exposing its detour. Entries are never erased or replaced.

A four-entry thread-local cache now retains successful lookups by exact vtable
and slot, independently for each function type. Misses still use the locked
registry and are not cached, so later hook installation remains visible. The
registry's append-only contract is documented at publication. No command,
resource reference, fence, barrier or original function argument is changed.
`CyberpunkVR_FastOriginalMethodLookup` defaults to 1 and permits a live A/B;
0 executes the original locked lookup.

Release build and `tools/original_method_tests` passed. Tests cover negative
misses followed by publication, vtable/slot distinction, cache eviction, six
argument/result forwarding, successful cache lookup while the registry lock is
held, legacy lock behavior, and eight concurrent readers during late publication.

Candidate DLL:
`6173307D41F2A91781A00176E92D4EFB214B6C775F5E7CDE816A4D129EA032CE`.
Live performance/functional comparison is still required; do not claim an FPS
gain from the source change or micro-tests alone.

Installed candidate 6173307D with the game closed; baseline DLL/PDB preserved in
`build/render-perf-20260927/deploy-lookup/previous`. User confirmed gameplay in
PID 24352. First same-process off/on/on/off/on series: 81.586 /82.710 /82.589 /
80.590 /81.497 FPS. Mean 81.088 off versus 82.265 on, with overlapping ranges;
this is modest preliminary evidence, not a large gain. All samples kept FG,
FPS telemetry and broad diagnostics off. Foreground remained outside the game
(Firefox, with brief Codex focus in the first two windows). Cache restored on.

Framegen smoke test after late GPU-object creation succeeded: 45 real +45
generated =90 output FPS, 459 inputs for each eye, 437 generated frames,
zero skipped frames and zero current repeats. FG, telemetry and diagnostics were
restored off and verified. A confirmation A/B is in progress.

The latest crash report (13:43:56, PID 11320) belongs to the earlier failed
Nsight launch, not this candidate: engine watchdog timeout after 120 seconds.
The report shows WarpViz.Injection.dll loaded. This establishes a startup hang,
not a new heap/depth failure or a proven underlying cause. No new report from
PID 24352 has appeared.

Confirmation series on/off/off/on/off/on: 83.413 /83.554 /81.230 /80.157 /
81.277 /81.917 FPS, mean 81.829 on versus 82.020 off. The initial advantage did
not reproduce. The original-method cache was withdrawn from source, including
its header/tests/A-B symbol, rather than claimed as an FPS improvement.
Experiment source and patch are preserved under build/render-perf-20260927.

## Unused queue signal tracking

Repository-wide search found no caller of CyberpunkVRPort_WaitOnAllGameSignals,
but the old Signal hook still ran on every queue signal, took a shared mutex,
queried the clock and retained the last fence. Normal rendering no longer
installs or maintains that unused tracker. CyberpunkVR_LegacyQueueSignalTracking
defaults to 0 and enables the old observer for an explicit A/B. After an A/B
installed the hook, disabling it forwards the native Signal unchanged and
clears all tracker-owned fence references. Inline depth capture, writer-queue
discovery, command-resource retention and the actual frame fences are unchanged.

Release build and the generated WARP fixture passed after cache withdrawal.
The fixture extracts the actual Signal/cleanup functions and covers all call
arguments/results, disabled tracking, replacing/retiring fence references,
repeated signals and failed native Signal. Current candidate SHA-256:
70E2EF8ED08C96717D16C0734DB28C19464979BDED40F33EA51BAC9E85793611.
This candidate has not yet been evaluated in gameplay.

PID 2396 loaded 70E2EF8E successfully. Runtime debug/census/probe switches were
all confirmed zero. Tracker off/on/off/on/on/off gave 82.10 /80.78 /79.38 /
79.11 /79.29 /82.92 FPS; means 81.47 off, 79.73 on, overlapping ranges and the
last window changed foreground from Firefox to Codex. This is preliminary,
not isolated proof of a 1.74 FPS improvement. The tracker retained fences only
while enabled and cleared them on every disable. It is now off.

Bounded GPU profiling verified both depth capture and depth submission still
execute, with 32 completed batches, no cancelled/invalid samples. Framegen smoke
test gave 45+45=90 FPS and 457 MV/depth inputs per eye. FG, metrics and broad
diagnostics were restored off. No new report from PID 2396.

## HUD submission serialization

Read-only sampling of the XR submission thread 4844 in PID 2396 found 213/240
samples blocked in Quad::Prepare's per-panel fence wait: 146 for the main HUD,
67 for the interaction HUD. This is independent of the simulator preview code.
The two sampled engine workers mostly waited on the native job dispatcher and
had no comparable hot port-side lock. Source confirms each HUD channel used
one allocator/list and waited for its previous GPU copy before progressing the
entire XR frame loop.

Quad now has three fenced copy slots. Only a completed slot can reset its
allocator/list; its exact source Frame remains retained until that slot's fence
completes. A full ring or XR image timeout keeps the last released image and its
published masking/loot metadata, while continuing the pose update. Image waits
are nonblocking in this mode. Unready acquisitions are retained and correctly
waited/released before resize or shutdown. Failed queue signals stop reuse.
No extra HUD textures are allocated; only two additional allocators/lists per
active channel. CyberpunkVR_HudSubmitRing=0 retains the old wait policy for A/B;
default 1 enables the ring.

Release build and tools/hud_submit_tests passed with WARP/debug validation:
GPU-delayed three-slot recording, fourth-frame fallback, retained source
lifetimes, metadata matched to the displayed image, same-serial reuse, XR
timeout/resume, disable/re-enable, pending-acquisition resize and legacy switch.
Candidate A1305241A596096CA209A9687D58182B2FBFE1E14E75A53D2DE3B1F4D2DECDBB
awaits live validation. Measure XR submission cadence separately from game FPS;
eliminating this wait need not increase game FPS on an already busy GPU.

An initial PrintWindow capture in PID 11876 was black in ring mode, prompting a
temporary live rollback to 0. This was not reproduced by the simulator's native
GPU-readback screenshot path: hud-native-on-11876.png and hud-native-off-11876.png
both contain stereo scenery and the HUD, with nearly identical pixel means.
Projection logs continuously advance in both modes, with matching 2560x2560
rectangles and FOVs for both eyes. Therefore hud-ring-11876.png is an unreliable
window-capture result, not evidence of a broken submitted image.

runtime_status counts all end-frame calls, so its rate alone never establishes
visible delivery. Native captures and the projection log are required as well.
Ring mode submits approximately 90 projections/s versus 45-50 with the old HUD
wait. Game FPS in the first A/B was about 75-79 versus 83-84 respectively; this is
NOT a game-FPS improvement. The runtime now composites more frames with the
unchanged simulator preview settings. Burst/Framegen checks remain to be done.

Follow-up validation: native burst captured 32 nonblack stereo/HUD composites
(hud-burst-11876), and a Framegen smoke test reported 45 real +45 generated =90
output FPS, inputs [462,461], zero skipped frames. FG, metrics and diagnostics
were restored off. A 32-batch GPU-stage probe completed with zero cancelled or
invalid batches and both depth capture/submission present. Per-copy GPU times
are tens of microseconds; the old HUD wait serialized unrelated earlier queue
work, not just the small copy itself.

XR thread 3328, 180 read-only samples: 157 in xrWaitFrame, 10 in the simulator
StretchDIBits path, none in Quad::Prepare's fence wait. This verifies removal of
the port's accidental HUD serialization, without changing preview settings.
An 8-second game sample after validation measured 77.86 FPS, 89.55 end-frame/s
and 94.95% NVML GPU utilization; these are different quantities. Native preview
burst paint intervals averaged 16.61 ms (maximum 47 ms), so do not describe the
90 submissions as 90 unique game frames or 90 painted preview frames.

Opening and closing the native settings panel also passed the simulator's
native screenshot check with the HUD ring enabled. The PrintWindow capture
helper should not be used as visual evidence for this GDI preview path.

## Disabled belt props

Main-thread sampling in PID 11876 found 87/400 samples inside CET callbacks,
including 25 in TacticalBelt. Source inspection identified seven disabled empty
prop slots being queried through GetItemInSlot every frame: the existing
half-second cache applied only to non-null results. Those empty slots also
resolved the selected grenade and rendering plane despite having no object.

Successful empty lookups for disabled slots now use the existing half-second
refresh period, keyed by slot name. Enabling a slot bypasses the negative cache
immediately; failures and explicit invalidations retry. Occupied/active slots
keep the prior cadence. Item/plane work is skipped only for empty disabled slots
while the CET debug panel is closed. Cleanup for an occupied disabled slot and
live debug item labels still use the original path. An internal
performance.legacyEmptyPropPoll switch restores the old behavior for live A/B.

tools/belt_tests/idle_props.lua executed in CET's LuaJIT with lexical mocks:
43 cases passed, including enabling, changed slot name, failed lookup retry,
explicit invalidation, active/occupied cleanup and legacy/debug-panel parity.
Seven disabled slots over 200 updates made 28 lookups versus 1,400 originally.
The complete candidate module compiled successfully; no live module or native
game object was used by these tests. Live FPS comparison is still pending.

Installed Lua SHA-256 1C6A1D4479310EBA8D54628A4CC6B574C0FBEADD8553738CA7C1DAAAC8D40CCE,
with DLL A1305241 unchanged and the complete vrport.ini hash unchanged. PID 16428
loaded successfully. Same-process new/old/old/new/new/old measurements gave
76.589/77.197/76.277/76.823/76.645/76.501 FPS: means 76.686 new, 76.658 old.
There is no measurable FPS benefit in this GPU-heavy scene. All windows kept
ring=1, FG=0, broad diagnostics=0, closed ImGui with fence advancement=0, GPU
94.84-95.00%, XR submissions about 89.5/s. The new polling path remains enabled;
it removes unnecessary API calls, not a proven FPS bottleneck. No new crash.

The user redirected further work toward single-pass instanced stereo. The
proposed extra engine GPU timing instrumentation was not implemented. Continue
at the geometry/shader/framegraph level, preserving these validated changes.
