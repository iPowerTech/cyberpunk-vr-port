# Performance measurement and recurring startup GPU fault

The performance investigation began on installed DLL `58fc7a6d` (HEAD
`482549c0` plus the existing story/cyberware work). Frame generation and its
pacing-ready state were both off. UserSettings had VSync and MaximumFPS disabled.
The runtime was OpenXR Simulator at 90 Hz, 2560 x 2560 per eye, DLSS Balanced.

## Measurements

Artifacts: `build/performance-20260926/`.

- PID18864: about 50 FPS / 20 ms between real frames. The XR worker maintained
  90 submissions/s by repeating completed frames. Capture reported no fence
  waits or skipped frames in the measured window.
- Main thread TID25472 used about 77% of one logical CPU, with 15 render/job
  workers each around 20–26% of one CPU. Aggregate CPU utilization is not an
  estimate of spare capacity on the frame's critical path.
- Short thread snapshots were taken with suspend/copy/resume in one local
  function, always resumed in `finally`; longest measured suspension was below
  1 ms. Offline unwind identifies the main thread's busy wait as the engine's
  `redJobs2::FlushCounter` at EXE+14AB30. Other samples show driver SleepEx and
  CET calls; the driver stack alone does not identify the reason for the wait.
- FPS telemetry on/off ABBA: 49.61 / 49.51 / 49.96 / 49.35 FPS. There is no
  evidence here of a material FPS-panel bottleneck. The original INI was restored.
- PID9448, closed ImGui: no new ownership wait in a 7.3-second window; the last
  reported wait was stale (0.1308 ms), load guard was inactive. Its fence still
  advanced once per present. Read-only inspection of ImGui GetDrawData showed
  valid data with zero command lists, indices and vertices.
- User brought the game to the foreground: 52–55 FPS, still no ownership wait
  in the following window. Background samples were about 49–51 FPS. No hard
  45/90 frame limiter was found.
- 743 Lua updates over 15 seconds: Stereo HUD/module work averaged ~0.52 ms
  per update (HUD 291 ms total, VRCAM selection 58, keypad 16, story 19,
  overlay bridge 1). Temporary timing wrappers restored themselves.
- Reflex Disabled was compared briefly and restored to Enabled. No material
  change in frame rate. A DLSS Ultra Performance trial changed the settings
  value but did not establish a change in native render dimensions; do not use
  that trial as proof of a CPU bottleneck. DLSS was restored to Balanced.
- The GPU timestamp metric spans queue work between frame boundaries; it is
  not a measurement of GPU busy time alone. Hardware telemetry's CPU percent
  is systemwide, whereas the thread measurements above are game-only.

WPR CPU recording could not be started due to profiling policy (0xc5585011).
There is no WPR recording to stop. No RenderDoc replay was used.

## Withdrawn empty-ImGui submission experiment

Candidate `92965d1f` built CPU draw data first, avoided GPU submission for empty
frames, retained load/resize drains and ownership waits for real draws, and
prevented secondary passes from using a failed primary submission. It passed
16 overlay tests, including a real ImGui 120-frame empty/dot/callback/menu
transition test and the existing WARP GPU validation test.

The user then reported three startup crashes. The exact preceding DLL58fc
was immediately restored and the experiment removed from source. Its patch,
test source and matching DLL/PDB are archived under
`build/performance-20260926/rejected-empty-overlay/`. Do not treat the experiment
as a validated optimization or redeploy it accidentally. One rollback launch
reached the menu; this does not establish causation for an intermittent failure.

## GPU fault evidence

Reports 205111/PID26296, 205143/PID13156 and 205218/PID7712 all stop in
`Lighting` on the graphics queue, with subsequent lists not started. HRESULT
is 887A0006 / DEVICE_HUNG; the CPU assert at EXE+2A43F4B only reports the fault.
DRED has no address and its auto-breadcrumb interface returns 887A0004.

The same Lighting pattern exists in report202504/PID19656 on the preceding
DLL, before the ImGui experiment. Report202423/PID18864 is also a GPU crash.
Thus the last edit is not established as the root cause.

Offline NVIDIA Aftermath decoding of PID7712's nv-gpudmp establishes:

- Error_DMA_PageFault, Graphics engine, write to unmapped GPU VA75426275328.
- A compute shader instruction caused an MMU fault; PC `compute_01 +240`,
  shader size768 bytes. There is no shader hash/resource identity in this dump.
- Fingerprint040FBDC8FAB2740B, quality20, also seen in September24's GPU fault.
  The older shader size/PC differ; a shared fingerprint does not identify the
  exact same faulty command.
- Crash after19 seconds, engine-reported VRAM3261 MB: not evidence of VRAM
  exhaustion or defective hardware.

PID19624 on the rollback DLL reached and stayed at the main menu. A four-second
gated probe saw710 MAIN sky calls, zero VRCAM nodes, zero sky skips, zero
early-AA/cache/sky-radiance modifications, 2840 MAIN LUT table observations and
zero LUT lends. The menu's current stable path does not exercise those cross-eye
substitutions. All probe gates were restored.

## Next diagnostic launch

Candidate `a2caa113` contains only opt-in D3D12 diagnostics in Dred.cpp in
addition to the preceding source; the empty-overlay experiment is absent.
It reads `bin/x64/vrport_gpu_validation.txt` once before device creation:
1 enables the CPU debug layer, 2 additionally enables GPU shader validation.
No file means no debug layer/callback overhead. Errors/warnings are captured
with ID-based repetition limits through ID3D12InfoQueue1 and logged as
`[GPU-VALIDATION]`; no breakpoints or RenderDoc are involved.

Mode1 was tried. Reports211330/PID12492 and211415/PID12692 failed before any
render commands, DEVICE_RESET887A0007. The CPU stack reaches native state-object
creation at315F68 (return31635F), unlike the earlier in-flight Lighting fault.
No validation callback was attached and no debug-layer messages were collected.
The marker has been removed, Dred.cpp restored, and the exact DLL58fc restored
again. The abandoned diagnostic is archived under `validation-01/`; do not
retry this path blindly. No persistent driver/debug-layer setting was changed.
Performance measurements while validation is enabled are not representative.
Source for the GPU validation behavior:
https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-d3d12-debug-layer-gpu-based-validation

Root cause and final performance improvement are still unproven. No commit
has been made for this investigation.

## Aftermath's native diagnostic mode

The executable's native wrapper2A429E4 calls DX12_Initialize(version209h,
flags3, device) normally, and flags4000000Bh when its debug boolean is true.
Resource tracking is therefore already enabled. The debug variant adds shader
debug information and automatic call-stack markers. EnableGpuCrashDumps at
18767F8 already supplies the game's own crash/debug-info callbacks; the debug
callback2A439E0 keeps data in the engine's debug-info map.

Opt-in hook `AftermathDiagnostics.cpp` calls that same wrapper with debug=true
only when `vrport_aftermath_debug.txt` exists. The 2.31 entry bytes are verified.
It leaves SDK version, callbacks, registration, device and return value native.
No marker file means the hook is skipped. The SDK's required monitor was started
hidden for this session (PID7524; ownership manifest in `aftermath-02/monitor.json`).
No driver configuration was changed through the monitor.

Candidate `7baa4b20` is installed for one startup to the menu, without loading
gameplay. Match DLL/PDB in `aftermath-02/`. Remove the opt-in marker and stop only
the task-owned monitor after the capture. This is a diagnostic build, not a fix.

That startup was PID23628/report213349. It reproduced Lighting/DEVICE_HUNG,
but there was no nv-gpudmp or initialization log: the lower wrapper was never
called. The statement that the extended SDK initialized was premature; only
installation of the hook was confirmed. Monitor7524 was stopped, marker removed,
and DLL58fc restored.

Native renderer setup7E0BB4 defaults its enable byte to0. Setup87518C receives
two bytes (enabled/debug); it calls2A429E4 and publishes byte3494010 only when
enabled. Candidate `ea1f1a8f` also wraps87518C with local options{1,1}, preserving
the native setup and initialization result. Both hook sites are byte-validated.
Monitor26184 and marker are temporarily active for this next launch; cleanup
manifest and matching DLL/PDB are in `aftermath-03/`. Verify BOTH setup and
initialization logs before claiming the SDK is active.

Report233037/PID26408 on candidateea1f again has Lighting/DEVICE_HUNG and no
nv-gpudmp. Neither Configure nor Initialize logged an entry. Hook installation
alone was again insufficient. The timing defect is explicit in WorkerThread:
Boot is installed after Sleep(8000); native device setup can already have run.

Candidate `2cd2d746` installs the Aftermath observers in PreDevice, called
synchronously from PluginBootstrap after MinHook initialization, before starting
the delayed worker. No other current hook used PreDevice. Entry/return logs
distinguish a hook call from a mere successful installation. The user was asked
to stop at the resolution launcher so the SDK result can be verified before
continuing startup. DLL/PDB/source: `aftermath-04/`; monitor remains the task-owned
PID26184, with its cleanup manifest in `aftermath-03/monitor.json`.

PreDevice timing is now verified in actual launches: PID6812 and PID20272
both log Configure entry, Initialize entry/result1, and options0,0 ->1,1 before
the launcher. Both reached the main menu. Their logs are archived under
`aftermath-04/`. This proves diagnostic activation, not a crash fix; the full
mode changes timing and used roughly9.7–10.7 GB private memory in these runs.

Candidate `89f08f12` adds marker modes:1 = native flags3 (markers/resource
tracking),2 = native flags4000000B (also shader debug/automatic call stacks).
Missing marker still installs nothing. Mode1 is installed for the next cold
startup to avoid the heavy instrumentation hiding the intermittent failure.
DLL/PDB under `aftermath-05/`. Both earlier menu runs have been closed, no game
was auto-launched, and no settings/calibration were reset.

## Resource-tracking capture: PID18372

Mode1 initialization succeeded, and report234256/PID18372 reproduced the
failure. This time the engine reports PageFault at gpuApiDX12Error.cpp(27),
and contains a2.42 MB nv-gpudmp. Decoded JSON is `aftermath-05/crash-18372.json`.
The failed write is GPU VA1197E32000. Aftermath identifies two prior allocations
covering that address, both destroyed:

- R32_TYPELESS,1485x1485x1,mip1,9175040 bytes; base1197E00000,
  context289F72EBC40, destruction tick83591156.
- R16G16B16A16_FLOAT,160x160x64,mips2,15073280 bytes; same base,
  context289CCAA9430, destruction tick83591109.

Dump tick83593921, compute_01+240,size768B, no shader hash. The named resource
is a likely allocation candidate, not yet proof that its destructor itself is
wrong: a stale descriptor can also address freed/reused memory. The R32 shape
matches an internal-resolution texture; the earlier volume shape is consistent
with volumetric rendering. Shader identity/last binding remain to establish.
Aftermath also marks several later command-list contexts executing, so the
engine's coarse Lighting breadcrumb cannot alone pinpoint a specific dispatch.

Candidate `7dfa403e` adds a bounded observer around the existing SDK resource
register/unregister calls, installed before native device setup. It records only
R32_TYPELESS 2D textures >=512px and RGBA16F volumes128–1024 wide, at most256
registrations. It logs immutable descriptions, handles/pointers, ticks and CPU
stacks; it never AddRefs resources, changes descriptors, or reads a resource
after unregister. Marker mode remains1. DLL/PDB/source: `resource-life-06/`.
Waiting for one startup to correlate the next fault candidate with its lifecycle.
