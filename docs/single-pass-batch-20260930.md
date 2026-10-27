# Single-pass batch conversion and CPU preparation audit

Status: offline bytecode candidates, not a continuous stereo renderer. No new
single-pass FPS gain was measured or deployed. The first batch audit below used
the unchanged mono-cascade DLL (`d8e2079ed054331860982682f3acdde13d38f672c370cad06db11556b5f6a644`).
The subsequent live-observer/HUD-readiness continuation is recorded at the end.

## Batch conversion

`tools/single_pass_tests/batch_shaders.py` processes the existing native shader
inventory in one invocation, without opening RenderDoc or replaying on the GPU.
It verifies original SHA256s, obtains lossless DXC IR, classifies resource layouts,
converts camera reads, regenerates ViewID metadata, assembles and validates DXIL.
Stages without the typed camera block receive only the SM6.1 metadata upgrade;
they are **not** classified as eye-independent.

| Saved frame 10323 inventory | Result |
| --- | ---: |
| Unique original shader blobs | 239 |
| Graphics camera conversions passing DXC validation | 83 |
| Graphics metadata-only upgrades passing DXC validation | 54 |
| Compute shaders excluded from view instancing | 98 |
| Legacy DXBC blobs excluded (2 VS, 2 PS) | 4 |
| PSOs with all constituent bytecodes prepared | 98 |
| Resource/camera-layout audit families | 112 |
| Runtime-approved PSOs | 0 |

All 45 `RenderElements` PSOs in this saved inventory have prepared bytecodes
(2,993 native draw events across both eyes). This is capture-specific coverage,
not whole-game coverage. The two most frequently used PSOs, 152895 and 152910,
account for 865 events each. Both depend on additional constants and texture
resources, so camera conversion alone is insufficient to combine their draws.
None of these 83 camera consumers reads the previously unavailable camera word
147; this does not establish availability of every other binding/history input.

Cache keys include input bytes, converter/helper sources, DXC executable/compiler/
validator DLLs, assembler and optimizer passes. Cached output hashes are checked
on reuse. Corrupt or incomplete entries are rebuilt. Audit families never permit
substitution of another member's binary. A repeated run reused all 239 entries.
The manifest preserves shader-to-PSO mappings and native fallback requirements:
pipeline linkage, per-eye visibility, target routing, non-camera constants,
SRVs and UAV side effects. Replay event timings are ranking hints, not additive
live frame time. Individual module validation does not prove VS/PS linkage.

```powershell
py -3.13 -B tools/single_pass_tests/batch_shaders.py --index build/gpu-audit-20260930/native-shaders/index.json --frame build/gpu-audit-20260930/frame-10323-detail.json --dxc-dir 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64' --assembler build/single-pass-tests/Release/dxil_tool.exe --out build/single-pass-20260930/batch
py -3.13 -B -m unittest discover -s tools/single_pass_tests -p '*_test.py' -v
```

24 Python tests passed, including 14 batch tests. Tests cover unsupported inputs,
container bounds, missing metadata, corrupt caches, duplicate stages, conflicting
camera layouts, conservative dependencies and workload counting.

## Live CPU preparation

`profile_prepare.py` samples the existing gather/filter/finalize/sort counters for
a bounded interval, checks conflicting diagnostics and crash state, and restores
controls in `finally`. It does not skip native work or enable the preparation gate.
PID 26720, framegen off, High cascades, current committed DLL:

| Inclusive scope | MAIN total, 15.117 s | VRCAM total, 15.117 s |
| --- | ---: | ---: |
| PrepareStage | 260.497 ms | 367.561 ms |
| Gather | 27.353 ms | 41.850 ms |
| Filter | 30.226 ms | 52.092 ms |
| Finalize (includes sorts) | 187.301 ms | 250.988 ms |

The cascade report advanced approximately 1,115 stereo pairs. That gives roughly
0.234/0.330 ms per pair for MAIN/VRCAM PrepareStage, with denominator uncertainty
from the 250 ms report cadence and settling interval. Parent/child times must not
be added. These scopes exclude upstream culling/materialization and downstream
packet consumption/command recording. They do not establish CPU critical-path
time, a 40% duplicated CPU share, or an FPS prediction.

A subsequent bounded packet snapshot returned zero records and was rejected.
The installed release has no validated probe shader assets; its packet-consumer
hook is currently conditional on that old GPU probe. No packet equality or common
CPU-preparation result is claimed for this run. Existing owned September 28 packet
fixtures remain available; the observation hook should be separated from those
hard-coded GPU assets before a fresh full preparation trace.

Reports: `build/single-pass-20260930/batch/manifest.json` and
`build/single-pass-20260930/prepare-26720-cycle.json`. After sampling, runtime
diagnostics, preparation profiling, GPU probe request and preparation gate were
read back as zero. No game restart, deployment or graphics setting change occurred.

## Integration direction

Unity's `XRPass` owns multiple views and texture-array slices and enables stereo
for a whole pass, rather than independently replaying each draw:
[XRPass.cs](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.core/Runtime/XR/XRPass.cs).
Unreal prepares cacheable mesh commands above the RHI:
[Mesh Drawing Pipeline](https://dev.epicgames.com/documentation/en-us/unreal-engine/mesh-drawing-pipeline-in-unreal-engine).
For this closed engine, the resulting design requirement is an owned current-pair
pass description, shared preparation plus separate visibility masks, and array
targets retained across the entire compatible pass. Native view scratch pointers
are reused across eyes and cannot provide that ownership. Hardware routing alone
does not remove CPU preparation:
[DirectX View Instancing](https://microsoft.github.io/DirectX-Specs/d3d/ViewInstancing.html).

Still required: actual PSO linkage tests for each family, complete binding/history
validation, shared native CPU preparation, persistent pass-wide arrays and a
continuous mode without MAIN wait/reference/backup recording. The old 26-33 ms
diagnostic gate remains disabled. Native multipass stays the fallback.

## Independent live capture and managed startup

The native packet-consumer hook now depends only on its checked EXE prologue,
not on the old private GPU shader assets. It forwards directly while capture is
off. `GeometryPacketHookCompatible` distinguishes an unexamined/unsupported EXE;
it is not by itself proof of a successfully installed detour. The installed hook
log and nonempty live captures provide that proof. Packet ABI remains 216 bytes.

The latest user rules are implemented by `run_launch_tests.py`:

- Start Game one second after the resolution launcher appears.
- After the actual game window (not the preliminary window behind the launcher)
  appears, wait five seconds, activate/verify the game, then activate/verify the
  Codex desktop window one second later. No window-title guess is used for Codex;
  the owning package path identifies it. Native continuation restores Codex.
- Resume after the single-player menu finishes initialization AND receives its
  save list, instead of arming at the base native controller's OnInitialize.
- Require scripting world readiness, native menu=0, stable HUD, both scene images,
  valid owned packets for both eyes, repeat/bounded captures and a CPU sample.
- Monitor PID-matched crash reports and newly appearing reporters. Reject failed
  stages, preserve a summary with unfinished checks, and close the exact game PID
  in `finally`. Never leave a failed startup running for later inspection.

HUD readiness combines the existing seqlocked pose diagnostic with its new
source serial/timestamp and a counter incremented only after successful
`xrEndFrame` with the main gameplay HUD layer. The old 80-byte pose prefix is
unchanged; the record is now 96 bytes. Require at least one second, 30 successful
HUD frame submissions, fresh sources under 300 ms old, valid tracking/layers and
advancing source serials. Missing, cached, paused or stale samples reset the
stability interval. Counters are diagnostic-only, off during ordinary play.

Two full runs on the final DLL passed all nine stages:

| PID / report folder | HUD stability | Owned resolved packets | Four-frame packets |
| --- | --- | ---: | ---: |
| 22052 / `hud-ready-menu-06` | 1,015 ms, 88 submissions, 49 source advances | 3,178 | 12,747 |
| 28204 / `hud-ready-confirm-07` | 1,016 ms, 88 submissions, 49 source advances | 3,219 | 12,876 |

Both runs returned all test controls to zero, had no new crash reports, and
closed the game. Both succeeded without the experimental extra activation pulse;
that ineffective recovery was removed afterward. Earlier rejected attempts
included native menu=1 despite a scripting player, and a child window-enumeration
script failure. They were not treated as gameplay/FPS samples or random crashes.
The window guard now uses the existing process-scoped API directly, without a
separate Python process or lossy hidden stderr.

Installed DLL SHA256:
`b19a8d99d1dbc183cf6aca0a02dfe9e18927760648846e6a79d1a865bd0a7b37`.
PDB: `b41e6f97326a8188b412dacea2c0ffc1ae1959ee6ea42b85cf72ce195b0a670b`.
Backup: `build/single-pass-20260927/deploy-hud-ready-menu-06`.
Settings hashes were preserved during both deployments. Main/VRCAM scratch still
aliases the same storage, and raw packed IDs do not match across eyes. These
captures enable the next shared-preparation investigation; they do not authorize
skipping draws or demonstrate a new single-pass performance gain.

Validation: 11 launcher unit tests, 31 single-pass Python tests, four native
packet/instance tests and the WARP HUD-submit test. HUD tests cover frozen source
serials, stale data, gaps, missing layers, menu resets and the difference between
preparing a layer and successfully submitting it. No RenderDoc replay was opened.

## Order-preserving mesh pass (goal continuation)

`StereoMeshPlan.hpp` now builds a common command sequence with eye masks 1/2/3.
It preserves **both native eye orders and duplicate multiplicity**, rather than
assuming opaque draws may always be freely sorted. Shared IDs must come from
an exact, certified draw contract; ID 0 always remains per-eye. A bounded
Hunt-Szymanski LCS selects the maximum order-compatible common subsequence, then
emits its shortest common supersequence. Excessive duplicate matches reject the
plan without changing the previous output, allowing native fallback.

This scheduler is exercised by the actual captured VS/mesh GPU fixture, not just
a list comparison. The two native sequences have ten draw calls total; the plan
has six calls (four both-eye, one first-eye, one second-eye). The optimized path
binds the two-layer targets for the whole pass, records only those six draws,
and has no preparation gate, predicate backup or intermediate per-draw copy.
Boundary copies/readbacks are test/legacy-consumer overhead, not removed cost.
16 nonempty camera/scale/roll/clip cases passed with **zero color differences and
bit-exact depth**. The prior 22 GPU cases also pass. Explicit clipping cases use
zero additional clip-space translation, because combining clipping with some
translations puts this particular captured mesh outside both visible regions;
those empty results were rejected, not accepted as correctness evidence.

`NativePacketKey.hpp` defines capture-side candidate equivalence for ordinary
kind-0 mesh packets. Fresh EXE disassembly was checked for the consumer at
1F1208 and its helpers 1F1E44, 1F1FA8, 1F020C, 1F3F2C:

- First-word low 13 bits are sorting, not rendering state. The mesh planner
  retains eye ordering separately. Rendering/control bits 13 and above remain.
- Ordinary mesh record reads are byte +0 and +8..+2C. Padding/union bytes +1..7
  and +2D..3F contained unrelated native stack contents and caused false misses.
- State record program pointer +10 is replaced by owned program bytes; padding
  +1C..1F and +27 is excluded. Other fields, complete programs, transforms,
  skinning records, counts and all unresolved identities remain compared.
- This is NOT proof of the contents/lifetime of pointed-to mesh/material objects,
  shader descriptors or camera data; candidate keys cannot authorize live draws.

`NativePacketPass.hpp` reconstructs the native sorted packet span from complete
worker partitions. For plane 6 the current engine prepares one VRCAM partition
and four MAIN partitions; MAIN callback order was 0,1,3,2, not sorted order.
The assembler checks all partition IDs, contiguous original ranges, copied data
bounds and the native floor(total*part/parts) distribution. Different filter
subpasses on plane 24 are retained separately. Addresses are never dereferenced
or replayed; output ranges index owned snapshot memory only. Worker lane IDs are
not semantic pass identity.

Offline candidate results (packets are NOT necessarily one D3D draw each):

| Source PID | Native packets | Planned candidate packets | Common candidates |
| --- | ---: | ---: | ---: |
| 22052 | 3,178 | 2,220 | 958 |
| 28204 | 3,219 | 2,378 | 841 |
| 16800 (complete D3D reference) | 3,193 | 2,426 | 767 |

There are 594 unresolved instance/unsupported-geometry inputs per recording;
they stay per-eye. These counts are coverage evidence, **not frame-time savings**.
The planner still takes two already-prepared native lists in this audit. Shared
upstream scene traversal/materialization has not been implemented by this change.

### Complete native reference

The initial PID27996 draw snapshot exposed a second old asset dependency:
graphics roots/tables and index-buffer hooks were missing (flags 57, tables zero).
It was sufficient for observed instance-buffer ranges but NOT geometry/binding
equivalence. `verify_mesh_plan.py` now rejects incomplete state rather than
interpreting equal zero fields as agreement. That preliminary comparison was
superseded and its report regenerated with 774 incomplete and 98 unmapped pairs.

The full graphics observers now have an independent test-process opt-in,
`CVR_STEREO_OBSERVE_BINDINGS=1`, set only by `--native-draws` through the owned
launcher. Ordinary startup clears that variable in the child environment and
does not install the additional hooks. Input-layout masks are captured from the
actual successful PSO description, including stream PSOs. Draw ABI remains
1096 bytes; reserved +1092 now stores the mask, valid only when flags bit 7 is set.
This excludes stale *unused* vertex streams without guessing which bindings matter.
Single DEFAULT-heap instances can now be checked against observed CPU-upload
bytes, without mapping the GPU resource or requiring a batch output vector.

PID16800 passed the complete ten-stage launch/test/close sequence:

- 2,803/2,803 native draw snapshots have complete state and input-layout masks.
- All 1,240 preuploaded packet ranges match actual bound native GPU ranges.
- 1,340 single CPU instances match exact recorded data; 253 are not individually
  observed because native batching can combine them (not claimed as verified).
- Of 767 proposed common packet pairs, 683 have geometry-compatible native draw
  mappings, zero geometry mismatches, and 84 remain unmapped. 190 of the 683 have
  ambiguous packet-to-batch mappings, so only 493 are unambiguous at this level.
- Native descriptor tables differ across eyes. Descriptor/resource content
  equivalence remains unverified; **zero pairs are approved for runtime reuse**.
- HUD readiness, both scene images, four-frame/repeat captures, CPU profiling,
  crash checks and control restoration all passed. Game closed, no new report.

New GPU test output: `mesh-pass-gpu-results.json`, `mesh-plan-regression-gpu.json`.
Native evidence: `mesh-plan-complete-bindings-09/`, `mesh-plan-16800.json`,
`mesh-plan-16800-native-geometry.json`, all under `build/single-pass-20260930`.
CPU tests include 5,000 random sequence plans compared with a quadratic optimal
reference, corruption/partition/order cases and each preserved/ignored key byte.

PID16800 used DLL `15b6dee239fcc188f4761fd773dcaaa658b53d17a4fb82fbc2317b16ce575e34`,
PDB `dd49258064920f1f6605a41ae449ae4f76933374c81d9c51b4801ba4fc1b17af`.
Settings preserved; no commit/push made. The next integration work is descriptor
content/lifetime validation and an earlier shared preparation owner, followed by
the real pass-wide renderer. No full-scene single-pass or overall FPS claim.

Final review added an internal-command guard around candidate PSO creation:
the native observer must not reenter its pipeline mutex from its own D3D PSO
callback. The unit test verifies internal variants do not enter the native input
layout map. Latest installed DLL is
`dffdb02c501f8acc96fcac12b699c6c9421e77fc27d3dce092ce14b7244e220b`, PDB
`70152a48460f56104cdfa8b3c1cbb1bfef08bd1db7143ec2acb52f83de11c913`.
PID23256 (`mesh-plan-final-10`) passed all ten stages and closed without a crash.
It captured 3,190 packets / 2,794 native draws, verified all 1,226 uploaded ranges,
and reset every test control. Its plan has 792 common candidates; 723 have
geometry-compatible mappings, 170 of those ambiguous, 69 unmapped, zero
mismatches. This remains geometry-only evidence, not descriptor-content proof.
Final checks: 13 CTest cases, 35 single-pass Python tests, 11 launcher tests and
16 new / 22 legacy GPU comparison cases passed. All games/captures are closed.

The subsequent descriptor-content audit and paired-resource shader conversion
are recorded in `single-pass-descriptors-20260930.md`, including the discovered
36-parameter native root signature and the newer installed DLL.
