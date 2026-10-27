# Empty ImGui submissions and CET bridge overhead

Following the Reflex improvement, profile the new bottleneck rather than
repeating measurements with Reflex's sleep active. Baseline PID10752 used
Reflex Off, FG Off, the simulator and DLL A1ABA3CC. No graphics setting changed.

## Measured work

The game produced67.53FPS. GPU utilization averaged88.46% at2782MHz. The main
thread22252 consumed97.14% of one logical CPU. Native stack samples still show
job-counter waits and substantial CET work, without the earlier Reflex sleep.
The measured windows had ChatGPT PID12228 in the foreground; they are background
comparisons. Per-node CPU totals overlap and must not be added as frame time.

CETBridge opened its sandboxed command.json path on every game update, ignoring
its poll_interval setting. A temporary10Hz poll gave72.59FPS and93.97% GPU;
restoring the original poll gave70.02FPS and92.59% GPU. Individual polls averaged
1.6ms. This supports removing unnecessary polling, but scene/run variation means
the first67.5-to72.6 change should not be presented as a guaranteed5FPS gain.

The installed development bridge now honors poll_interval=0.1 using wall time,
including paused gameplay. Heartbeat and TCP callbacks are unchanged. It was
applied live and command/heartbeat delivery verified. Backups of bridge.lua and
config.lua are in build/performance-next-20260927/CETBridge-before. This is a
local MCP tooling optimization, not a component shipped by the VR port.

With that overhead reduced, read-only sampling of the actual ImGui context
found898/900 valid empty draw-data frames (the other2 were mid-update and also
empty). Yet its fence advanced684 times in9.6 seconds. The last wait duration
changed412 times, averaging2.05ms and peaking4.96ms. This is a lower bound on
wait occurrences, not an exact per-frame average: a10ms sampler can miss waits.
Layout offsets were compiled from this build's imgui_internal.h before reading.

## Implementation

The closed overlay still runs its CPU NewFrame/Render and barrel-dot publication
so stale menu geometry and old weapon dots are cleared. When there is no canvas
and no draw list, it submits no command list, performs no target transitions,
and does not wait for the preceding overlay fence. A real draw still waits
before resetting its allocator or uploading ImGui vertices. The independent
load/resize guard still drains while active, including empty overlay frames.

Settings snapshots are fetched only when a panel or placement save needs them.
A failed/skipped submission cannot replay stale data into the desktop or the
second eye. Callback-only lists remain work even with zero vertices. All new
counters and timing accumulation require the existing runtime diagnostic gate.
CyberpunkVR_OverlaySkipEmpty is an internal live A/B switch, default1.

This refines the experiment previously withdrawn during the startup crash
investigation. That crash was later traced to depth staging lifetime, whose fix
is retained. Current evidence specifically shows the empty-overlay waits with
Reflex Off, rather than attributing stale wait telemetry to current frames.

## Validation

All16 overlay tests passed, including the D3D12 fixture, pointer routing,
menu/desktop transitions and120 empty/dot/callback/menu draw-data transitions.
Release build succeeded. Installed and hash-verified:
B5D806AA3F38562C271FB3E22A2293B1D2ADAB0C59955700911991065BDA4E68.
Previous DLL/PDB and unchanged vrport.ini are archived in
build/performance-next-20260927/deploy-empty-overlay. Runtime comparison and
menu verification follow the user's next launch. No commit has been made.

## Installed runtime comparison: PID23148

With10Hz bridge polling, Reflex Off and FG Off, the internal A/B switch gave:

| Empty submission skipping | FPS | GPU | Overlay fence advances /8s |
| --- | ---: | ---: | ---: |
| Off | 73.58 | 94.39% | 589 |
| On | 78.07 | 94.46% | 0 |
| On | 77.70 | 95.00% | 0 |
| Off | 71.73 | 93.32% | 576 |
| On | 77.58 | 94.93% | 0 |

All windows stayed in gameplay with the panel closed and the same foreground
process (ChatGPT12228). The switch was restored to1 and profiling disabled.
The gain is about7% in this scene, with no empty overlay GPU submissions.
Raw samples and summaries: build/performance-next-20260927/overlay-abba*.json.
User menu/pointer feedback remains pending; automated menu tests passed.

## Hidden HUD source updates

The scanner, scanner details, subtitle root and two phone roots were hidden in
the current scene, but Lua still queried their layout and invalidated their
windows every frame. The guarded path checks native source visibility/opacity
each tick, skips geometry for hidden roots and clears their old image once on
hide/first observation. Visible sources retain per-frame bounds and paint.

Controlled6-second windows in the same candidate module, old/cull/cull/old/cull:
252ms/456 calls,136ms/465,142ms/462,227ms/462,136ms/469. This is roughly0.52ms to
0.30ms per HUD update, not evidence of a large independent FPS increase. The
final version also explicitly invalidates on the transition to hidden, avoiding
a stale exported texture. That transition is covered by the fixtures.

Pure Lua tests ran inside CET's VM with lexical mock Game/widget bindings; they
did not alter live game widgets. Phone, subtitle and scanner fixtures cover
hidden idle (no repeated geometry/redraw), first show (same crop as original),
hide clearing, opacity fade and controller rescanning. All passed. The source
was copied to the installed Stereo module and applied live without restarting;
no HUD errors and27 published sprites. The previous source is archived as
build/performance-next-20260927/hud-panel-before.lua.

Three further native VROverlayToggle open/close cycles succeeded in PID23148.
The panel reached active/panel each time and returned to idle after closing.
Desktop render inspected in overlay-visible.png; no stale menu after closure.
Pointer usability feedback requested from the user remains pending.

## Next profiling build

Build7DDFE7A667809D6B8BCFE98EE43E24075765BB500687A521A001056F31F6581B
adds a one-shot, runtime-debug-gated VRReflexTiming(request) native. A request
is queued by the script thread; the serialized native SetOptions render path
collects Streamline ReflexState v1 afterward. This avoids concurrently invoking
the SDK's non-thread-safe GetState from CET. Reports preserve raw64-frame driver
timestamps; no queries, threads or GPU allocations run without an explicit
diagnostic request. The public v1 structure ABI has compile-time size/offset
checks and its initialization is covered by reflex_options tests.

VRIK's0.25-second mouse-Y settings poll now calls GetVRMouseYDisabled directly
instead of opening a sandboxed ini path. Older DLLs retain the original file
fallback. No input/camera policy or polling cadence changes. Release build and
Reflex tests passed; DLL/PDB and the VRIK script installed. Backups in
build/performance-next-20260927/deploy-timing. The user was asked to restart;
timing API and native getter still require live validation in that next run.

PID25572 validated the timing API: gate off returns diagnostics_disabled; one
gated request returned result0/available=true with64 native frame reports. The
gate was restored off. GetVRMouseYDisabled returned true, matching the setting;
HUD loaded without errors and bridge polling remained0.1s. A normal12s sample
gave73.57FPS/94.10% GPU, with Firefox8216 foreground (different conditions from
PID23148; do not interpret this cross-launch difference as a regression).
The full timing window was partly affected by enabling the broad diagnostic
gate. Its first16 reports average13.365ms/frame and10.81ms active GPU time;
later intervals include diagnostic disturbance. Raw: gpu-latency-25572.json.

## Expensive empty body-component scans

To attribute CET work without new hooks,300 short GameThread21848 snapshots
were unwound offline. Maximum thread suspension was0.805ms, resumed in finally
in each sample. CET's ScriptStore update loop at RVA2E9100 inlines ScriptContext:
its current context is RSI+28h, or R14+30h for the overflow container. The
logger argument at context+160h confirms the name string at+140h. Only strings
matching installed mod directory names were accepted.159/300 samples were in
Lua updates: Basketball55, HandCollision51, TacticalBelt18, Stereo12, CETBridge7,
others15 and one unresolved. Source references:
https://github.com/maximegmd/CyberEngineTweaks/blob/master/src/scripting/ScriptContext.h
https://github.com/maximegmd/CyberEngineTweaks/blob/master/src/scripting/ScriptContext.cpp

The current player has333 components and zero VRPortBody_ names. Both capsule
maintenance routines leave their cache nil when no matching capsule exists,
so each repeats the entire Lua name conversion scan every frame. This is
independent of whether a basketball is spawned; the query safety pass precedes
that check. HandCollision also reaches this scan while COLL_ON is false.

VRShouldScanBodyCapsules now obtains the same native GetComponents snapshot,
checks names in C++, and returns false only for a confirmed empty result.
Lookup/execution failure returns true to retain the original Lua scan. There
is no negative-result TTL: newly attached components are still detected every
tick. Positive cases use the existing Lua toggle/query logic unchanged, and
older DLLs fall back to the original scan. The diagnostic A/B variable
CyberpunkVR_BodyCapsulePrefilter=0 forces that original path; default1 enables
the prefilter. No physical-body handles or component references are cached by
the new native function.

Four pure-Lua fixture cases exercise both real maintenance helpers with and
without the native predicate: absent components, first-tick appearance on a
replacement player, preserved maintenance cadence and old-DLL fallback. Passed.
Build1CCDEFD05A09E243D2A7C6A0A8B432F16DABF51883696F4A59AAF7FF46B0FE2E
was installed with updated Basketball and HandCollision scripts. Backups in
deploy-capsule-prefilter. The user was asked to launch for native equivalence,
A/B throughput and functional checks. These remain the next required steps.

### First prefilter candidate withdrawn

PID18648 crashed while loading the save: report20260927-113828-18648-18580.
CPU AV at EXE144C74 in the heap free-list removal used by allocation. The node's
links contain noncanonical floating-point-looking data. Real offline unwind:
144C74 ->1449AD ->1448C9 ->141EF3 ->AAD32C ->14BAA2 ->4EC2A3 ->4ECB4C ->
4ED61D ->733901 -> job worker. The worker was allocating a48-byte-element
geometry array. Main thread19268 was also in allocation. No prefilter function
is on either current stack; the dump identifies heap damage, not its writer.
It does not establish a GPU/depth failure or conclusively prove the new call
caused the corruption.

The exact previous7DDFE7A DLL/PDB and both previous Lua scripts were restored;
the user confirmed the same save loads. The new nested ExecuteFunction/
GetComponents array-return path is withdrawn, rather than hidden by exception
handling. Codeware's published EntityEx::GetComponents implementation simply
returns a copy of Entity::components:
https://github.com/psiberx/cp2077-codeware/blob/main/src/App/Entity/EntityEx.hpp

The revised prefilter reads that array directly on the CET game thread, only
when the entity is Attached and the game is outside menu/loading. It performs
no array allocation/copy/free and retains no component pointers. During load
it returns true to keep the established Lua path. It is being built; repeated
load and live equivalence/A-B validation remain necessary before acceptance.

Read-only candidate3F1698120FC6AB574F27B92A2D5257F4CB718B99CEE889DE770657E1AFBD58ED
installed with the two prefilter callers. Known-good control PID13148 was closed
for deployment. Previous7DDFE7A DLL/PDB archived in deploy-capsule-readonly.
User asked to load the same save and reload it from gameplay. No claim that the
corruption writer was conclusively found; this candidate removes the new array
return/ownership path and bypasses prefiltering during loading entirely.

PID18836: user confirmed initial and repeated gameplay save loads both passed.
Live Lua full scan reported333 components/zero matches; native predicate agreed
(false), and nil entity also returned false. Same-process A/B, native on/off:
73.19 off /82.53 on /82.78 on /73.71 off /82.68 on FPS. All samples kept Reflex
Off, FG Off, ImGui closed, identical foreground process Firefox8216, and zero
overlay fence advances. GPU usage reached95% in enabled windows. The switch
was restored to1. No new crash report since the withdrawn first candidate.

Functional basketball smoke test: temporary ball spawned from previously absent
state, fell fromZ134.079 to133.111, recorded1 contact with vIn4.36/vOut1.49 and
valid hand-bone publication. Despawned in finally and previous correction state
restored. This verifies the fast empty-body scan does not suppress VRBallTick.
Physical grabbing/throwing and actual positive body-capsule scenes were not
claimed from this stationary smoke test. Raw A/B data: capsules-abba*.json.

## Render overhead follow-up

After the capsule filter, 300 GameThread samples contained 59 Lua callbacks;
Basketball and HandCollision accounted for one each, TacticalBelt for 22.
Two render workers also sampled repeated mutex acquisition in
viewdata_fill_holes and VirtualQuery in the old NGX ProcessTag observer.
The native EXE+21C8A2 wait is an allocator/backbuffer fence wait; it must not
be removed. The capture path already rotates three allocators.

Build B093BFA85AFD643B864A8252E5DF0938E41BBF81E0D68360EF71447DE3CBDAC7:

- Gate the obsolete single-view NGX diagnostic snapshots, including resource
  probing and retained references. Framegen's real RecordTags observer and the
  original Streamline call always receive the original arguments. Turning the
  gate off releases the diagnostic resources on the next tag call.
- Copy the owned MAIN viewData staging block once under the mutex per dispatch,
  then apply the unchanged ranges and pointer filters. No cross-frame cache or
  changes to shadow/fog/light policies. Internal A/B controls are
  CyberpunkVR_NgxLegacyCapture (default 0) and
  CyberpunkVR_ViewDataBatchSnapshot (default 1).

Release build and ngx_tests passed. The NGX test uses the real hook and WARP
resources: all-argument/result forwarding, 32 diagnostic/override transitions,
release of both retained resources, and a disabled-path unreadable-tag check.
A generated fixture using the actual ViewReuse tables/function passed 4096
byte-for-byte legacy/batch cases and 4096 concurrent-publication cases.
Fixture generator: build/performance-next-20260927/make_viewdata_fixture.py.

PID 18836 was closed and the build installed; prior DLL/PDB are archived under
deploy-render-overhead/previous. User confirmed gameplay in PID 5080.
Same-process matrix (legacy diagnostic capture, batched snapshot):
1/0 80.02, 0/0 80.90, 0/1 82.29, 1/1 83.04, 1/1 80.54,
0/1 81.30, 0/0 80.42, 1/0 80.67, 0/1 80.52 FPS.
GPU approximately 95%, same Firefox foreground, Reflex Off, FG Off, diagnostics
Off. This supports at most a modest gain amid noise, not another large FPS jump.
The diagnostic MV/depth pointers were zero in every disabled sample. Defaults
were restored. Raw results: render-abba.json. Simulator preview inspected:
render-overhead-5080.png; both eyes present, no obvious lighting regression.

A native one-shot driver report used the existing serialized Reflex hook with
the broad diagnostic gate enabled for only 2.382 ms. Discarding the last eight
reports, the preceding 56 averaged 12.595 ms between simulation starts,
10.293 ms GPU active, 11.846 ms GPU span, 11.110 ms render CPU span and
6.825 ms simulation CPU span. These spans overlap; do not add them together or
equate one process's driver GPU-active metric with whole-GPU NVML utilization.
Raw report: driver-timing-5080.json. Diagnostic gate restored Off.

Framegen smoke test in PID 5080: temporary FG/FPS telemetry enable produced
45 real +45 generated =90 output FPS, zero current repeats, 460 input pairs
for each eye, 437 generated frames and no skipped frames. Status reported
engine motion vectors and depth. Legacy capture stayed off during generation;
diagnostics were enabled only to read the final report. The original FG Off /
FPS telemetry Off / diagnostics Off state was restored and verified. No new
crash report since the withdrawn 11:38 candidate.

## Tactical-belt follow-up in progress

The player has 21 vrp_belt components and 90 ucbelt components; this is not an
empty-component scan. updateHighlight repeated IsEnabled/centre transforms for
both hands. A candidate now prepares plain name/coordinate targets once within
that highlight tick, lazily after a valid palm. The frequency, per-hand distance,
hysteresis, grouping and union rules remain unchanged. No component references
or targets are cached across ticks. A performance.legacyHighlightScan field on
the module's public table permits an in-process A/B without changing settings.

The actual original/candidate helpers were run inside CET with lexical mocks:
768 comparisons passed, including tracking loss, disabled highlights, cadence,
changing component sets, failed enabled reads and per-hand hysteresis. Native
enabled-read count fell from 10234 to 5085. Full Lua module compilation passed.
The grenade trace also had a forced-on override despite being diagnostic; it
now respects grab_trace and defaults to false. Its off/on/off file-I/O gate
test passed. These Lua candidates are not yet installed or live-validated.

Tactical-belt candidates installed after closing PID 5080, with previous scripts
and belt.cfg archived in deploy-belt-highlight/previous. The local grab_trace
setting alone was changed to false; other belt settings and calibration were
preserved. The user confirmed gameplay in PID 16052 (GameThread 26164), same
B093BFA8 DLL. GetMod(...).performance is present; magKey and both magDist values
remain live. The trace file stopped growing at 12:48:49.

In-process legacy/shared/shared/legacy/shared comparison: 83.52 /83.31 /83.71 /
82.91 /83.20 FPS, GPU approximately 95%, same Firefox foreground. Thus this
change cuts component reads but shows no significant throughput gain here.
The shared path was restored. Raw data: belt-abba-*.json. New 300-sample main
thread profile: 62 Lua callbacks, TacticalBelt 17, versus 22 in the prior launch
(sampling noise/cross-launch differences apply). Most samples are native
redJobs2 FlushCounter waits. Offline decompilation confirms these execute jobs
and pump messages while awaiting dependent counters; do not bypass them.

Simulator overhead was isolated using its existing Mirror Rate menu, without
changing game rendering, poses, resolution or XR pacing. Mirror 90 Hz /Off /
Off /90 Hz /90 Hz: 82.31 /87.55 /88.52 /82.80 /80.94 FPS. The first two windows
included both Firefox and Codex as foreground, but the game stayed in background
throughout; later windows used Firefox. This is evidence of preview overhead,
not a mod optimization and not a real-headset benchmark. Original 90 Hz mirror
was restored and persisted setting verified. No simulator source changes.
Raw data and bounded restoration helper: simulator-mirror-ab.json and
simulator_mirror_ab.py. Nsight environment/help was inspected only; no new
capture, injection, Aftermath mode or game launch was started.

User steering: keep the simulator preview exactly as it is; continue optimizing
the port. Its measured overhead is background context, not an optimization target.

## Bounded GPU stage profiler

Candidate 4C04C0FFA7F8454884F5727E94076B30D695C819B801B0691A5DED4AC87E0676
adds a dedicated one-shot GPU timing request, separate from the expensive broad
runtime diagnostics gate. CyberpunkVR_GpuStageRequest accepts a recording budget
clamped to 32 batches; Poll at Present arms it. Results are published in
CyberpunkVR_GpuStageReport with CyberpunkVR_GpuStageReportSeq as a seqlock.
Default request/budget zero means no D3D calls, allocations or samples.

GPU timestamps cover CaptureMain, CaptureSecond, CaptureDepth, SubmitColor,
SubmitDepth, SecondHud, SecondBlit, HudSprites, HudBlur, HudStyle and HudSubmit.
SecondHud/SecondBlit are nested within CaptureSecond: do not sum them twice.
This measures queue elapsed time for recorded stages, not pure shader occupancy.
Each bounded batch owns its query heap/readback; KeepCommandResources retains
them through reset, execution and possible replay. The existing submission fence
is supplied only after a successful Signal. Poll maps only completed readbacks;
there are no CPU waits, new queue waits or altered production barriers.

Release build and tools/gpu_stage_tests passed, covering unarmed invalid-list
input (no D3D access), bounded requests, nonblocking GPU-delayed completion,
reset with another allocator, cancelled recording and command-list destruction.
The current B093BFA8 DLL/PDB were archived under deploy-gpu-stages/previous and
PID 16052 was closed for deployment. GPU timings in gameplay remain to be taken
after the user launches the candidate. Simulator preview remains 90 Hz.

Candidate installed and user confirmed gameplay in PID 21884. First 32-batch
GPU stage report completed in 0.125 s, cancelled=0, invalid=0, outstanding=0.
Mean times (microseconds): CaptureMain 31.44; CaptureSecond 47.75 (including
SecondBlit 44.28; SecondHud never ran); CaptureDepth 37.79; SubmitColor 41.74
per eye; SubmitDepth 35.26 per eye; HudSprites 40.70; HudBlur 62.66;
HudStyle 89.56; HudSubmit 26.97. These small costs do not justify speculative
rewriting of the capture/HUD pipeline. Request and profiling budget drained
normally; broad runtime diagnostics stayed off. Raw: gpu-stages-21884-first.json.

Next investigation needs native engine GPU pass timings. Nsight 2026.3.1 CLI
help confirms GPU Trace Profiler activity, F11 trigger, bounded frame/duration
limits, and set-gpu-clocks=unaltered. A 16-frame/500-ms single-pass launch plan
and equivalent manual script are prepared in nsight-launch-plan.json and
Start-NsightGpuTrace.ps1. Nothing has been launched or injected. The user's
earlier instruction says they start the game themselves, so obtain explicit
one-run authorization before using the launcher tool, or let them run the script.

User explicitly authorized one Nsight launch. The first CLI attempt exited before
launching a target because the output directory had not been created after the
old process-stop check; this was corrected. Actual launch handle
run_Cyberpunk2077_7022e6 (ngfx PID 23104) created game PID 11320, but timed out
waiting for the supported graphics API. No GPU trace was captured. The CLI
exited; the remaining owned game process had also exited by the cleanup check
after the user changed to RenderDoc capture. Do not automatically launch another Nsight run.

Latest user steering: they propose recording 16 consecutive frames with
RenderDoc themselves. RenderDoc is now authorized for this next analysis after
the game is closed. Compare GPU pass timings, MAIN/VRCAM work and resource
copies across captures; replay timings are approximate and do not reproduce
the live CPU/GPU scheduling timeline. Await capture paths. Current installed
DLL remains 4C04C0FF with the bounded stage profiler unarmed. Last normal probe
before the Nsight attempt: 84.20 FPS, GPU 94.96%, FG/overlay/diagnostics off.
