# Native frame generation work

**Current decision:** the user rejected the standalone NVIDIA interpolator for
severe ghosting/artifacts and explicitly requested restoring NVIDIA OFA +
FidelityFX. The two supported options are now that hybrid and full FidelityFX.
The experimental shader is removed from the product; its local research copy
is under `build/framegen-native/rejected-native-interpolator`. Passing simple
GPU fixtures or pose/counter checks must not be represented as real-scene visual
acceptance. The user reports full FidelityFX looks good. After deployment of the
restored hybrid and complete telemetry shutdown (DLL `26ebce35...`), the user
reported "Вроде все ок" before requesting the hand/follow changes below; this is
general acceptance, not a controlled image-quality comparison.

FPS overlay OFF now means telemetry OFF, independently of generation: no CPU
history/counter collection, hardware worker, generation timestamp collection or
ImGui statistics graphs. Completed query resources and the XR quad/upload/bitmap
are retired; pending GPU/image ownership is honored without blocking. A light
framegen status remains available for settings/errors. Re-enable starts fresh
history. Tests cover both generators continuing through telemetry toggles,
resumed GPU queries, stopped hardware monitoring and pending XR-image cleanup.

Diagnostic exports/stage counters are now also controlled by the launcher DEBUG
gate, independently of the user-enabled FPS overlay. See
`debug-gate-20260923.md` for coverage, test results and deployment. A disabled or
frozen diagnostic report is not evidence that frame generation stopped.

## Hand smoothing and delayed menu/HUD follow

When frame generation is enabled, `FlushHandsToShared` bypasses the entire hand
filter before the base/weapon/wheel paths. Both hands use their current source
pose, and the filter history is invalidated. Saved lerp rates are untouched;
turning FG off seeds each filter from the current pose before smoothing resumes.

Menu quad and HUD head-follow share the same policy. If the head/panel yaw gap is
strictly over 10 degrees and inside its configured free-look cone, three seconds
at rest start the existing smooth catch-up. A 0.5-degree heading tolerance accepts
small HMD jitter; further turning restarts the wait. Outside the cone, catch-up
starts immediately. Tracking loss, recenter, panel closure and long timeline gaps
discard waiting time. HUD body-follow and FPS overlay cone behavior stay as before.
The XR display timeline measures rest time separately from the 50ms motion-step
clamp, so low frame rates do not stretch the delay.

Validation: Release plugin build succeeded (`build/framegen-native/hand-follow-build.log`),
29/29 HUD CTests passed, including jitter, delay reset, angle boundaries, wrap,
slow frames and the actual HUD quad update path. These tests use production math
and WARP/mock XR and do not replace a real-headset usability check.
The rebuilt framegen tests also passed `overlay`, `overlay_follow` and
`metrics_disabled` (3/3). DLL SHA256
`6410339cf902289815ebb32488077d884dda60bb6f587929c116bb58fd6cbd27`
was installed with settings/calibration preserved; deployment record:
`build/roomscale-plugin-deploy-20260923-215030/deployment.json`. Game launch is left
to the user; this build has not yet been checked in a live headset session.

Requested: engine motion vectors and depth, in-process generation without OFXR's
tray/API-layer resource duplication, an ImGui Framegen tab, and a lightweight
headset overlay showing real/generated FPS and CPU/GPU frame spikes.

## Inputs verified before implementation

* OFXR source: `C:/Users/dariulone/Desktop/OFXR/OFXR-Bridge-main`, V068/0.2.0.
  Its synthesis is color-only. Its existing history, private swapchains and
  presenter duplicate facilities already provided by this port.
* The saved OFXR tray preset is NVIDIA Slow, 50%, unidirectional.
* Local GPU: RTX5070Ti. The existing game uses D3D12 and a native two-eye view.
* AMD SDK1.1.4, commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`, was fetched
  under `build/fidelityfx-native`. Optical Flow, Frame Interpolation and the
  DX12 backend build successfully; logs are `build/fidelityfx-native-*.log`.
* Streamline ABI headers come from NVIDIA-RTX/Streamline commit2122257.
  Their original MIT notices are retained under `externals/streamline/include`.
* Static engine assembly for0x1D4FDC0 and0x78933C is in
  `build/framegen-native/streamline-native-assembly.txt`.
  slEvaluateFeature has FIVE arguments in this game, not the four in the old
  disabled diagnostic hook. slSetConstants has three. Use relocating MinHook.

## Integration constraints

Do not use the old last-pointer NGX getters as frame-generation inputs. They
discard frame identity, eye, extent, state, MV scaling, jitter and reset data.
Take engine resources while their command list owns them, restore their states,
and publish only after that list is submitted. Match native frame index, camera
identity and eye to the captured color. Unknown/missing inputs skip generation.

Keep the existing XR session, device, queue, swapchains and color capture leases.
Independent temporal contexts per eye are required. No color readback, desktop
capture, tray registration or second XR frame loop. Generate at most one midpoint
between two real frames; report repeats separately rather than counting them as
new real or generated content. Keep HUD/metrics outside interpolation.

All history and shader work must stop when disabled; release resources only after
their last GPU use. No per-frame GPU/CPU drain for telemetry. CPU frame cadence
and GPU timestamps must be labelled accurately; submission FPS is not physical
headset scanout. Additional algorithm/history VRAM is necessary and must be counted.

## Implementation and verification so far

Native NVIDIA/FidelityFX flow feeds FidelityFX Frame Interpolation with engine
motion/depth. It shares the port's device/queue/color capture/XR swapchains.
There is a Framegen tab, settings persistence, a midpoint/real-endpoint presenter,
half-refresh pacing, asynchronous GPU queries and a small single-quad FPS overlay.

Eight CTests pass (policy, cadence, settings, metrics, WARP interpolation, overlay,
planar depth copy including value preservation, GPU query ring). The real NVIDIA
OFA test also passes on RTX5070Ti with exact expected moving-box midpoints in both
eyes. It caught a teardown crash in nvwgf2umx: unregister and release the OFA
textures BEFORE destroying their context. This is fixed; the red crash/log is
under `build/framegen-native/nvidia-before-retirement-fix.dmp` and related logs.
All19 pose identity/ABI tests and21 HUD tests pass. Renderer submission lock
must precede the GPU-timer lock; input recording releases the registry lock before
calling resource/barrier hooks. Native resources are never freed before completion.

First candidate installed via DLL-only deployment
`build/roomscale-plugin-deploy-20260923-181705`:
SHA256 `0ed9219bf2498a49a8fbcebffa236fb1a5a0448c9cd2a471d1af2356c197420e`,
3,833,856 bytes. Its matching DLL/PDB are preserved in
`build/framegen-native/candidate-0ed9219b/`. Rollback is the deployment's
`CyberpunkVR_Stereo.before.dll` (previous accepted2a516851...).

PID32136 started18:17:14 and uses OpenXR Simulator, not the physical headset.
`live-32136-off.json` verifies roughly50-52 real FPS, no synthetic frames, separate
repeat counters, working CPU/GPU frame timing and zero tracked framegen VRAM.
Enabling generation left the input counters at zero (`live-32136-on.json`).
Read-only symbols confirmed `g_setTagHookInstalled=0`, `g_origSlSetTag=0`, with
DLSS actually active (`VrcamDlss=1`, eval hits advancing). The legacy manual E9
tag hook had not installed. Source now uses MinHook for that hook, classifies
constants by actual SL viewport id, derives FOV from the supplied projection,
and logs the first camera validation samples. This builds but IS NOT DEPLOYED YET.
The running game was returned to `xr_framegen=0`; FPS overlay remains1.

## Latest user steering: expand performance overlay

The user supplied an fpsVR reference and explicitly requires CPU/GPU frame-time
history graphs, average and1%/0.1% low, GPU memory, GPU/CPU usage, RAM, prepared
top/bottom/etc placement options and a separate free-look cone. The initial
three-line overlay is superseded by detailed720x340, compact420x336 and minimal
360x128 layouts. There are9 placement presets, angular X/Y offsets, scale,
distance and an independent yaw free-look cone. History windows are10/30/60/120s,
with120 graph buckets that retain the maximum frame time per bucket. FPS lows
average the slowest1%/0.1% of real-frame intervals (minimum100/1000 samples).

CPU total/per-logical-core load, RAM/working set/commit, adapter-local process
VRAM/budget, NVIDIA utilization/temperature/board VRAM are sampled on a worker
at1Hz. The exact rendering GPU is selected through DXGI LUID -> D3DKMT PCI address
-> NVML. A unique NVIDIA adapter is the only allowed fallback. Missing sensors
are unavailable, never zero-filled estimates. Bitmap/chart updates stay at4Hz.
Local hardware: Ryzen7 5800X (16 logical CPUs), RTX5070Ti; nvml.dll is in System32.
Windows SDK has d3dkmthk.h, allowing exact DXGI LUID -> PCI address matching.

All11 expanded tests passed (`expanded-final-tests.log`), including metric history,
all layouts, single BOTH-eye layer, yaw cone/preset behavior, settings roundtrip,
GPU queries, depth copy and hardware monitoring/restart. The hardware test read
CPU68.4%, GPU88%,61C, VRAM8.37/15.92GiB and RAM20.13/31.95GiB; its poll took0.156ms.
Those are one test sample, not idle overhead or in-game framegen performance.
The expanded overlay and tag-hook fix were deployed at19:03 through
`build/roomscale-plugin-deploy-20260923-190305` after closing PID32136.
DLL SHA256 `890a134ec66c0a67ce0a9c415cb2d51825834410b5fb768f0db5750ace2921ee`,
3,867,136 bytes. Settings/calibration preserved. FPS overlay remains enabled,
generation remains disabled for the next startup check. User starts the game.

Primary API references:
* https://docs.nvidia.com/deploy/nvml-api/nvml-api-reference.html
* https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation
* https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_adapteraddress

No live frame-generation acceptance yet; engine input pairing still needs the
next build/launch. User starts/restarts the game; agent may close it for deployment.
Do not run RenderDoc replay while the game runs. Do not load screenshots into the
chat tools (earlier image tool inputs repeatedly caused Bad Request). No commit
requested for this feature task yet.

## Native camera staging correction, 19:13

The 19:03 candidate was tested in simulator PID31224. The expanded panel reported
live CPU/GPU load, memory and frame history. With generation enabled, MAIN input
count advanced from118 to338, but VRCAM remained0 and generated frames remained0
(`live-31224-expanded-on.json`). Private read-only symbols showed eight valid,
current MAIN camera records and eight empty VRCAM records, despite the native
VRCAM constants detour running. This isolates the missing data to the common
Streamline constants path, not texture tags or the eye's DLSS evaluation.

Static assembly of RVA0x78933C confirms a shared last-frame gate at state+0x4A0:
it can skip the second eye's slSetConstants in the same native frame. The camera
writer has already filled that eye's staging block before this gate. The plugin
now reads those staging fields in the existing constants detour before calling
the original, recording an independent camera per eye and native frame. It does
not change the gate or the game's DLSS histories. FOV is derived from projection
Y (+0xB4), because Cyberpunk sends its sl::Constants cameraFOV in degrees.

This correction built successfully and was installed through
`build/roomscale-plugin-deploy-20260923-191323`. DLL SHA256
`cdbc2854fd3d4e6536b18e04f4bffc4ac3ef43654dc1f077471e39ada221a912`,
3,868,672 bytes. PID31224 was closed for deployment; settings and calibration
hashes are unchanged. Generation remains disabled for startup. The user was
asked to launch and load a save for both-eye input verification.

Whitespace-only normalization was applied to the vendored NVIDIA/Streamline
headers; API definitions and license text are unchanged. `git diff --check`
passes with Git's line-ending warning disabled for the check.

## Both inputs captured; RTT frame token is not a render clock

PID27552, started19:13:45, confirmed the native camera reader works: after
enabling, MAIN/VRCAM input counters reached444/444, all input stages advanced,
but presentation correctly refused to pair MAIN frame10245 with VRCAM frame0.
Every valid VRCAM camera record still had frame0. Its camera reset flag was false
and its position differed from MAIN by the expected IPD; these are actual eye
inputs, not a copied MAIN camera. Reports: `live-27552-camera-off.json`,
`live-27552-camera-on.json`, `native-frame-counter-27552.json`.

The earlier last-frame-gate explanation was incomplete: the RTT view's native
viewData+0x1A0 is permanently zero (also documented by the existing ViewReuse.cpp
environment census). It cannot identify a sequence of rendered textures.
`renderer+0x4CA4` is the common rendering frame clock: static assembly shows its
increment at0x294055, frame driver0x29384C, accessor0x1902110 and use by temporal
passes such as0x788104. It is two ahead of the latest MAIN Streamline token in
the measured run; no assumed constant offset is used in the correction.

The node dispatcher now stamps color/depth/MV with the common renderer clock.
Camera capture and successful DLSS evaluation use that same node stamp, while
pose and tracking-origin equality remain required at each association. The
engine's zero RTT token and its global frame counter are never written. Details:
`native-frame-counter-assembly.txt`, `render-frame-lifetime-assembly.txt`,
`render-frame-driver-assembly.txt`. This correction still needs the next live run.

The user additionally requested CPU/GPU model names, `cyberpunk-vr-port` as the
title, the selected frame generator's name when enabled, and MIN/MAX beside AVG.
All layouts now include these fields; model names were already cached by the
1Hz hardware sampler. New dimensions: detailed720x452, compact420x404,
minimal360x222. The title uses lowercase glyphs. MIN/MAX are reciprocals of the
slowest/fastest real-frame interval in the same selected history window as AVG;
they are not percentiles or generated-frame rates. The diagnostic report and
ImGui also expose the extrema. All7 targeted policy/cadence/settings/metrics/
history/overlay/follow checks pass (`overlay-names-tests.log`). No commit yet.

The combined build was installed at19:30 through
`build/roomscale-plugin-deploy-20260923-193005` after closing PID27552.
SHA256 `44b4965b0c0e881bcf4c1d13dd743fbd4d1c23d816a96f308e9b2d564a90f326`,
3,870,208 bytes. Settings and calibration unchanged; Framegen0, overlay1.
The user starts the next game process. The previous candidate's matching DLL/PDB
remain in `build/framegen-native/candidate-cdbc2854` for its saved diagnostics.

## Live generation and latest user direction

PID7656 with44b4965b generated successfully. Both eyes shared render frame9641
and their independent poses/depth/MV. The steady simulator run reported about
50-52 real,35-37 generated and79-82 unique output FPS at90Hz; tracked generation
resources479,300,160 bytes (~457MiB), GPU generation roughly4-6ms. This proves
native generation executes, not physical-headset image quality. The user has
not tested it in the headset yet. They confirmed the expanded panel is readable,
without overlap, and manually chose bottom-center (`overlay_corner=5`).

The user correctly questioned why REAL was not45 and OUT not90. The original
limiter kept its deadline in TLS; a migrating Present job therefore restarted
or split the limit across workers. A regression test with16 worker changes and
a10ms real-frame target ran in6.07ms before the correction (expected150ms).
One shared schedule plus its own mutex passes this test and the policy/cadence
checks. Runtime90Hz and pacing-enabled flags were also confirmed read-only.
Turning the old generation off freed all tracked generation VRAM (0) and stopped
generated counters (`live-7656-retire.json`).

The pacing fix was installed at19:39 via
`build/roomscale-plugin-deploy-20260923-193933`; SHA256
`89080424c4c05ac4d8cea4cfc40c321343c7800617737df38dd1d4bc06f0b6d6`,
3,870,208 bytes. No live verification of this pacing correction yet.

LATEST REQUIREMENT: user explicitly wants TWO INDEPENDENT backend paths:
NVIDIA OFA with its own image interpolation, and FidelityFX with its own.
NVIDIA must no longer call FidelityFX Frame Interpolation. Earlier preference
for choosing best verified quality remains relevant to A/B comparisons, but does
not supersede this explicit separation. Keep native engine MV/depth in both.

LATEST AUTHORIZATION supersedes the earlier manual-start restriction: the user
went to the pool and explicitly authorized the agent to launch/restart the game,
send input, press Start in the resolution launcher, then Continue at startup,
the first Continue menu item, and Continue after the save loads. Screenshots are
now expressly allowed if needed. Use the computer-use skill (@oai/sky through
node_repl) for Windows UI actions. The skill and guidance/API/confirmations were
read. Still never run RenderDoc GPU replay while the game is running. No commit
was requested for framegen.

## Shared pacing verified; independent NVIDIA synthesis

The agent launched PID18760 and loaded the save through the UI with the user's
new authorization. The shared limiter held44.941-45.002 real FPS. Unique output
was86.95-87.71 in this sample; generation43-ish, GPU synthesis6.5-8.6ms. Thus the
REAL45 bug is fixed, but this is not a claim of sustained90 unique output frames.
Reports: `live-18760-off.json`, `live-18760-pacing-on.json`.

NVIDIA is now an independent pipeline: bidirectional NVOFA, native midpoint
shader, engine motion/depth, previous color/depth and camera data. NVIDIA's
Eye::Create returns before any FFX backend/context creation; dispatch goes to
NvidiaInterpolate instead of FFX. FidelityFX separately owns both its optical
flow and frame interpolation. The dropdown is named Frame generator and the
overlay shows NVIDIA Optical Flow or FidelityFX, matching the actual path.
No OFXR shader code was copied; its color-only reference lacks the required
native engine MV/depth inputs. The new shader is in
`include/Framegen/NvidiaInterpolation.hpp` under the project license.

The first NVIDIA edge test found a2-pixel midpoint bias. Seeding inverse motion
lookup from nearby foreground depth and refining against the actual per-pixel
motion fixed it: both eye centroids now exactly equal their known midpoint.
Mean RGB error is0 on three of four synthetic eye outputs and0.1073/255 on the
fourth. A separate moving-light/zero-engine-MV plane fixture tests the optical
flow fallback, with mean errors5.67-5.94/255. These are small fixture results,
not a guarantee of artifact-free gameplay. Resource retirement and independent
eye histories pass on the RTX hardware. Logs: `separate-nvidia-mv-quality.log`,
`separate-nvidia-flow-quality.log`. All13 CTests pass, including the complete
FidelityFX path, its flow fallback, shared pacing, history and overlay checks.
`separate-backends-build.log` records the successful DLL build. The game was
closed before these hardware checks. New backend costs and native visual
behavior still require the following deployment/live validation.

The independent-backends build was installed through
`build/roomscale-plugin-deploy-20260923-200833`, SHA256
`1fb57a90e7d3bd514adcee75a60dbbb5ba0de85e235defc3f5f1fd1f273013f4`,
3,880,960 bytes. PID21064 was launched through the UI, Start Game pressed at
2560x2560/eye, and the startup Continue screen was passed.

LATEST USER STEERING at20:14: the user returned and revoked automatic launching:
"игру запускать больше не нужно я сам открою. в геймплее". Stop automatic launches
and menu input. They have loaded gameplay; continue metrics/backend verification
there. Any later restart must be left to the user. Screenshot permission has not
been revoked. No commit was requested.

Both independent backends were validated in PID21064 and hot-switched without
restart. Static scene: about45 real +45 generated, no repeats in the sampled
windows. Tracked allocation peaks: NVIDIA279MiB, FidelityFX456MiB. Disabling
returned tracked generation memory and generation_ms to0; re-enabling succeeded.
Reports: `live-21064-native-nvidia.json`, `live-21064-native-fidelityfx.json`,
`live-21064-retired.json`, `live-21064-nvidia-restored.json`.

The user's additional motion/jitter validation is documented in
`docs/framegen-motion-20260923.md`. Both25-second tests passed pose/input lifetime
checks and restored the simulator pose. They found an FPS estimator issue for
sparse events, now corrected in source and tested. The new diagnostic report
also exposes real_submitted and repeated totals. `rate-window-build.log` records
the successful new DLL build; it needs deployment/restart. Matching prior
DLL/PDB are preserved in `build/framegen-native/candidate-1fb57a90`.
