# Single-pass stereo: current implementation and measurements

The user requested moving performance work to single-pass instanced stereo and
explicitly asked not to use old stereo experiments. Findings below come from the
current source, capture `10frames_10323.rdc`, current hardware and new tests.

## Implemented and tested

- RTX 5070 Ti reports D3D12 View Instancing Tier 3, shader model 6.1 support and
  resource binding tier 3. `tools/single_pass_tests/main.cpp` queries the adapter.
- `patch_camera.py` rewrites the real SM6.0 DXIL camera-buffer loads to select
  one of two 1024-byte camera blocks through `SV_ViewID`. It preserves draw
  instance IDs, integer camera origins and all other shader instructions. It
  rejects unsupported bindings/profiles. DXC rebuilds ViewID dependency metadata;
  the official validator checks/signs the resulting shader. This is an offline
  prototype, not a runtime shader compiler or an enabled game setting.
- Of 15 unique shaders exported from ten representative opaque/velocity/decal
  draw events, nine reference `CameraShaderConsts`; all nine rewrites validate.
  The other six do not reference that block. This does not establish that those
  six are completely view-independent: other bound constants still need review.
- `CreateViewInstancedPipeline` converts a classic graphics PSO descriptor to a
  two-view pipeline stream, preserving its fields and discarding the obsolete
  monoscopic cached PSO. The helper is in the plugin source but game rendering
  does not call it yet.
- GPU comparison uses the game's actual VS, vertex/index/instance data and both
  captured camera blocks for event 38430 (decal volume geometry), with a diagnostic
  PS that writes projected depth, instance ID and another VS output. One draw
  into two texture-array slices matches two ordinary reference draws exactly in
  19 tests: captured inputs, scaled geometry, three instances, asymmetric clip
  offsets, opposite rolls, edge-of-frustum visibility and clipping only one eye.
  Maximum covered pixels in a test: 3,737; differing pixels/channel error: zero.
  D3D12 validation reports no errors. This tests vertex/rasterization/depth/ID
  behavior, not the original material shader, complete G-buffer, motion vectors,
  temporal effects or game FPS.

## Integration constraints measured in the current capture

Both views write the same native G-buffer/depth resources sequentially:
166842, 166849, 166858, optional velocity 166863, and depth 166450. Dimensions
are 1485x1485 at the captured DLSS setting. Two array slices need explicit
routing into each eye's later native passes; merely duplicating draw instances
does not provide separate intermediate images.

Events 10042 (VRCAM) and 33519 (MAIN) use the same mesh/index ranges and VS/PS,
but instance-stream offsets are 7424 and 8192. Sharing geometry requires merging
the visible instance data, not assuming the existing draw indices are identical.
The two eyes' visibility must be preserved, including objects visible to only one.

MAIN's camera buffer 29488 is not ready at the earlier VRCAM draw: 514/848 bytes
differ from MAIN's eventual data. Its first captured GPU CopyDst is event 32905,
and further writes occur throughout MAIN's passes. Other constants also differ;
the material blocks are not automatically interchangeable. A common geometry
pass therefore needs both camera/material snapshots prepared at the correct
stage, rather than reading a previous eye/frame from a mutable buffer.

## Next integration step

`SinglePassTrace` records a bounded 1-4 Present intervals of the *actual forwarded*
848-byte camera blocks and command-list work boundaries. It observes data after
the existing stereo corrections, copies the bytes, and never retains game-object
pointers or changes rendering. It is off by default and performs no GPU work.
Its tests cover disabled operation, independent byte ownership, frame limits,
deduplication, concurrent writers, capacity and publication. This trace is for
locating the native preparation point for both eye cameras before shared geometry.

Release DLL 61330457F745F3EA687B1D1F7DD292C37FF4AAB4640BCB28D42DE482FFCC6220
was built and installed with the game closed. The previous A1305241 DLL/PDB is
archived in `build/single-pass-20260927/deploy-trace/previous`. Configuration hash
is unchanged. `SinglePassTraceRequest` defaults to zero; single-pass rendering
is not activated by this deployment. All RenderDoc controllers were shut down.
After gameplay loads, `tools/single_pass_tests/read_trace.py --pid <PID> --frames 3
--out <file.json>` captures the data and restores its controls to zero.

PID 6960 loaded that DLL successfully. A steady capture recorded 178 events/62
camera blocks without overflow. Three small HMD yaw/pitch/roll/position changes
recorded 175-178 events and 60-62 blocks each; the original pose was restored.
For the steady interval, first VRCAM geometry was event 27 and first MAIN
geometry 114. MAIN scene-camera uploads did not precede the former; early MAIN
uploads were rain-map cameras. The trace was returned to zero after every read.

Fresh static analysis of the installed EXE identifies the actual 848-byte
camera builder at RVA 0x1E2C94..0x1E3858. It has seven arguments (confirmed at 38
call sites), not merely a camera pointer. Common calls pass render context,
view+0x70 camera data, viewport dimensions, view+0xEF0 auxiliary/history data, a boolean and a
binding-stage mask. The sole direct 848-byte buffer-upload call is at 0x1E37DE.
This is fresh disassembly saved in the current build directory, not an old
experiment's conclusion. The extended trace uses current-thread unwind and
checked ReadProcessMemory to copy its live 904-byte camera input, context/view
identity, dimensions and the auxiliary/history header while the native stack is
still valid. The fifth argument is not just a clip plane: fresh disassembly
shows matrix reads at +0x30 and +0xB0..0xE0 after testing its enable byte.
Its full history layout remains to be mapped. The clip-plane value copied into
the shader block comes from the camera input itself.

Live input capture in PID 11564 succeeded (58/60 blocks had the expected native
builder frame; all scene RenderElements inputs were valid). Three continuously
moving HMD samples, with small positional jitter, captured 120-124 blocks per
two-frame interval without overflow. The HMD pose was restored and verified.
Current eye bases agree; fixed-point eye origins are reproduced exactly by the
port's existing per-eye IPD rule, including all seven stationary/moving pairs.
The two eyes use different temporal jitter, so origin shifting alone is not a
complete camera-pair construction. Matrices at CB +0xC0/+0x100 and basis vectors
at +0x2B0..+0x2E0 include history; moving captures distinguish them from current data.

## Render-view preparation hook correction

Fresh hardware write breakpoints found camera matrix recomputation at native
RVA 0x1E412C and the jitter assignment at 0x4E51AF, inside function 0x4E4AFC.
The latter builds a view's four camera variants, updates history, computes
jitter through 0x1D4B4B0, then rebuilds their matrices. The call chain goes through
GraphContextPrepare (0x79AE11 in the captured stack).

Current WeaponAim.cpp incorrectly intercepted 0x4E4AFC as Hooked_Fire. Its
unconditional argument scanner ran even in debug-off mode and bracketed render
preparation with g_fireInShot. At idle, g_fireCalls advanced 235 times in 1.5 s,
while g_ssCalls stayed zero. The false hook and its scanner were removed from
source. The actual shot-window/physical-ray hooks remain. Legacy exposed debug
values stay for ABI compatibility, but this render function no longer feeds them.
This correction is relevant to preparing camera state for single-pass rendering;
it is not claimed as an FPS gain without a live comparison.

## Early peer-camera preparation

Fresh disassembly identifies 0x1E412C as camera-matrix recomputation on the given
camera structure. It updates that structure and stack temporaries, using numeric
matrix/projection helpers; it does not submit GPU work. The trace now tests it
only on an owned, aligned copy with guard bytes.

The active native jitter path is R2, not Halton. With the observed AA-mode flags,
it rounds `counter * alpha + 0.5` as separate float operations, takes the
fraction and subtracts 0.5. Exact native multipliers are 0.7548776865005493 and
0.5698403120040894. `PeekR2Jitter` preserves those rounding steps and does not
advance the native counter. Twelve live MAIN predictions sampled before VRCAM
geometry matched the later native shader jitter and phase bit-for-bit. Results
are saved in `early-jitter-validation.json`. Unsupported AA modes remain outside
this initial path.

`PreparePeerCamera` copies the source input, applies the existing port's two
symmetric fixed-point IPD shifts, updates position/temporal fields and rebuilds
only that copy. Its unit test uses independently captured eye coordinates and
covers reverse-eye conversion, unchanged source ownership, invalid axes and
overflow. The diagnostic records an early predicted MAIN input alongside the
eventual native MAIN input. This is still comparison-only: no game target or
game camera is replaced, and no geometry pass is skipped.

The intended split is common visibility/geometry into two G-buffer slices,
followed by each eye's native lighting, transparency and temporal/DLSS processing.
No second-eye pass can be suppressed until its replacement buffers, instances,
motion vectors and current/previous camera transforms are proven equivalent.

## Reproducing the prototype

### Current limitations

This branch has no production shared-geometry switch yet. Explicitly armed probes
can record comparisons, bind a temporary twin-camera block and issue private
offscreen GPU draws. The visible scene still uses both native passes. A standalone GPU
test and a matching camera block do not prove that the native visibility lists,
materials, descriptor lifetimes, resource barriers and all geometry passes can
already be shared. No second-eye native geometry is skipped.

### Live early-camera validation, 27 September evening

The first early prediction was rejected because MAIN's derived AA flags at
view+0x17D8 are zero while VRCAM geometry is running. A short hardware breakpoint
confirmed this; the prepared source VRCAM flags were 0x517F028. Eligibility now
reads the prepared source flags and retains MAIN's own resolution and counter.
The diagnostic-only condition was temporarily changed in PID 13400 for the
measurement and restored byte-for-byte afterwards; no rendering hook changed.

Nine steady predictions and 214 moving predictions matched the later native
MAIN camera's tested matrices, fixed-point position, orientation and jitter
bit-for-bit. The twelve moving captures cover slow/fast yaw, pitch/roll, position
jitter, and combined movements. The original simulator pose was restored.
The removed false render-as-fire hook's counter stayed zero at idle.

`StereoShaderCamera.hpp` constructs the full 848-byte perspective scene block
from an owned current camera and the peer's previous camera. Native history
excludes the previous projection jitter and computes translation from the
fixed-point origin difference. A recorded 107-sample fixture agrees bit-for-bit
outside the inverse matrix; an independent DirectXMath inverse differs by at
most 4.35e-7 relative error. Runtime comparison calls the same pure numeric
inverse routine as the native builder, avoiding that rounding difference.

DLL 6C2470C8FCA4C996ADD88BAB25C79D02E3694EA8515D9CDB8054EE0DB5CC7775
loaded in PID 23888. All 18 steady full-block predictions and 54 eligible moving
ones matched every byte. All 212 moving current-camera predictions matched;
the other shader-block cases were explicitly rejected for nonzero history
weight, not counted as successful shader comparisons.

Fresh hardware watchpoints identified weight accumulation at native RVA
0x36F768 and per-view decay at 0x4E4DF6. Its integer and fractional parts decay
separately using the game frame parameter at descriptor+0x10 -> parameters+0x1B8.
The graph preparation descriptor is its second argument (verified in fresh
disassembly of 0x79ACA0 and 0x4E4AFC). `NativeHistory.hpp` models that decay without
updating the native state. The bounded trace now copies the frame delta from
the existing preparation hook, resets it on every trace-frame boundary, and
reports missing/unsupported inputs instead of borrowing an older delta.

DLL CFAFAFDEDF7ADC1A3E618B215909D7B3334181534A6ECBB29FF10FD7BB40BEDD
is installed for the next live comparison. Its record ABI is 4536 bytes and the
reader checks the exported size before arming. Previous DLL/PDB pairs are in
the `deploy-*-camera/previous` build archives. Configurations are unchanged.
All this remains comparison-only; it neither changes game targets nor skips
either eye's geometry pass. No game FPS increase is claimed.

The next run, PID 23772, rejected the assumption that the two views share the
frame delta. VRCAM used 0.033333335; MAIN used its own varying delta (e.g.
0.014464400/0.012979200). Consequently, 171/216 complete shader-block predictions
were exact, and the others included 36 cases differing only at CB+0x24C plus
nine from one frame with a small quaternion mismatch. The latter compared
q.z=-0.821262717 with -0.821261048. The attempted weight prediction and its
additional graph-preparation observer were removed rather than retained with
incorrect per-view timing.

MAIN's published source pose at view+0x1E20 is available before VRCAM geometry.
Fresh read-only sampling of 112 moving frames matched its eventual fixed-point
origin and quaternion exactly. The new diagnostic rebuilds the private camera
from this native MAIN pose instead of inferring orientation from VRCAM.

CB word 147 (offset 0x24C) remains unavailable at the early point. The generated
block marks that word invalid and stores a NaN sentinel; it must never be used
by a shader requiring the word. `camera_usage.py` conservatively records actual
legacy-CB component reads; unknown aggregate uses require all four components,
dynamic indexing requires the entire block, and unknown handle operations are
rejected. The nine unique sampled camera-consuming shaders do not read word
147. This is a restriction on shader eligibility, not proof about every shader
in the game. Shader assembly reports now include the read-word requirements.

DLL 3CCDA22C34060878A5B4E63676D8AB5A38DA77A0A66DDCE6BC39171DFE5814D4 is
installed with this native-pose path and a checked 4592-byte trace record ABI.
PID 15364 subsequently passed all 216 current-camera predictions and all 216
available-word shader-block comparisons in the twelve motion/jitter captures.
Word 147 is explicitly excluded, not silently counted as matching. The original
HMD pose was restored and the trace controls returned to zero. The user starts
the game; it was closed for deployment and the intervening RenderDoc export.
The replay controller was shut down before asking the user to launch again.

### Common targets and instance visibility

`StereoTargetArray` owns a two-slice output and records explicit per-eye copies
into a native single-slice texture. It restores both resource states, copies
both depth/stencil planes, and leaves fence/lifetime ownership with the caller.
It does not add CPU waits. WARP and RTX hardware tests each passed 40 eye/plane
comparisons over 778,396 meaningful bytes, including padded row pitches and
destination reuse, with no D3D12 validation errors. Formats cover the captured
R10G10B10A2/RGBA8 buffers, motion-vector-style R16G16, and D32+S8 storage.

The original captured-VS GPU comparison now uses this production helper to
route the single-pass output back into separate native-style 2D inputs. It also
uses real D3D12 view-instance masks for objects visible in only one eye. All 22
cases pass with zero differing pixels, including masks 0/1/2/3, asymmetric
projections, roll, multiple instances and per-eye clip planes. This validates
the prototype path, not the game's full material/lighting pipeline.

Fresh capture export confirmed events 10042/33519 have identical geometry,
shader binaries and all 64 instance bytes despite different first-instance
indices (7424 versus 8192). Non-camera constant blocks differ in unused fields;
every word read by those two shaders agrees. Other inspected pairs differ in
skinning addresses, shaders or transforms and must not be merged by mesh ID.

`StereoInstanceUnion` merges full packed instance records only after explicit
eligibility checks for identical geometry/materials, order-independent opaque
rendering, and no native InstanceID dependency. It preserves duplicate counts
and produces mask-1/mask-2/mask-3 groups. Tests include the captured 64-byte
record, distinct skinning data, one-eye-only objects, rejected ordering/ID cases
and 1000 randomized multiset round trips. This planner is not wired into native
culling or draws yet; shader resources and every other per-instance stream must
be verified before any live draw can use it.

Copy/plane API references:
[CopyTextureRegion](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copytextureregion),
[planar depth/stencil indexing](https://github.com/microsoft/DirectX-Specs/blob/master/d3d/PlanarDepthStencilDDISpec.md).

### Native pipeline and binding probe

The next step captures the game's actual classic/stream PSO description for
the proven 10042/33519 shader pair (VS FNV 02F5A04A6E812686, PS FNV
955C629FD0296DC3). `NativeStereoProbe` creates a separate view-instanced variant
using the original root signature, input layout, material PS and fixed state.
It does not bind the variant, change a native draw, or allocate stereo textures.
At most eight candidate PSOs are retained for the session.

The probe is enabled only by the exact DXC-validated file
`CyberpunkVR_SinglePassProbeVS.dxil` beside the DLL; the payload hash is checked
before use. Without that file, the four additional graphics-binding hooks are
not installed. The existing hooks' observation calls are behind the default-off
recording state. A bounded request records 1-4 frames of matching draws, owning
copies of VB/IB views, root tables/CBVs and target descriptor values per command
list. No GPU addresses or descriptor values from this snapshot are dereferenced.
Per-list state is cleared at reset and frame boundaries, and published with a
sequence counter. Snapshot memory is capped at 128 draws.

`ViewInstancedPipeline` now also supports native stream descriptions. The parser
uses the ABI alignment of each payload (scalar payload +4, pointer payload +8),
preserves native state, drops cached monoscopic PSO data, and rejects unknown,
duplicate, truncated or already-view-instanced input. CPU tests cover these
cases and independent/interleaved/concurrent command-list capture. All six test
targets pass; the 22 actual GPU comparison scenarios still have zero differing
pixels after using the stream-cloning path.

DLL 0B3CE41742E48E2C8A63F0C6FB6E7B8A1E089AFBB0E14E138E4102125129884A and
the probe shader were installed with the game closed, preserving configuration.
This deployment is archived in `deploy-native-pso`. `read_native_probe.py` checks
the 80-byte pipeline and 952-byte draw ABIs, records a bounded sample and restores
its controls to zero. Remove the exact probe asset after this integration test
when creation/binding observation is no longer needed; it is not a release asset.

PID 22812 recorded 50 candidate draws (26 VRCAM/24 MAIN), but the initial
prototype PSO was rejected with E_INVALIDARG. The native PSO uses three targets
(R10G10B10A2, R10G10B10A2, RGBA8), D32+S8, and camera b1 at root table 0 offset 1.
The VB/IB, root and MRT state were fully observed for the sampled draws.

An independent hardware device with the D3D12 debug layer reproduced the error:
PS stage validation failed for view instancing. Merely upgrading SM/DXIL metadata
did not fix it. The fresh Microsoft ViewID validator source compares the prior
output and next input signature prefixes, including semantic kinds. The captured
VS outputs ClipDistance at element/register 4, whereas PS used that position for
the generated IsFrontFace input. `link_material_pixel.py` inserts an explicit
unused clip-distance PS input and moves the generated front-face input after it.
Its executable IR is verified unchanged except that input's ID; all material
arithmetic and resource access instructions stay identical. This is a narrowly
validated adapter, not a general signature linker.

The linked material VS/PS pair passed DXC and native D3D12 creation with no
validation errors. DLL 7C1A6ACFEC30F83A82B8933E3B7233B902D5B537567F12EE64CCF1EC9DB51AF3
then created the actual native-root-signature variant successfully in PID 23976
(HRESULT 0, non-null PSO). The same 50 draws were recorded. Probe assets now
include both the VS and linked PS, each checked against its exact binary hash.
The variant has not yet been used to draw or replace native output.

Fresh source reference for the signature-link condition:
[Microsoft ViewIDPipelineValidation](https://github.com/microsoft/DirectXShaderCompiler/blob/main/include/dxc/HLSL/ViewIDPipelineValidation.inl).

### Native twin-camera upload probe

Fresh disassembly of the current EXE shows why widening the existing upload
call would be unsafe. Dynamic CB mapping at RVA 0x1F0B1C allocates by the original
buffer record size, ignoring the requested map byte count for constant buffers.
An attempted 2048-byte copy into the native 848-byte camera's allocation would
therefore overrun it. RVA 0x1EE3CC is also not a general handle allocator: it
uploads and binds a specific slot/stage and must not be repurposed as one.

The new opt-in path instead calls the native sized upload helper 0x1F0114 with
2048 bytes and an owned CPU-only CBV descriptor. That helper allocates aligned
upload-ring space, copies the exact byte count and creates the descriptor with
the aligned requested size. The normal camera bytes occupy [0,848), and the
prepared MAIN bytes occupy [1024,1872). The descriptor is observed at the real
CreateConstantBufferView call and checked for size/alignment before use.

After the corresponding native b1 binding, the bounded probe uses the same
native descriptor-binding helper as the game (0x1F3978, six arguments), retaining
the current stage mask and render context. It does not enlarge a registered
native buffer. Original shaders still read the unchanged first camera prefix;
the view-instanced PSO remains unbound. The feature requires an accepted native
prototype, defaults off, only runs within an armed scene-camera trace, caps its
CPU descriptors at 128 per process and allocates no separate GPU texture/buffer.
Descriptor callbacks and counters are disabled after the trace.

DLL B9A327A516FFBD39A6B9D2A8C81AC33C1CD9770AB8D0B0E8CBF7E50491100706
was installed for the next live upload/binding check, retaining the probe shader
assets and configuration. Trace ABI is 4616 bytes. `read_trace.py --native-upload`
arms the path only for its bounded recording and clears the upload flag in
finally. PID 17636 completed eight 2048-byte uploads and eight corresponding b1
bindings without failures. All eight current-camera and eligible shader-block
predictions still matched; a native simulator screenshot afterwards showed both
eyes and HUD rendering normally. This screenshot does not independently sample
the exact modified frame.

### Live GPU geometry and native instance data

`StereoGpuProbe` adds an explicit, default-off 256x256 offscreen test using the
real native mesh, material bindings, linked view-instanced PSO and twin camera.
It initially compares a mask-3 draw with mask-1/mask-2 draws of the same PSO.
Three G-buffer targets and both depth/stencil planes are copied into readback.
Private resources are retained with `CommandResources`; completion is observed
through a queue fence without CPU waits. PSO, targets, viewport, scissor and view
mask are restored. An internal-command scope excludes private resources/commands
from the port's normal target/depth observers and avoids recorder reentrancy.

The standalone `scene_probe_test` executes the production probe code on hardware,
verifies 2,228,224 matching bytes with nonempty views, and verifies a following
native-style draw against an untouched reference to check state restoration.
D3D12 validation reports no errors. Its camera source is a test adapter; this
does not replace the separate live test of the native upload function.

PID 24164 (DLL 0B2E7C7FCC062FE460C0E5D78EBE647F7DE173109A271B0CB5FD042EDA847FCD)
executed the first actual in-game offscreen view-instanced draw. All 2,228,224
compared bytes matched. VRCAM/MAIN layer coverage was 306/309 pixels, and 18 twin
uploads/bindings had zero failures. This proves the split-view-mask versus
combined-view-mask path for the real material; it is not yet a comparison to the
later native MAIN material bindings, nor a replacement of visible rendering.

Fresh IA binding disassembly at RVA 0x1F6CB0 identified native buffer metadata
and its resource pointer. A current-thread unwind now reads that pointer while
the native IA binder is using it, checks the actual resource/heap with D3D12,
and retains only upload resources whose VA/range match vertex slot 7. At each
candidate draw, a bounded Map/read/Unmap copies the 48/64-byte instance record.
CPU-only snapshots need no GPU fence wait. Resource references are released on
list reset, trace-frame boundaries, or the next Present after forced trace stop.

PID 17596 (DLL CA2D9FDE4084D05F65104162793047ABC25A2AF026FFC1E5D27AAA561C7349FE)
copied all 50 candidate instance records. In each of two frames all 12 MAIN
draws had exact geometry/instance matches among 13 VRCAM draws; one VRCAM-only
draw remained unmatched. First-instance indices differed, while all 64 data
bytes agreed. Materials and shader resources are a separate remaining check.
`analyze_draw_pairs.py` preserves multiplicity and reports this limitation.

A hardware-breakpoint inspection produced the fresh IA caller snapshot. Some
queued thread stops persisted after deleting the breakpoint; x64dbg was detached
and advancing simulator frame counts verified that the game resumed. Prefer the
bounded in-process unwind capture over further high-frequency hardware stops.

### Current native-reference comparison build

DLL BD2EC19FFB168FC9E39DBB2987F9DC19AD58611B4B98D9218F0D89696912C789 is
installed (`deploy-native-reference`), with trace ABI 4616 and draw ABI 1088.
It extends the offscreen test with an optional real-reference mode: original
VRCAM shaders draw reference layer 0; a matching later MAIN draw supplies its
own original shaders, camera and material bindings for reference layer 1.
Matching requires the same frame, native pipeline/root, geometry and full
instance record. The MAIN reference is emitted only if the producer's GPU fence
has already completed; otherwise it is skipped, without a CPU or GPU wait.
State 6 reports an incomplete native reference, not success.

Both split-mask and native-reference modes pass the standalone GPU test with
state restoration and zero validation errors; all six CTest targets pass.
PID 20916 completed the live native-reference check: all 2,228,224 bytes matched,
coverage was 308/310 pixels, and the native MAIN reference required no skipped
attempt. Nine native twin uploads/bindings had zero failures. This compares the
real late MAIN bindings, but only the selected object/material at 256x256; it
does not prove all shaders or full-resolution scene output. The script restored
all recording/upload controls. All visible native draws remain.

### Native packet preparation and GPU ordering

A subsequent slow HMD/position-motion attempt in PID 20916 ended as incomplete
(state 6, one skipped reference), not a pixel mismatch. The producer had not
passed the old CPU-completion gate before the matching MAIN draw. Earlier result
bytes remained in the diagnostic exports; the reader now emits null results for
incomplete/error states, and the new probe resets metrics at each start.

The new reference path accepts an already-submitted producer without waiting for
completion on CPU. At actual MAIN submission, the same command queue naturally
orders the reference; a different queue enqueues a wait for the producer fence.
The wait is installed before ExecuteCommandLists, never afterwards. If the
producer has not been submitted yet, reference capture is still skipped.
The hardware test deliberately blocks the producer behind a GPU-only gate,
records/submits MAIN while it is unfinished, then releases that gate. Both the
same-queue (zero extra waits) and two-queue (one wait) tests compare all 2,228,224
bytes exactly and report no D3D12 validation errors. This dependency is only in
the opt-in offscreen test. See [D3D12 queue Wait](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-wait).

Fresh EXE disassembly maps RenderElements (0x23A938) to the sorted packet
consumer 0x1F1208. Its two scene paths are a prepared list at 0x23AEA2 and a
local gather/filter/sort at 0x1D572F1. The other direct callers, 0x378DBD and
0x77C111, also pass the same three integer/pointer arguments and are excluded
by the scene-node gate. Packets are 16 bytes. A native plane contains 28 linked
worker lanes, collected by 0x15375C; reading their pointers later is not a safe
way to identify one eye's completed input.

A read-only inventory in PID 20916 found both view+0x1E10 pointers referencing
the same storage (0x17729906900), with contents changing between reads. The new
NativeGeometryPackets observer therefore copies the owned input span at native
consumer entry instead. It records both prepared and locally sorted paths,
preserves duplicates and packet order, and never calls the consumer twice or
modifies its input. A current-thread unwind identifies the 48-byte section
definition from the live RenderElements frame. Addresses are diagnostic IDs,
not pointers retained for future rendering.

The observer requires the private probe assets and an exact native prologue;
capture defaults off. A request captures 1–4 Present intervals, capped at 256
calls, 32,768 packets per call, and 262,144 copied packets in total (4 MiB).
Invalid/oversized/unreadable inputs are recorded as failures without accessing
them through raw dereferences. Capture tests cover duplicate ownership, guard
pages, malformed spans, multiple writer threads, stale completion tickets and
both capacity limits. All seven CTest targets pass. The record ABI is 216 bytes.
Use read_geometry_packets.py and analyze_geometry_packets.py for the next live
capture. Raw packed-handle equality alone does not authorize skipping draws;
mutable data, materials, visibility and output routing remain separate checks.

DLL 4D534D667E48557246503AA9E9096C4DB102FADB64547FD3B769ACC947FDD83C
was installed on 2026-09-28 (`deploy-native-packets-20260928`), after closing
PID 20916. DLL/PDB hashes were verified and both configuration/calibration files
were preserved. Live validation of this packet observer and the revised GPU
dependency is recorded below. No visible native draw is skipped yet.

PID 9184 validated this build. A one-frame snapshot copied 2,992 packets in 27
consumer calls (12 VRCAM, 15 MAIN), including four empty spans; no overflow or
unreadable input occurred, and all 27 native section definitions were recovered.
Ten plane categories were observed. Both views again referenced the same view
scratch storage, while every raw packed packet differed across eyes. These are
temporary table indices, not evidence that every mesh/material is different.

Four native-reference GPU comparisons passed in PID 9184: slow movement, about
30 degrees/sec yaw, another slow sample and up to 120 degrees/sec yaw. The latter
includes pitch/roll and 7 mm position motion. Each compared 2,228,224 bytes with
zero mismatches, zero skipped references and zero extra queue waits. Total twin
uploads/bindings were 72/72 with zero failures. The simulator's original pose was
restored after both motion runs. Results live in native-reference-motion-9184
and native-reference-motion-fast-9184. This still covers the selected material
in the private test targets, not every scene shader.

Fresh consumer disassembly resolves the five native index fields to: a 64-byte
draw entry, a six-byte shader entry, a 40-byte state entry, a 24-byte object
entry and 48-byte transform records. The table pool is loaded from
[global at EXE+0x3427C00]+0x4628; the renderer argument must match global+0x80.
A read-only live check confirmed that identity in PID 9184. For skinned packets,
0x1F1A88 appends 16 bytes from object metadata+0x14 to the instance transform.

The next opt-in resolver copies these entries at the consumer call itself.
It does not interpret reused table contents after the eye finishes. Its owned
216-byte input snapshots are allocated only on request, capped at 65,536
packets (13.5 MiB), and released after the control is disabled. Native command
programs are explicitly reported; their payloads are not yet resolved. Multiple
instances remain incomplete rather than being represented by one transform.
The comparison removes only the decoded table indices and preserves all other
packet bits, including sorting and render flags, as well as multiplicity.
Matching copied entries is still insufficient to skip rendering: referenced
GPU buffers/material resources and pass routing must also be verified.

The resolver's tests cover exact table strides, independently owned bytes,
single versus multiple instances, missing shader/skin memory and pointer
overflow. The recorder test also verifies that a missing native pool fails
without dereferencing it and that optional storage is released when disabled.
Four Python comparison tests verify temporary-index normalization, duplicate
multiplicity, retained sort/render flags, changed skin data, and rejection of
unresolved command programs. A simulator screenshot after the four live GPU
tests showed both eyes and HUD rendering normally (singlepass-packets-9184.png).

DLL D1F64F951AD09AB8EEC7C60384BCC18339E663B1E790F64D65FB4F97E71689E0
is installed (`deploy-resolved-inputs-20260928`) after closing PID 9184. DLL/PDB
hashes were verified and configuration hashes are unchanged. Its optional input
resolver has not yet been exercised in the game. After the next user launch,
run read_geometry_packets.py --resolve-inputs --frames 1, then the comparison.
The resolver flag defaults off and the reader clears it in finally, releasing
the transient input storage on the next Present.

### Complete instance streams and actual binding validation

PID 16776 copied all 2,992 table snapshots without a read/allocation failure.
The initial recorder marked 1,660 single-instance snapshots complete; this was
only table-read coverage. Fresh consumer analysis found two cases where the
earlier pool-matrix interpretation is insufficient, so those old flags must not
be interpreted as complete render inputs. Updated offline analysis rejects them.

For batched non-skinned packets, first-word bit 59 selects instances already
uploaded to the buffer at renderer+0x200; second-word bits 33–49 are a GPU first
instance index in this path, not a pool transform index. The native IA binder
uses the resource registry at EXE+0x3438A28. PID 16776's buffer was 6 MiB with GPU
VA 0x2EF800000 and no CPU mapping. Draw-kind 2 also postprocesses its matrices to
face the view, and remains unsupported. Regular CPU batches copy all 48-byte
transforms; skinned batches append 16 bytes to each. Native 0x1F1DB2 confirms the
null-skin fallback is the integer tuple (0,0,0,1).

The state program field is a byte length: observed lengths 17,23,28,31,37,43
contain an eight-byte header followed by opcode/length/payload records. The
resolver now copies and bounds-checks their complete contents. Opcodes 13/14
are object-dependent resource/constant bindings (fresh 0x3A0BFC/0x3A09B4), not
proof of equivalent shader resources just because the program bytes match.

The new instance resolver supports complete regular CPU batches and the actual
preuploaded buffer range. For an unmapped target, an opt-in observer copies CPU
upload bytes when CopyBufferRegion records the write to that exact resource.
It owns the copied bytes and tracks initialized ranges. Unknown source heaps,
whole-resource copies, holes, wrong identities and UAV-writable destinations do
not yield valid shadow data. The observer issues no GPU copy, GPU readback or
fence wait, and stops when the bounded recording finishes. This is diagnostic
upload evidence, not authorization to replace rendering or assume arbitrary
unobserved GPU writes cannot exist.

An optional all-scene draw snapshot records actual instance buffer bindings,
including DEFAULT-heap resources without trying to Map them. It retains the
native caller thread so each packet can be compared within its own consumer
call. GPU address/resource/range checks and available exact CPU instance bytes
are compared by verify_packet_instances.py. Draw ABI is now 1096 bytes, capped
at 4096 records; input ABI is 256 bytes, with an owned variable payload. Input
records are capped at 16 MiB, payloads at 32 MiB and the upload shadow at 16 MiB;
all optional dynamic storage is released after recording/readout.

New tests cover complete multi-instance streams, CPU versus preuploaded source
selection, native skin fallback, malformed programs, view-facing exclusion,
shadow holes/overwrites/resource identity, and actual D3D12 upload mapping on
WARP. Native-reference GPU tests still compare 2,228,224 bytes exactly on one
queue and across two queues, with no validation errors after the ABI extension.

DLL 19BCAE8F3968030C5CF1E67423591BCF6EFA5B24501C9255DD155930C2B829AD
is installed (`deploy-complete-packets-20260928`), replacing D1F64F95 after
closing PID 16776. Configuration/calibration hashes are unchanged. Live
validation of complete streams/upload shadows is pending the next user launch.
Run read_geometry_packets.py --resolve-inputs --native-draws --frames 1,
then analyze_geometry_packets.py and verify_packet_instances.py on its JSON.
Unsupported or unobserved data must not be used to skip a native draw.

PID 6760 validated the complete-stream capture: 27 native calls, 2,994 packets,
2,699 actual draws, and 624,477 bytes of owned variable payload, without overflow
or capture failure. There were 2,754 complete input streams, 232 intentionally
unsupported view-facing/special draws and eight preuploaded ranges not observed
in this recording's CPU uploads. Every one of the 1,371 preuploaded packet
GPU ranges/resources matched a real draw in the same native call/thread.
Another 1,213 single CPU instances matched actual bound instance bytes exactly;
178 were not observed as separate single-instance draws (native batching is not
treated as a mismatch). Complete packet comparison found 462 exact input pairs,
while retaining sort/render bits and duplicate multiplicity. Material resource
contents beyond the copied records remain a separate requirement.

The validated material's 13 VRCAM draws and 12 MAIN draws all occur in one
plane-6 packet-consumer call per eye. The same native command-list object is
reused between eyes. The next GPU probe therefore aggregates the entire group
of this original PSO in those calls, rather than selecting one matching object.
Original mono draws accumulate into each reference layer with their actual late
bindings, and the early common draws accumulate into both shared layers. No
geometry-equality filtering can hide MAIN-only objects in this comparison.
Only one native command list per group is accepted; submission in the middle
of a group or incompatible state makes the result incomplete, not a success.
Private copy-to-readback transitions are restored before further group draws.

Hardware tests compare overlapping objects with different constants and depth:
the matching three-object groups have zero differing bytes; adding a visible
fourth object only to MAIN produces 136,370 differing bytes, as expected. The
same-list/new-allocator reuse case also passes while the producer is still
waiting on the test's GPU gate. Native state restoration and D3D12 validation
pass, as do the original split/native-reference and cross-queue cases. Visible
game rendering is still untouched; this validates a common material group in
private targets before any replacement of the scene pass.

DLL 13E02512D7E05A81C71FEE5A9B225721F2B5055116CA031CF553C4E04023A19E
was installed for that group test. PID 13532 completed it with 13 source draws,
12 native MAIN references, no skipped references, zero extra queue waits and
18 successful twin uploads/bindings. Unlike the earlier one-object test, the
complete group differs by 1,468 of 2,228,224 bytes. This is not a passing image
comparison. Raw snapshots identify exact geometry/instance matches for source
ordinals 0–11 with MAIN ordinals 0–11, and one VRCAM-only object at ordinal 12
(7,236 indices). The same mapping appeared in all three recorded intervals.

The next test build records differences per eye and target plus 128 pixel
samples. It also accepts bounded per-draw visibility masks for the private
group experiment: MainViewMask selects which source draws reach layer 1, while
MainReferenceMask can isolate the corresponding native references. Defaults
include all draws; the reader restores them after each test. These ordinal
masks are diagnostic controls, not a production visibility solution. Source
and native MAIN group sizes remain recorded even when an output is masked.

Hardware tests pass both an eye-exclusive object (3 source / 2 MAIN draws) and
an isolated shared pair within a larger group. The intentional extra-MAIN
object still reports differences in MAIN only, with pixel samples. Before
attributing the live difference to the extra object, compare the unmasked
result with --main-view-mask 0xFFF. The private group excludes other scene
materials/occluders, so a difference here does not alone prove a visible scene
artifact or justify changing native visibility.

DLL 2810B6EA5165BD1A4C33E76CE5FEBA1FFE73AE947081F068D7E68DE76EF0043D
is installed (`deploy-group-masks-20260928`) after closing PID 13532; settings
and calibration hashes are unchanged. Live mask validation is pending. First
run the unmasked --native-reference --group comparison to capture per-eye
differences, then rearm with --main-view-mask 0xFFF in the same stable scene.
If differences persist, paired source/reference masks can isolate individual
matched objects. The default masks are restored in the reader's finally block.

PID 17012 confirmed the visibility-mask hypothesis for this private group.
Unmasked output differs by 1,466 bytes, all in MAIN (target differences
444/356/222/444; VRCAM has zero). With MainViewMask=0xFFF, all 2,228,224 bytes
match, with 13 source draws and 12 native references, zero skipped references
and zero extra queue waits. The excluded source ordinal is still rendered for
VRCAM. This does not establish a production visibility mechanism or prove that
the extra object would survive other scene occluders.

The next bounded diagnostic asks whether the corresponding MAIN packet consumer
can be reached on CPU while VRCAM GPU commands are pending. PrepareGateMs is
off by default, clamped to 50 ms, and only applies to an armed private group
probe. Before the source command-list submission it queues a wait on an owned
fence. Entering the matching MAIN plane signals the fence from CPU (reason 1).
An independent short-lived CPU worker signals it after the requested timer
interval (reason 2), regardless of render-thread progress; queue failure reports
reason 3. Start, MAIN-entry and release QPC timestamps are exported. This is a
one-frame dependency experiment, not a pacing/performance setting.

Hardware tests exercise release by MAIN and by the watchdog, with the normal
group image still exact and no D3D12 validation errors. Visibility masking and
same-command-list reuse remain exact. A failed reachability test would only
describe this chosen consumer point, not prove that earlier MAIN culling data
cannot be prepared elsewhere.

DLL 9E30DD3C6EC071A916F88599D46AB1FD64B8A8C56AE5703F907D8ED85BEE94B2
is installed (`deploy-prepare-gate-20260928`) after closing PID 17012. Settings
and calibration hashes are unchanged. The live reachability test is pending;
run --native-reference --group --main-view-mask 0xFFF --prepare-gate-ms 50 in
the same scene, then inspect prepareGate.reason/mainMs/releaseMs together with
the group pixel comparison. The gate control is restored to zero afterwards.

PID 16912 reached the corresponding MAIN consumer 3.7238 ms after the source
submission gate began and released it from CPU at 3.7239 ms (reason 1); the
50 ms watchdog was not needed. The masked 13/12-draw group still matched all
2,228,224 bytes, with no skipped references or extra cross-queue waits. This
supports trying current-frame matching before GPU execution, rather than using
the previous frame's visibility.

The next private-group mode matches actual MAIN draws against owned VRCAM draw
snapshots, consuming each match once. A small upload buffer stores the match
flags. For each source draw, complementary D3D12 predicates select either a
view-mask-3 common draw or a view-mask-1 VRCAM-only draw; only one executes.
Unmatched MAIN draws use their original shaders/bindings in the shared MAIN
slice. Native references remain separate for comparison. The source GPU gate
is released after the MAIN group finishes filling flags, not at its entry.
Publication and release share a mutex, and an sfence precedes the CPU fence
signal. A watchdog release rejects the incomplete comparison and stops late
writes; it is never reported as a passing common draw.

Native predication is observed in optional command-list slot 55. Already
predicated native draws are excluded, and private predicates are reset before
copies and following native draws. Direct-list Reset begins unpredicated, as
specified by [D3D12 predication](https://learn.microsoft.com/en-us/windows/win32/direct3d12/predication).
The upload heap is legal for the predicate buffer; the selected operation means
"skip when equal", not "execute when equal".

Hardware tests pass automatic eye-exclusive visibility and a MAIN-only native
fallback with zero image differences and native state restored. An intentional
material change with unchanged geometry produces 230,522 differing bytes, so
geometry matching alone is not presented as material equivalence. The watchdog
case returns incomplete state 6; native predication tracking and internal-command
isolation pass their CPU test. These are still private GPU targets, not visible
scene replacement or a measured FPS improvement.

DLL 1E79BAD116E38DE3ACE5169D3FBF1290F6B863B8441B273A1227EFAC1726BF59
is installed (`deploy-late-visibility-20260928`) after closing PID 16912. Settings
and calibration hashes are unchanged. Live automatic-matching validation is
pending: use --native-reference --group --late-visibility --prepare-gate-ms 50
with the default masks, then check matchedDraws/fallbackDraws, gate release and
per-eye image differences. Matching currently compares geometry and complete
single-instance data; a passing image comparison remains necessary because it
does not independently establish every material/resource dependency.

PID 18312 validated automatic visibility without manual masks: 12 source /
12 MAIN draws, all matched, no fallback, and all 2,228,224 bytes equal. MAIN
entered after 4.1191 ms and released the preparation gate after 5.8121 ms.
Slow and fast HMD tests also matched exactly (12/12 and 10/10 draws); the fast
case includes roughly 120 degrees/sec yaw and 7 mm positional jitter. All three
comparisons had zero twin-upload failures. Original simulator pose was restored.
Files: `late-visibility-live-18312.json`, `late-motion-18312/summary.json`.

Fresh native draw records show the candidate material forms a contiguous prefix:
PID 6760 VRCAM consumer 6 has candidate ordinals 0..12, MAIN consumer 18 has
0..11. These are packet-consumer boundaries, not a claim about every scene.

After closing PID 18312, the supplied 10323 capture was reopened briefly and
closed again. Draws 10042/33519 have three cleared MRTs (RGB10, RGB10, RGBA8),
but their depth/stencil already contains the native depth prepass. Blindly
copying an independently cleared common group would overwrite that depth.
The actual policy is reverse-Z GREATER_EQUAL with depth writes; stencil is
ALWAYS/KEEP/KEEP/REPLACE, full mask, reference 0, both planes writable. The
generic MCP stencil enum label was wrong; raw D3D12 state plus RenderDoc's
current CompareFunction definition identifies value 1 as AlwaysTrue.
`capture_group_state.py` saves raw API state, root layout and resource history
and shuts down its replay controller in finally. Evidence: `group-target-state.json`.

The next opt-in test (`--scene-depth`, requiring group, native reference and
late visibility) renders the common group at native resolution and resolves
its colour/depth into a private copy of each eye's actual initial targets.
Native reference draws use a separate copy of the same initial targets. A
fullscreen resolve loads the common MRTs and depth, applies native depth/stencil
testing, and preserves uncovered pixels. Stencil 255 is reserved as the private
untouched-pixel marker; source reference 255 and unsupported blend/stencil/depth
policies are rejected. Both depth and stencil planes are imported explicitly.
Only the contiguous candidate prefix participates; reentry after another PSO
rejects the result. Original visible draws continue normally.

The native root has 36 descriptor tables. The existing 32-table exported draw
ABI is preserved for readers; a separate bounded state tracker saves/restores
up to 64 tables, both descriptor heaps, topology and stencil. Incomplete root
state, descriptor rewrites, read-only DSVs or mismatched target dimensions are
rejected. Descriptor metadata retains no resources permanently; resources are
retained only for the requested frame and its GPU completion. All extra draw
state recording remains behind the existing bounded diagnostic request.

Validation: 11/11 CTest cases pass. The new resolver matches 5,105,304 bytes on
both WARP and the NVIDIA GPU across 12 cases (two eyes, depth 0/0.6/1, occluders,
equal depth, alpha discard holes, overlapping draws, nonzero stencil and an
empty group). The complete production probe, with different native initial
depth per eye, matches all 870,400 bytes and restores following native draws;
no D3D12 validation errors. Existing cross-queue, visibility, fallback, deliberate
material mismatch and watchdog tests still pass. A separate state test exercises
all 36 tables, heap invalidation, reset and descriptor replacement.

DLL BC650E146D7EAC5B6820222D7D74ACD78C2646B86E1C8C1DEEF93FD55C3FD1E1
was installed for this full-resolution/native-depth check. It does not enable
visible single-pass replacement or claim an FPS gain. Run the live scene probe
before deciding to suppress any native draws. SceneDepth and its buffers are
off by default; the reader clears controls in finally.

PID 10392 passed the ordinary automatic-group comparison on that DLL: 12/12
matched draws, all 2,228,224 bytes equal, gate release 5.534 ms. SceneDepth was
not activated. Final review found that descriptor heap changes also invalidate
compute tables; the next build preserves those alongside graphics tables.
Its GPU regression test binds compute CBV/UAV tables before the resolve, then
dispatches afterwards without rebinding either table or heap. Both eye paths
produce the expected values (103 and 203), and the depth-aware image comparison
remains exact with no validation errors. Unknown/non-table compute roots reject
the optional scene comparison instead of leaving partially restored state.

DLL 99480FE9AED6647800FC542967D8B22DE34C06B63EE46803471555D0CCE331C9
contains this additional restoration. PID 10392 was closed for deployment;
the full-resolution live test remains pending. Settings and calibration are
preserved. It remains a bounded private comparison, not visible draw suppression.

PID 25420 passed the full-resolution native-depth test: 1485x1485 per eye,
12 source / 12 MAIN draws, 12 automatic matches, no fallback. All 74,977,650
colour/depth/stencil bytes match; gate release 5.4208 ms. Slow and fast HMD
motion plus positional jitter also pass with zero differences, releases
5.5970/5.6771 ms. No upload failures, skipped references or extra queue waits.
The simulator's original pose was restored. Evidence: `native-scene-depth-25420.json`
and `scene-motion-25420/summary.json`.

The next opt-in `--scene-route` step routes the resolved group into the real
native targets. Original native draws are still recorded under a complementary
GPU predicate. A frame-wide flag initially selects native fallback; it changes
only when MAIN finishes validating the group before the preparation gate opens.
On watchdog expiry or an incomplete group, originals execute and copy-back is
skipped. This uses the documented D3D12 predication support for both draws and
CopyTextureRegion. Reference draws still execute separately for verification,
so this diagnostic version is not an FPS optimization or an FPS measurement.

In route mode, the pixel comparison reads back the actual native targets after
copy-back, not only private resolve surfaces. An occlusion query measures passing
resolve samples in each eye to exclude a vacuous comparison of untouched native
clear colours. The native draw ABI returns whether it has already emitted the
conditional original; the outer hook then avoids issuing a duplicate.

GPU tests pass exact native output for scene routing and preserve subsequent
native graphics/compute bindings. The forced watchdog test keeps native draws,
does not commit routing, returns incomplete state 6 and preserves the image.
Ordinary, cross-queue, command-list reuse, eye-exclusive visibility, MAIN-only
fallback and intentional mismatch regressions still behave as expected.

DLL B7FC4E8DE1E82E51EDEE85407B53F668E824CA35E4E1F98A57B5A667784C51B9
is installed (`deploy-visible-route-20260928`) after closing PID 25420. Settings
and calibration hashes are unchanged. First run --scene-depth with native
references, group, late visibility and a 50 ms preparation gate to verify passing
samples. Then rearm with --scene-route. Success requires committed=1, all selected
native draws routed in both eyes, nonzero passing samples and zero differences
against the actual native targets. Routing remains off by default and lasts only
the requested group/frame. No performance gain has been measured yet.

PID 19576 passed the visible-route test. The native fallback draws were
conditionally suppressed for all 12 source and 12 MAIN draws (committed=1).
All 74,977,650 bytes read from the actual native outputs matched the separate
native reference. The resolve passed depth/stencil for 30,409/48,007 samples,
so the result is not merely untouched clear/background data. Slow and fast HMD
tests also routed 12/12 draws with zero differences and nonzero passing samples
(36,329/57,092 and 38,703/61,108). Original pose and all controls were restored;
the native simulator screenshot after testing shows both eyes and HUD normally.
Files: `visible-route-19576.json`, `visible-route-motion-19576/summary.json`,
`build/render-perf-20260927/singlepass-visible-route-19576.png`.

The next build adds optional GPU timestamps around native reference draws,
common draws, native-target import, depth-aware resolve, copy-back and diagnostic
readback. Timers are off by default, capped at 256 ranges, retained through queue
completion and converted using each command queue's timestamp frequency. The
test-only reference import and readback costs are explicitly labeled; these
numbers describe this instrumented material group, not whole-game FPS.
Production-path GPU tests verify range counts, both queue frequencies, exact
native scene output, restored graphics/compute state and watchdog fallback.
DLL 927E3B0867BECAFE47039821C05705661B7D83BDDCDF903B883D5CA27CFFEF4C
is prepared for measurement with --scene-depth --scene-route --gpu-times.

PID 24872 completed four timed routed comparisons, each exact at 12/12 draws.
Discarding the first sample, median sums are: native reference draws 0.261312 ms,
common draws 0.181664 ms, reference+resolve imports 0.209568 ms, resolve 0.092448 ms,
copy-back 0.103136 ms. Diagnostic readback is 14.37344 ms and is not part of a
production common-pass design. No timing ranges were dropped; both queues report
1 GHz timestamps. The common draws alone are about 30% cheaper, but the copied
output route does not yet provide a net improvement for this small group. These
are instrumented-group costs, not a whole-frame benchmark or FPS gain.
Evidence: `route-timing-24872/summary.json` plus four raw run reports.

The next route mode (SceneRoute=2, --direct-resolve) binds owned RTV/DSV descriptors
for the real native targets and applies the same depth-aware resolve directly.
It removes the private resolved target pair and copy-back. Only the reference
copy remains for verification; timings label it importReference. The same frame
predicate selects direct resolve versus native fallback, so incomplete MAIN or
watchdog expiry leaves the original group intact. Actual native-output readback
remains the comparison source. Hardware tests pass exact direct output and forced
watchdog fallback, including the following graphics draw and compute dispatch.

DLL F0F11E12E9444D9876EB72C839FAACE5DCB7FAE07D61AD293A7D40E0B3832A44
is installed (`deploy-direct-resolve-20260928`) after closing PID 24872, with
settings and calibration unchanged. Direct-mode live image/timing validation is
pending. Use profile_gpu_group.py --direct-resolve for a bounded four-sample
comparison; all reference/readback machinery remains diagnostic and no continuous
single-pass mode has been enabled.

PID 25572 passed all four direct-mode timed runs (12/12 replaced native draws,
zero image differences). Median native reference 0.242592 ms, common draws
0.196544 ms, direct resolve 0.091104 ms. Copy-back is absent, and verification-only
import is 0.116480 ms. Thus common+resolve is still about 0.045 ms more expensive
for this small material group, before other production costs; do not present this
as an FPS gain. Direct-mode slow/fast head-motion tests also pass exact output,
with 36,221/50,545 and 37,305/52,008 passing samples. Pose and controls restored.
Evidence: `direct-timing-25572/summary.json`, `direct-motion-25572/summary.json`.
Next inspect adjacent materials from the supplied capture to amortize resolve
across a larger group. PID 25572 was closed before offline RenderDoc inspection.

Fresh capture inspection (`capture_adjacent_materials.py`) found the immediate
next VS 143078 / PS 143079 run: events 10081..10193, 27 draws / 51 instances,
with identical writable MRT/depth policy and stencil reference 0. Event 10200
changes the reference to 20; that marks the end of the compatible prefix even
though the PSO/shader pair remains the same. The capture controller was closed.
The new static VS reads the same validated camera words and not unknown word 147.
Its SM6.1/ViewID rewrite is DXC-validated and both the native and instanced linked
pipelines are accepted by the hardware driver with the existing material PS.

Optional `CyberpunkVR_SinglePassStaticVS.dxil`: original VS length 5238 / FNV
7BD6CC7EF414F11A, patched length 8946 / FNV 0D370481852CBA69 / SHA256
DDBD96F78389FE72F0B87435D2A8AEF8459E11E7D3A629E71A11C9B20455C003.
This remains a private capture-derived test asset, not a redistributable file.

--mixed-materials can accumulate adjacent validated pipelines with the same
targets/root/viewport and stencil reference into one common group. It stops
at an incompatible material or stencil change; later draws remain native.
Source draws use their own instanced PSOs, and matching still includes original
PSO identity. Group capacity is 128 and the frame predicate occupies a separate
slot after all per-draw flags; timestamp capacity is 512 ranges. Stream PSO sample
counts are now decoded rather than reported as zero. Full instance-array equality
is not assumed for unreadable/batched inputs: unmatched MAIN draws keep fallback.
The mixed GPU test deliberately changes culling in the second PSO and confirms
exact output with the correct pipeline for every draw. Direct routing, watchdog,
legacy visibility, native probe and pipeline-stream tests also pass.

DLL 2DD805EED885AC1AAF74BB512EDF9CB94C39FA1B32A555F54882F09E43DD3A73
is prepared for the mixed-prefix test. Begin with --scene-depth --mixed-materials
without --scene-route, inspect group and fallback counts and pixel equality, then
only consider a routed/timed comparison. RenderDoc is closed and the game remains
closed pending deployment/manual launch.

PID 20712 mixed private comparison passed: 106 source / 33 MAIN draws,
22 exact geometry+instance matches and 11 native MAIN fallbacks; 74,977,650 bytes
equal, about one million passing resolve samples per eye. The following mixed
direct-routing test did NOT pass: 6,407 differing bytes, all MAIN colour (3,067 /
2,529 / 811 by MRT), zero depth/stencil differences. The profile batch stopped
after that first failure and cleared controls. Do not claim the expanded group
is validated or enable it continuously. Investigate whether this is temporal
material data, matching/order, or the direct path with the native fallbacks.
Files: `mixed-depth-20712.json`, `mixed-direct-timing-20712/run-0.json`.

User then authorized autonomous game launches and requested QuickBoot plus a
launcher visibility switch, and explicitly requires closing the game after
tests. This supersedes the earlier manual-launch-only preference. PID 20712
was closed. QuickBoot/launcher work is being completed before further render tests.

QuickBoot, startup window switch, bounded message-pumping delay, and physical
window-size guard are now implemented and validated (see quickboot-20260928.md).
Current DLL includes that startup work; render controls remain default-off.

On a normal 2560x1440 startup (PID 23512), repeated mixed comparisons showed:
private-1 6,438 differing MAIN colour bytes; copied route 6,535; direct route
6,451; private-2 zero. First three had 106 source / 33 MAIN, 22 matched / 11
fallback; the final private test had 106 / 55, 51 matched / 4 fallback. Thus this
is not established as a direct-resolve-only bug. Every mismatch still leaves
depth/stencil exact. All controls were cleared and the game was closed.
Evidence: `mixed-repeat-23512`.

An additional lead is native VRS. Saved raw capture state for both 10042/33519
has base rate 1x1, combiners [0,1], shading-rate image ResourceId::166864. Colour
differences include repeating pairs in the native reference at x304..311/y1312,
while shared colours vary per pixel. VRS image identity does not establish equal
contents at both eye times. This is a hypothesis, not a confirmed cause; inspect
the bound rate/image and material/matching order before changing production VRS.
Microsoft's VariableRateShading specification also states that depth/stencil stay
at full resolution and SV_Depth output disables coarse shading, relevant to the
resolve. No VRS changes were made in this session.

Continuation: optional command-list-5 observers now capture native shading rate,
combiners and image during a requested probe. Slots 77/78 are installed only when
QueryInterface confirms the same list5 object/vtable; originals are published
before detours. Private --fine-rate comparisons temporarily select 1x1 and restore
native state after each draw. The hardware test checks subsequent coarse-rate
drawing without rebinding, in addition to image equality and compute restoration.

PID 26044 A/B: native VRS produced 8,820 and 8,554 differing MAIN colour bytes for
106/33 draws, 22 matched/11 fallback. Interleaved fine-rate tests on the same group
each produced zero differences. Both eyes report base 1x1, combiners 0/1 and the
same image pointer. No visible routing was enabled. A third native sample changed
to 106/19 with no fallback and passed. The process was closed after all six tests.
This establishes the VRS dependency but not whether image contents or raster
coverage are responsible. Evidence: `vrs-ab-26044`.

DLL 9CE293F2B78DCDA744993CF316DDD23741AB044C5A049E1D905387D1A6056B9C
adds a bounded R8_UINT VRS-image snapshot and --source-rate-image diagnostic.
It captures both native images and a frozen source copy, then can use the source
copy for private MAIN draws. The image-copy GPU test verifies row padding, copied
contents and resource-state transitions. No production VRS setting is changed.
PIDs 13344/9472 opened tiny windows and were rejected before probing. PID 15556
then crashed before probing: dump reports Codeware.dll+0x4D2A1, null write at +4,
not a GPU exception. `crash-15556.txt` is a stack scan, not a full causal unwind.

PID 12000 (normal 2560x1440 startup) confirmed different VRS image contents:
93x93 R8_UINT, same resource pointer but 27..30 differing texels between eyes.
Source histograms had roughly 5305 1x1 tiles and 3338 2x2 tiles; MAIN roughly
5295 and 3346. Native-VRS sample with 106/33 draws differed by 6,514 colour bytes.
Private MAIN draws using the frozen source VRS image matched exactly in both
tests, as did both fine-rate tests. Thus the early common MAIN draw uses a
different rate map from the later native MAIN pass; resource identity was
insufficient. Do not silently replace native per-eye VRS in production.
Evidence: `vrs-images-12000` (six samples). Full native packets/inputs were also
captured for the next geometry-stage step, then this test session was closed.

Fresh capture inspection of the depth prepass found VS 31209 with no pixel shader.
Its first compatible run has 18 draws / 130 instances; depth/stencil policy is the
same reverse-Z opaque policy. This is a genuine geometry-only candidate which
does not require replacing the native per-eye colour VRS maps. Patched VS passes
DXC validation and native/view-instanced PSO creation on the hardware driver.
Private asset: CyberpunkVR_SinglePassDepthVS.dxil, original 3635 bytes / FNV
3AC849D57E763A1F, patched 6387 bytes / FNV 600DF2F70C8C3C86 / SHA256
4735C5BC69DC9B59E629944B5CA2C59D567A4C75481AFD49D421CC613A61C93A.

The --depth-prepass probe supports zero MRTs, depth/stencil-only comparison and
direct native depth resolve. Colour targets are not allocated in this mode.
The production-path hardware test replaces the native depth group and compares
all 256,000 bytes exactly; existing colour/fine-VRS tests still pass. Eye-exclusive
draws now use the original mono PSO instead of the view-instanced PSO with one view.
This also passed the visibility, MAIN-fallback, mixed and watchdog tests.
Live depth validation is pending after deployment `deploy-depth-prepass-20260928`.

Build `tools/single_pass_tests` with CMake/Visual Studio x64. Private capture
exports live in `build/single-pass-20260927` and are not shipped. Use Windows SDK
DXC `-dumpbin` output as the patch input: RenderDoc's human-readable disassembly
uses rounded constants and is not a lossless assembly source. `dxil_tool` runs
`-hlsl-dxilload -viewid-state -hlsl-dxilemit`, assembles and validates. Run
`run_compare.py` with `gpu_compare.exe` and the exported fixture directory.

Detailed results: `capabilities.json`, `patch-sample-results.json`,
`comparison-results.json`, `pair-readiness.json`, `camera-writes.json`.

API references: [Microsoft's View Instancing specification](https://microsoft.github.io/DirectX-Specs/d3d/ViewInstancing.html)
describes view-aware shaders, target routing, dependency analysis and instance
independence; [DXC's assembler interface](https://github.com/microsoft/DirectXShaderCompiler/blob/main/include/dxc/dxcapi.h)
provides LLVM/DXIL assembly and validation. Hardware support and pixel agreement
above are measured locally, not inferred from the documentation.


Depth live attempt PID 28568 stopped before GPU substitution: Snapshot rejected
partial graphics root tables (native depth binds 0x8003ffff in the low 32 slots,
leaving unused pixel tables unbound). Capture now accepts known-unbound tables
only when command-list Reset was observed; restores all recorded tables up to
slot 63 and rejects out-of-root bits. The depth GPU test includes an unused,
unbound pixel-stage descriptor table and verifies later draws/compute.

Depth matching now also accepts complete CPU-upload instance arrays (48/64-byte
strides, max 16 KiB), using byte equality and strict resource/view bounds. Missing,
truncated, GPU-only, reordered or changed data remains on the native fallback.
DrawRecord ABI stays 1096; the owned batch is private to the bounded GPU work.
12 CPU/API tests plus depth-single, depth-batch, direct-colour, fine-VRS, fallback
and watchdog GPU tests pass with no validation errors. DLL FDE66FE748D999293840E576F55B2DFEE1E024DEFB942AD755C695F19B52A719,
archive `deploy-depth-batches-20260928`. Live validation still pending here.


Depth continuation, 2026-09-28 final checkpoint:

- The first depth group uses a DEFAULT instance buffer. The GPU reader now
  enables the existing bounded upload observer before those native uploads;
  NativeStereoProbe reads only fully observed ranges for the exact resource/GPU
  identity, never maps DEFAULT memory. Packet Resolve mode 2 keeps upload
  observation but avoids copying all scene object/material programs.
- PID 10692 validated 15 source / 7 MAIN draws, all 7 paired, no native fallback,
  22,052,250 depth/stencil bytes exact. Three visible direct-resolve samples and
  slow/fast HMD + position-jitter cases passed. For this small group the warm
  native draw sum was 0.056064 ms versus common 0.049296 + resolve 0.013936 ms:
  no net GPU saving. Files: depth-direct-profile-10692, depth-motion-10692.
- PID 7716 full native census: 3,777 draws, 4,202 packets, no truncation. The
  opaque depth call has repeated occurrences of the same supported PSO separated
  by other shader variants. Its long final run contains 69 draws per eye, sorted
  differently. Selecting a fixed mesh can land halfway through that run.
- The probe can select a complete 1-based source depth run (--depth-run). MAIN
  starts at its first exactly matching member of the selected source set. It
  must not require the first source object to also sort first in MAIN. Tests
  cover an earlier native prefix, an unrelated MAIN object, and reordered MAIN
  members. Native fallback/watchdog is preserved; unsupported shaders are not
  merged and native VRS remains unchanged.
- Installed DLL 219CF75C082789C408D077B7AE4581C37330E515746631F895CDF0222903A507,
  PDB D9E4A924EAABF5D8E4EFEA2AECB9290B9EF69A5D9A5A3266BF20D7A3E6FD835E,
  backup/deployment deploy-depth-run-20260928. 12 CPU/API tests passed; hardware
  depth-batch/anchor/reordered-anchor/later-run tests passed with zero differences
  and no D3D12 validation errors.
- PID 24528, run 3: private comparison 69 source / 65 MAIN, 65 exact matches,
  no fallback. Both visible direct-resolve timing samples replaced all 69/65
  native draws and compared 22,052,250 actual output bytes exactly.
  Native reference sums: 0.272448 / 0.275456 ms. Common draw + direct resolve:
  0.216064 / 0.216000 ms. Approx. 0.056-0.059 ms lower GPU cost for this
  INSTRUMENTED GROUP ONLY, excluding diagnostic reference/import/readback.
  Per-draw timestamps perturb timings; this is not demonstrated whole-frame FPS.
  Files: depth-run3-24528-private.json, depth-run3-profile-24528.
- Fast HMD motion (~120 deg/s) with up to 7 mm position jitter also passed direct
  visible replacement: 69 source / 69 MAIN, all 69 paired, zero byte differences.
  The simulator consumed restoration to x=0,y=1.7,z=0,yaw=pitch=roll=0. Only the
  fast case was run on this larger group because the bounded camera descriptor
  heap had room for one remaining capture; small-group slow/fast tests above are
  separate evidence. File: depth-run3-motion-24528.

All test controls were verified zero (depth-final-controls-24528.json), the final
both-eye image was inspected, and the game was closed after the test series.
No RenderDoc was opened in this continuation. No commit was made.

This remains a default-off, one-group/one-frame prototype. No continuous full
single-pass mode or overall FPS improvement is claimed. The diagnostic late
visibility gate still releases only after MAIN preparation (~26-33 ms in these
captures); that wait cannot simply be enabled on every frame as an optimization.
The next production step must address pass scheduling and broaden validated
shader/group coverage while eliminating diagnostic copies/queries/waits.
