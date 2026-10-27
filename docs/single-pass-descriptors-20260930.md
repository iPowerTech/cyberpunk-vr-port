# Single-pass descriptors and per-eye resource banks

Continuation of `single-pass-batch-20260930.md`. The full-scene continuous
single-pass goal is still active. No new overall FPS improvement is claimed.

## Native binding evidence

`StereoDescriptorAudit` follows heap/view creation and descriptor copies during
the explicit `--native-draws` test process only. It records canonical CBV/SRV/UAV/
sampler descriptions, object-lifetime cookies, descriptor range versions and
immutable per-draw snapshots. Unknown slots remain unknown. GPU handles are not
treated as resource pointers. Private object cookies prevent address reuse from
being interpreted as the old object. These cookies are NOT resource ownership.

Large ranges use versioned identities, cached until a write overlaps that range.
Small ranges also receive exact descriptor IDs. Overwritten values are reference
counted and recycled; exported snapshots intern their own descriptions, so reuse
of an internal ID cannot change an old snapshot. Capacity failure disables proof
and stops further store growth. No descriptor or resource is read back from GPU.

Snapshots are checked again immediately before command-list submission. Changed
ranges are rejected. Reset-before-submit rejects abandoned recordings; ClearState
and heap changes invalidate binding state without pretending a stale table is bound.
This verifies CPU descriptor state through submission, NOT resource contents or
immutability through GPU completion. A production pass still needs exact leases
and its own lifetime/synchronization contract.

The game's actual graphics root signature has **36 descriptor-table parameters**,
not 32: 18 per shader stage. The last four PS tables were outside the previous
1096-byte native draw record's table array. Geometry-only checks remain useful,
but that old record alone was never complete descriptor evidence. The descriptor
audit now tracks 64 root parameters independently, preserving the old draw ABI.
Root ranges are CBVs b0..b13, SRVs t0..t126, UAVs u0..u18, two sampler banks, and
five 32768-entry bindless SRV banks (spaces 1..5), repeated for VS/PS visibility.

PID27272 (`build/single-pass-20260930/descriptor-audit-14`) passed eleven launch,
HUD, native-instance, descriptor, image, repeat-capture and shutdown checks:

| Evidence | Result |
| --- | ---: |
| Native draw snapshots | 2,809 |
| Complete descriptor snapshots unchanged through submit | 2,809 |
| Captured root ranges | 97,302 |
| Owned descriptor IDs / distinct descriptions | 937,024 / 4,079 |
| Verified uploaded instance ranges | 1,248 / 1,248 |
| Geometry-compatible candidate pairs | 846 |
| Unambiguous candidate pairs checked for descriptors | 643 |

The full-table comparison finds differing CBV locations and some distinct SRV
resources for every unambiguous pair. Notable slots include t37 (the earlier
shader inventory identifies FrameExposureData), t38, t45, t70 and some low SRV
slots. Many other descriptors match exactly despite different GPU table handles.
This comparison includes unused bound slots: **differences do not prove that a
particular shader reads them**. Pipeline-specific shader usage must filter the
result before deciding which bindings require per-eye selection.

Earlier PIDs13888/24480 exhausted a prototype store that retained old descriptor
values; PID7628 then exposed the 32-parameter limit. All were rejected as evidence
and closed. They were not game crashes or accepted performance measurements.
The active observer is expensive and must not be used as an FPS benchmark.

## Paired CBV/SRV shaders

`patch_resource_pairs.py` rewrites the resource handle selection, not shader
arithmetic. Space-0 register N becomes two descriptors at registers 2*N and
2*N+1 in a private space (100 for VS, 101 for PS in the batch). `SV_ViewID`
chooses the descriptor. Original constant-buffer sizes and texture dimensions
are preserved. The selected index is marked nonuniform; instance-varying resource
indices require this according to the [DirectX binding specification](https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html).

Unlike the old camera-only approach, this supports independently allocated
per-eye constants and resources such as exposure. `--pair cbuffer:N` / `--pair
srv:N` can limit conversion to verified eye-varying bindings. Keeping proven
shared data independent of ViewID is important for GPU sharing; the all-bindings
conversion is a correctness baseline, not an optimal final binding policy.
Bindless arrays, samplers and UAVs are unchanged and explicitly listed as shared
dependencies requiring separate validation. No variant is automatically loaded
into game rendering.

Batch result from the saved inventory: **137/137 VS/PS modules assembled and
validated**, covering 428 CBV pairs and 289 SRV pairs. This is DXIL module
validation, not full pipeline linkage or whole-game material coverage.
Output: `build/single-pass-20260930/resource-pairs-batch/manifest.json`.

GPU validation uses the real captured mesh/VS and a resource-reading diagnostic
PS with a StructuredBuffer and Texture2D. The two eyes deliberately use different
camera blocks, different FrequentVertexConsts, different buffers and textures.
The shared six-draw plan equals the native ten-draw reference in all 16 tested
scale/roll/clip cases: zero color error and bit-exact depth. Deliberately swapping
the paired SRVs produces 16,635 differing pixels (max error 0.6), with unchanged
depth; the runner checks that negative control fails for that exact reason.
Output: `resource-pairs-vs-ps-gpu-results.json` under the same build directory.

## Remaining integration

Paired root layout construction and real PSO linkage are now implemented and
verified as described below. The next engine work is integrating those families
with timely owned per-eye resources before common recording.
Identical descriptor descriptions do not prove identical mutable buffer/texture
contents. Native descriptors may name memory reused between eye preparation;
the fixture's stable upload buffers do not solve that engine lifetime issue.
Shared CPU preparation, persistent pass-wide arrays and removal of the old MAIN
diagnostic wait remain required. The production renderer still uses native
multipass; the paired shaders above remain standalone tested candidates.

Final installed DLL:
`a71542e3879876e982c49c9da71ac660213cbd78d2e7dfdd65880ff3c0157a03`.
PDB: `74cfc42524a4d98c13d2519a02cd9cc81747ac67f57242ba0bb866200a5548bc`.
PID22856 (`descriptor-normal-15`) passed all nine ordinary launch checks with
`bindingObservationHooks=0`, stable HUD and both eyes. Settings were preserved;
the game was closed and no crash report was created. No commit or push was made.

Validation: 15 CTest cases, 44 Python single-pass tests, 137 DXIL validations,
16 paired-CBV GPU cases, 16 paired-CBV/SRV GPU cases and the intentional negative
control. No game and standalone capture/GPU replay ran concurrently.

## Exact pipeline linkage checkpoint

`export_native_pipelines.py` read frame 10323 once through CPU-only structured
data, saved 202 creation records (198 PSOs and four roots), and closed the capture.
No replay or game process ran during that export. Original binaries and converted
binaries are verified by SHA256 before constructing candidate pipelines.

`StereoPairedRoot` preserves the original parameter order, descriptor ranges,
root flags and static samplers, then appends at most one mixed CBV/SRV table per
VS/PS stage. The 36-parameter native root therefore needs 36..38 DWORDs in this
inventory, within the 64-DWORD limit. Each pair includes its original parameter
and descriptor offset, plus its private-bank destination. The builder rejects
missing/ambiguous bindings, duplicate requests, denied stages, register collisions,
unsupported root-constant materialization and root size overflow. Failure leaves
the previous plan untouched. Private banks use volatile descriptors/data: this
does not assert resource immutability that the engine has not yet proved.
See Microsoft's [root signature flags](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_descriptor_range_flags).

`BuildViewInstancedStream` now supports replacement of the root along with VS/PS,
while retaining the original stream state and removing the incompatible native
cached PSO. The caller still owns the root and shader bytecode lifetime.

The first exact pipeline batch created only 67/98 candidates. All 31 failures
were PS-stage View Instancing validation errors (1105), while the corresponding
native PSOs succeeded. This was not a descriptor or shader-model failure:
the ViewID inter-stage validator merges signature elements in order. Native PS
signatures can omit the VS clip-distance output and place rasterizer-generated
front-face input in its place or packed register. See Microsoft's
[inter-stage implementation](https://github.com/microsoft/DirectXShaderCompiler/blob/main/include/dxc/HLSL/ViewIDPipelineValidation.inl).

`link_stage_signatures.py` fixes the real interface rather than removing clipping
or suppressing dependency metadata. It inserts unused upstream outputs before
generated PS inputs, remaps the input IDs and their load instructions together,
and relocates a colliding generated scalar to a free register. Existing user
varying type/shape/location and interpolation remain unchanged. Unsupported
repacking is rejected. Shader arithmetic is checked unchanged after undoing only
the input-ID renumbering. DXC then regenerates and validates dependency metadata.

The per-PSO linker disassembles hash-verified converted binaries, not loose IR
files. Its output records both source shader hashes and the linked pixel binary
hash, and the native pipeline test checks all of them. A signature variant is
specific to a VS/PS pair, not simply to the pixel shader filename.

Final results (`build/single-pass-20260930`):

- `linked-resource-pairs-final/manifest.json`: interfaces for 83 PS-bearing PSOs;
  depth-only PSOs need no pixel link. 44 interfaces normalized, 19 generated
  inputs relocated.
- `linked-pipelines-final.json`: 98/98 prepared graphics PSOs created successfully
  with exact captured input, blend, raster, depth/stencil and target states.
  Two legacy graphics PSOs remain excluded from conversion. All 45 RenderElements
  PSOs covering 2,993 draw events in this frame are included in the successful set.
- The batch's exact-byte root cache used 73 entries and 123 hits. This cache is
  in the standalone test harness; it is not yet a live engine family cache.
- Two sky PSOs retain the same unbound-target/depth warnings as their native
  baselines. No new error is accepted as successful linkage.
- `relocated-input-gpu-results.json`: 32 real captured mesh/VS cases, different
  CBVs/SRVs per eye, both winding conventions, rolls, frustum changes and single-eye
  clip rejection. Zero color error and bit-exact depth against native two-pass.
  Reference color hashes change with winding, proving that the front-face branch
  is exercised. The unlinked PS fails with the expected PSO error; swapped SRVs
  fail the image comparison while leaving depth equal.
- 16 CTest cases and 62 Python single-pass tests passed. The Release DLL builds,
  including the new root builder; SHA256
  `f5dadabd33f85bc68fcaca8214df632aae5f410dc7987579fd3d65a21672e392`.

The installed DLL remains `a71542e3879876e982c49c9da71ac660213cbd78d2e7dfdd65880ff3c0157a03`.
Nothing from this checkpoint was deployed or enabled in live rendering. Game and
capture are closed; no commit or push was made. Pipeline creation does NOT prove
material image correctness for all 98 pipelines, resource ownership, shared CPU
preparation, persistent full-scene arrays, temporal correctness or an FPS gain.
All of those remaining requirements still apply to the active single-pass goal.

## Owned pass binding banks

`StereoBindingBank` now provides an immutable input bank for a whole compatible
pass, with multiple material/draw binding sets sharing one resource heap and an
optional sampler heap. It does not switch to a different heap for each material.
Bindings keep the original root parameter indices and the appended paired tables.

- CBV bytes are copied into owned, 256-byte-aligned upload storage. Identical
  payloads are deduplicated across draws, with full-byte equality, not only hashes.
- Mutable DEFAULT-heap SRVs receive committed snapshots. The snapshot key includes
  the eye: the same native resource can hold different eye contents at different
  producer positions. Repeated views of a resource within one eye share its copy.
- `CaptureEye` records each eye separately at its producer position and restores
  the source state. `Bind` requires both captures in the same direct-list recording.
  Source states must be known and uniform across subresources; the bank does not
  guess them. It does not insert a CPU wait or a late-MAIN diagnostic gate.
- Shared tables require already-versioned CPU descriptors and explicit read-only
  resource leases. Their descriptor ranges are copied once, deduplicated across
  material sets and retained separately from the source CPU heaps. Sampler heaps
  and explicit typed null SRVs are supported. Copy sources must be non-shader-
  visible, as required by [D3D12 descriptor copying](https://learn.microsoft.com/en-us/windows/win32/direct3d12/copying-descriptors).
- All source/destination resources, roots and private heaps are held by the
  existing `CommandResources` recording/submission owner. `HasCommandResource`
  checks the current recording, so a reused raw list pointer after Reset cannot
  authorize an old bank. GPU submissions retain the owner until their fence.
- Construction is transactional and does not record commands. Limits are 32,768
  draw sets, 512 paired bindings per set, one million resource descriptors, 2,048
  samplers, 16 MiB of unique constants and 256 MiB of snapshot allocations.
  Invalid table kinds/ranges, missing leases, duplicate destinations, unsupported
  upload/RTAS SRVs, mismatched devices and empty CBVs fail before publication.

The capture contract is GPU-ordering-sensitive: copies must execute after the
correct native producer and before reuse/aliasing. Reference counting alone
cannot preserve contents. The API owns resources, but it is not an automatic
native resource-state/lifetime proof. It currently copies whole resources;
view-range copies and a fence-recycled allocation arena remain performance work.
The [D3D12 CopyResource contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copyresource)
defines the matching dimensions/formats and source/destination states used here.

Validation:

- `binding_bank_test` (WARP) renders two different material sets from one heap.
  Both still produce exactly 13.25 and 14.25 after original constants, CPU SRVs
  and CPU samplers are overwritten and their owners released. Shared table
  deduplication gives nine resource descriptors; shared constants give 768 bytes.
  Typed-null descriptors read zero. Failed construction leaves no published bank.
- A deliberately blocked queue lets the test destroy the bank and reset its
  command list before GPU execution. The shared-resource sentinel stays alive
  until the completion fence, then is released exactly once. Stale Bind after
  Reset and incomplete/cross-recording eye captures are rejected.
- `owned-bank-alias-results.json`: 32 captured mesh/VS GPU cases where both eyes
  reuse the SAME source buffer and texture. Capture eye 0, overwrite for eye 1,
  capture eye 1, then poison the sources again before the joint draw. All colors
  and depth match the two-pass reference exactly, including reversed winding,
  view clipping and the swapped-pair negative control.
- `owned-bank-separate-results.json`: 32 equivalent cases with distinct native
  eye sources also pass; swapped SRVs fail color comparison without changing depth.
- 17 CTest cases and 62 Python tests pass. Release build SHA256:
  `bf2371f6da4daa0a6216093d3e4edeced2cad717f2f47c816870bc12e74661c4`.

These are production-library and standalone GPU changes, not a live single-pass
switch. The native descriptor/CBV collector still must supply timely owned inputs
and certify shared tables; common CPU preparation and pass recording are not
connected yet. No bank is automatically built from the diagnostic audit's weak
resource cookies or stale pointers. No overall FPS benefit is claimed.
The installed DLL remains `a71542e3879876e982c49c9da71ac660213cbd78d2e7dfdd65880ff3c0157a03`;
this checkpoint was not deployed. Game/capture were not started; no commit/push.

## Native family registry and live linkage

`NativeStereoFamilies` connects the offline linked families to real game PSO
creation and command-list state. `package_families.py` produces a hash-verified
development package: 94 unique VS/PS pairs and 137 converted shader blobs. Each
family carries the original resource-use contract and paired binding layout.
The package is private build output, not a release/distribution asset.

Creation hooks preserve the native PSO and separately create its paired root/PSO.
The family is attached to the original PSO via D3D12 private data, not a global
map of stale object addresses. SetPipelineState and SetGraphicsRootSignature
capture strong command-list metadata; Reset, ClearState and bundles invalidate
it. Observation reads this command-list state, never the older thread-local PSO
pointer. GetPrivateData/SetPrivateDataInterface reference counting follows the
[D3D12 lifetime contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12object-getprivatedata).

Enable only through `CVR_STEREO_FAMILY_MANIFEST`, set by the test launch option.
Ordinary launches remove it. The module does not redirect any draw, remove an
eye, wait for MAIN or change scene preparation. A created/recognized family is
linkage evidence, not authorization to skip native work.

Two real failures were found and fixed before accepting the live result:

1. The first offline batch (PID14476) built all PSOs but hung during destruction.
   Its completed JSON was therefore NOT a passing run. A debugger stack/memory
   inspection placed it in D3D12 root release while destroying the original
   root's private-data variant map. Strong child-root references there caused
   nested driver releases under its root lock, and could form a cycle for an
   identical root. That cache now stores weak references; PSO families own the
   live variants. Repeated offline batches exit normally, also verifying that a
   command list can retain a family after the original PSO is released and that
   resetting the list releases the last family without a cycle.
2. PID21936 crashed during save loading (report `Cyberpunk2077-20260930-220801-21936-21052`).
   The exact deployed DLL/PDB identifies Hash -> sight_is_target ->
   Hook_CreatePipelineState -> CreateViewInstancedPipeline. The legacy stream
   walker assumed every payload started at +8; a scalar subobject actually uses
   +4. Misaligned traversal overwrote a shader offset with state data, producing
   a bogus shader pointer 0x346 and length 0x100000000. The creation hooks now use
   the tested full-stream offsets parser, with duplicate/malformed-tag rejection,
   and internal PSO creation bypasses native substitutions/observation entirely.
   CrashReporter was closed by the launch script. This was a real regression
   exposed by integration, not a random game crash.

Always use the matching deployment archive with `read_dump.py --image-dir`.
The old script's default build-tree search can resolve a different DLL/PDB and
mislabel an address even when it prints a plausible function name.

Final installed/built DLL:
`783cb530433dbacb36797b07f0f38f68faa7d4e233f0003463dfa0e42a3164ea`.
PDB: `ce506ca3aa9dda36e46f369c2e3ac44b3625361348d237f52f2189a4c1c065ce`.
Deployment: `build/single-pass-20260927/deploy-family-stream-fix-19`, with settings
hashes unchanged. Framegen, foveated DLSS and VRS stayed off; async compute stayed on.

PID21820 (`family-stream-fix-19`) passed all ten planned checks and closed:

- Stable HUD: 1,031 ms, 88 successful submissions, 55 fresh source advances.
- 193 live paired PSOs created, zero creation/binding failures or record overflow.
- 124 paired-root cache hits while the native PSOs remained alive.
- 32 families used in the two-second RenderElements census. Matched draw calls:
  MAIN 149,282, VRCAM 153,365; unmatched 36,582 and 37,935. Roughly 80% coverage
  in this scene, not 80% of the single-pass implementation or an FPS gain.
- Zero native-root mismatches. Native output retained; both eye images nonblack.
- Geometry packet captures, repeat requests, CPU sampling and control restoration
  passed. No new crash report was created.

PID25552's ordinary launch was rejected before rendering tests because native
menu=1 and HUD counters stayed zero despite scripting world_ready. It was closed
and had no crash report; this pre-existing intermittent startup condition remains
guarded, not claimed fixed. The retry PID16020 (`family-disabled-21`) passed all
nine ordinary checks with family assetStatus=0, family count=0 and extra binding
hooks=0. HUD remained fresh for 1,016 ms / 88 submissions. Both processes closed.

Validation also includes 17 CTest cases, 65 Python single-pass tests, 12 launcher
tests and the 98-PSO runtime-registry offline batch with repeated lookup, metadata
reset and destruction checks. The game, capture replay and standalone GPU tests
were never run together. No commit/push was made.

Remaining: collect timely per-eye native CBV/SRV/shared-table contents and leases
using each family's resource-use contract, connect the owned pass bank to common
recording, and remove duplicated CPU scene preparation. Roughly 20% of observed
draws also need additional shader-family coverage. Texture-array routing and
temporal/Framegen/full-scene validation remain required. Native rendering still
executes both eyes; the full single-pass goal is active and unproven.

## Native constant ownership checkpoint

`NativeConstantCapture` connects a linked family's declared CBVs to the actual
graphics descriptor tables at the native draw. It copies the full aligned CBV
payload, verifies two consecutive CPU reads agree, interns full bytes, then
checks both the descriptor identity and bytes again immediately before submission.
An abandoned list, unreadable data, missing descriptor or changed contents are
explicit failures, not zeros/default constants. The source descriptor audit now
provides a bounded CBV resolver rather than exposing a raw GPU-handle guess.

Captured ABI: 32-byte Draw, 72-byte Binding, 16-byte Payload plus owned byte blob.
Draw flags 7 mean all declared CBVs were copied, the list submitted, and the
values were unchanged at the pre-submit check. Individual binding statuses are
1 copied, 2 unknown descriptor, 4 undersized, 8 unreadable/torn, 16 descriptor
changed, 32 bytes changed, 64 abandoned. Capture is opt-in and bounded to 4,096
draws, 65,536 bindings and 64 MiB of unique payload bytes.

`CopyConstantPair` packages same-family/same-frame VRCAM and MAIN records into
owned `BankPair` inputs. It requires complete constants and rejects any remaining
SRV/UAV/sampler dependencies. `ConstantBankRequest` exercises actual bank creation
on the render thread, but does not record a draw or approve geometry equivalence.
The test runner supports `--native-constants` with both `--native-draws` and
`--family-manifest`. **This live bank-construction gate has not passed yet.**

The initial existing mapped-upload table yielded zero reads (PID17608,
`native-constants-22`). A generic creation-time upload registry then filled with
streaming resources (PID10816, `-23`). Bounded LRU eviction avoided saturation
but did not resolve CBVs (PID11536, `-24`). CPU-accessible CUSTOM/GPU_UPLOAD
support did not solve the missing path either (PID1904, `-25`); the observed
creation types contained no GPU_UPLOAD buffers. That earlier hypothesis was
not confirmed. No failing capture was accepted as complete or benchmarked.

`ConstantUploadRegistry` now imports resources from exact native CreateCBV return
sites rather than collecting all temporary streaming allocations:

- At RVA 0x1F01A6, the native sized uploader's stack contains the strong live
  resource at RSP+0x30 and allocation CPU address at RSP+0x48.
- At RVA 0x1F0BF7, the native dynamic-map path has its resource in the current
  upload ring at R14+0x149DF68 and allocation CPU address in R15.
- The registry verifies buffer shape, CPU-accessible heap, the descriptor's GPU
  range, and that Map(base)+descriptorOffset exactly matches the uploader's CPU
  allocation address. Only then does it retain/map the resource. It never Map's
  a DEFAULT heap or AddRefs an arbitrary saved weak GPU-address entry.
- Source assembly is preserved in `build/single-pass-20260927/fresh-native-buffer-*.asm`.
  IDA MCP was unavailable during this checkpoint; saved current-EXE disassembly
  was used. CPU-accessible heap semantics were cross-checked against Microsoft's
  [GPU upload specification](https://microsoft.github.io/DirectX-Specs/d3d/D3D12GPUUploadHeaps.html).
- Retention remains bounded to 256 resources / 512 MiB, with active CBV touches
  updating recency. Eviction never invalidates already-owned constant payloads.
  Generic CreateCommitted/PlacedResource observation was removed after testing
  because it added no constant reads and retained irrelevant streaming buffers.

PID22576 (`native-constants-26`) provided new, partial live evidence:

| Evidence | Result |
| --- | ---: |
| Native draws / fully submitted descriptor snapshots | 2,796 / 2,796 |
| Draws with linked-family CBV contracts | 2,238 |
| Declared CBV bindings examined | 14,886 |
| Copied bindings unchanged at pre-submit | 4,817 |
| Unique owned constant payloads | 2,022 |
| Unique payload bytes | 703,232 |
| Unreadable bindings | 10,069 |
| Draws with every declared CBV ready | 0 |

All observed VS b5/b6/b7 and PS b3/b7 bindings passed. VS/PS b1 and several
persistent material/global blocks did not resolve. Thus no live constant-only
bank was built and no new rendering mode was activated. The bank-construction
unit test succeeds with controlled native-style descriptors, not with these
incomplete live inputs.

Joining that capture to geometry gives 865 candidate packet pairs, 792 compatible
geometry mappings (176 ambiguous), and 73 unmapped pairs. Among unambiguous
mappings, 518 linked-family pairs still lack complete constants and 98 lack a
linked family. `analyze_constant_pairs.py` deliberately reports zero approved
constant pairs and `runtimeEligible=false`. Both image/family/capture consumers
must preserve this distinction.

Tests: 18 CTest cases and 71 Python tests passed. The new C++ test covers owned
upload lifetime, streaming churn, bounds, full-byte deduplication, torn reads,
descriptor/data rewrites, abandonment, reversed eyes, frame mismatch, remaining
resource dependencies and construction of a real private bank from owned fixture
bytes. The Python tests validate binary ABI/ranges/hashes and geometry joins.
No crash occurred in the five diagnostic launches above; each was closed when
its incomplete-data gate failed, with remaining checks explicitly recorded.

Final DLL SHA256:
`830f269fcb26750a581da7f7a047221d382c6450ca260ebeb2a79acfd8c5e518`.
PDB: `3c89397988e8c7cf8923ad20bf56632915a6a470f19a69d1e599e23bc627a2d3`.
Deployment: `deploy-constant-registry-normal-27` under `build/single-pass-20260927`.
Settings hashes were preserved. PID12044 passed all nine ordinary launch checks:
HUD 1,000 ms / 87 submissions / 53 source advances; native family status/count,
extra binding hooks and retained constant-upload count all zero. It was closed.
Game/capture are closed; no commit/push or overall FPS claim.

Next: resolve the missing native registered-buffer path, starting with camera b1
and the existing 0x1F088C / 0x1F08D8 / 0x1F0A28 uploader/map/copy routines. Determine
its actual resource type and write/submit lifetime before choosing CPU ownership
or a properly ordered GPU snapshot. Do not repeat generic heap scans, widen cache
limits as a substitute for identity, or mark partial CBV coverage as a complete
draw. Shared SRV ownership, unified command recording, early common preparation
and full-scene/temporal validation are still outstanding.

## Recorded GPU Uploads and Queue Ordering

The later checkpoints supersede the earlier zero-complete-CBV result, but do not
enable native draw removal. CBV ownership now copies the shader-declared payload
(for example camera 848 bytes), not the descriptor's 256-byte alignment padding.
The serialized binding `reserved` value is 1 for logical CPU bytes and 3 for a
witnessed GPU-copy source. Old value 0 remains decodable as full-view bytes.

`RegisteredConstantUpload` observes actual CopyBufferRegion API arguments.
It owns small CPU-upload-to-DEFAULT-buffer writes, including destination offsets
and uploaders outside the native BufUpload wrapper. Two CPU reads must agree;
the source is checked again before submission. Resources are retained through
command recording and fenced submission. The ledger is bounded to 8,192 copy
versions / 16 MiB and exists only during the explicit constant observation.
UAV-capable destinations and unmappable sources are not accepted. Unknown
overwrites, discard, Reset and aliases invalidate latest-address lookups;
committed buffers survive unrelated null alias barriers. Historical receipts
preserve the intended source of an earlier draw when a later write is recorded.

The exact GetGPUVirtualAddress caller observer (native return RVAs 0x1F0BD5 and
0x1F0175) resolved the same 192 MiB UPLOAD ring as the older import path. It did
not resolve the missing cameras by itself: `constant-api-38`, PID21620, still
had zero complete draw sets. The broader actual-copy observer in
`constant-copy-api-39`, PID11776, witnessed 4,607 writes (3,948 outside the old
wrapper), but still had zero complete sets. Increasing cache limits was not the
solution. Both launches were closed after their failed bank gate.

In that capture the same two camera addresses were readable only for some draws.
Recording a draw and submitting its camera producer are different events. The
collector now permits a pending producer's bytes to be owned with a receipt,
then requires the producer to have been submitted no later than the consumer,
on the same queue and in a compatible batch position. Reversed batch order and
unproven cross-queue dependencies are rejected. Tests cover both cases, changed
source bytes, partial overwrites, suballocated uploads, Reset and actual GPU
readback of the witnessed copies.

`constant-order-40`, PID19596, DLL
`b49bc25566e3e0faffea7efd80640e99337f9e2082c0621387ae261dd72ea334`:

| Evidence | Result |
| --- | ---: |
| Native draws / complete descriptor snapshots | 2,823 / 2,823 |
| Linked-family draw records / used CBV bindings | 2,263 / 15,143 |
| Complete constant-only draw records | 78 |
| Stable individual bindings / unreadable / failed validation | 5,223 / 8,461 / 1,459 |
| Pending-producer reads / queue-order rejections | 3,504 / 101 |
| Private bank created from two live eye records | 4 descriptors / 2,560 bytes |
| Native rendering changed | No |

All 13 launch checks passed, including fresh HUD, both-eye images, repeated packet
requests, control restoration and crash checks. The game was closed. The bank
creation gate now passes for sequences 21 and 1572, but these two same-family
records are not proof of matching mesh geometry. The geometry join found 779
candidate packet pairs, 695 geometry-compatible pairs (183 ambiguous), and zero
unambiguous geometry pairs with complete constants on both sides. Of the 78
complete records, 75 are VRCAM and three MAIN, all family 0x27e.

Important remaining distinction: these receipts validate a witnessed copy's
source bytes and submission order, not the final GPU bytes at the draw. Before
using them to replace draws, resolve the last applicable write at each consumer's
command position, including out-of-order CPU recording, subsequent overwrites,
same-list positions and cross-queue synchronization. Do not pick whichever
historical version passes. Shader resources/shared tables, actual common command
recording, early common CPU preparation and whole-frame performance are still
outstanding. No overall FPS improvement is established by these diagnostics.

Startup controls also remain strict: one second / 30 successful fresh HUD
submissions are required after the world/menu checks. The ordinary
`stable-hud-normal-37` launch passed all nine checks (PID21920, 1,016 ms,
88 submissions, 57 source advances), with family assets/hooks/upload tracking
disabled. The packet reader now resolves the installed DLL/PDB, so a newer local
build cannot accidentally supply its symbol offsets. `stable-hud-normal-36`
correctly failed before packet reading on the old local-image mismatch.

Earlier diagnostic startup failures remain separate evidence: PIDs7296, 27248,
7448 and 28188 reached CET world_ready but native menu mode stayed active, so HUD
readiness rejected them. PIDs25688 and 19424 crashed before gameplay; their exact
deployment DLL/PDB archives and PID-matched reports were retained, and reporters
were closed. The native allocator/DEP crashes have no established root cause;
neither a random-crash claim nor causation by this observer is proven.

Final ordinary-mode control used the same B49BC255 DLL/PDB pair:
`constant-order-normal-41` (PID16840) was correctly rejected by the native HUD
gate: menu=1 and zero prepared/submitted HUD frames despite CET world_ready.
Its single Space confirmation did not clear that condition. It was closed;
there was no crash. No menu-state override was used. An unchanged repeat,
`build/single-pass-20261001/constant-order-normal-42` (PID26280), passed all nine
checks: HUD 1,015 ms / 88 submissions / 59 source advances, and zero family
assets, extra binding observers, constant-upload retention or active test
controls. It was closed too. This confirms an ordinary successful run, not a
fix for the intermittent native-menu startup condition. The simulator's
existing preview dimensions/settings were not changed; its narrow composite
image proves only nonblack per-eye content, not full-resolution visual equality.

Verification: 18 CTests, 76 single-pass Python tests and 13 launcher Python tests
passed. Deployments preserved all game/plugin/launcher settings hashes. Current
PDB SHA256 is `84efb94918594620da54875f6a9c92ea7298c3e3f9aa7d2bc168451810e60e79`;
matching binaries are archived in `build/single-pass-20260927/deploy-constant-order-40`.
No game, capture replay or GPU test remains running; no commit/push was made.

## Command Positions and Fence Dependencies (2026-10-01)

The earlier 78 complete records were **copy-recipe evidence**, not final buffer
contents at each draw. The new implementation deliberately rejects that weaker
proof and selects the last applicable witnessed write at the consumer's command
position. No old passing bank result authorizes draw replacement.

`ConstantWriteTimeline` tracks recording generations, intra-list command positions,
submitted list order, bounded known/unknown writes, partial overwrites, abandoned
recordings and alias invalidations. It does not select the latest CPU-recorded
write. `GpuQueueOrder` adds a bounded graph of real queue submissions, Signal and
Wait calls. Exact observed fence values can establish transitive dependencies.
CPU signals, non-monotonic/multiple-producer fences and already-completed unknown
waits cannot manufacture an edge. Polling completion of an unobserved signal can
end Pending, but cannot identify its producer. Queue/fence identities are retained
through the observation interval.

These choices follow the distinction between queue submission and completion in
Microsoft's [ExecuteCommandLists contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists)
and [multi-engine synchronization model](https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization).
Lists batched in one Execute can overlap; actual resource barriers and native
work ordering still matter. An observed dependency is not a replacement for
whole-pass GPU image validation.

`NativeConstantCapture` now records read positions while drawing and resolves
GPU-backed CBVs after all captured consumers reach submission. Registered-copy
sources retain their pre-submit byte check. Deferred bindings are not published
as ready; a missing read-position token fails capture instead of silently falling
back to the old receipt validation. Binding flag 7 means logical bytes selected
through the command timeline. Failed bindings encode status/detail in `reserved`
with bit 31 set, exposed by `read_constants.py` as `timelineFailureReasons`.
Unknown CopyResource/Discard/CopyTexture-to-buffer writes are included.

New tests exercise CPU recording in the opposite order to GPU submission, writes
before/after two reads of one buffer, later full and partial overwrites, Reset,
replayed lists, fence values, future signals and transitive dependencies. Two
real GPU readback tests verify different constant versions at their positions,
and a cross-queue producer recorded after its consumer on the CPU. The latter
uses actual hooked Queue::Signal/Wait and checks CPU Fence::Signal invalidation.

### Live Evidence

- `constant-timeline-43` was aborted: deployment was attempted while linking still
  held the output DLL. Installed B49BC255 DLL/PDB were unchanged. PID13484 was
  closed deliberately; this is not a crash or a test of the new DLL. Later build
  completion was verified before deployment. Child diagnostics now force UTF-8
  so localized Windows errors do not mask the underlying failure.
- `constant-timeline-44` / `timeline-order-reasons-45` (PIDs12460/14980) completed
  native captures but rejected the bank. The latter saw three queues, 310
  recording generations, 5,730 writes and **zero replays**. Its 7,141 cross-queue
  failures motivated observing the real fences, not suppressing async compute.
- `constant-fence-order-46` (PID7032) timed out after enabling the collector.
  Code review found a lock inversion: observing Signal under ImageSubmissionMutex
  could run while CommandResources held its resource mutex. The fixed observer
  uses a separate narrow recursive queue-order mutex only around actual enqueue
  operations. Resource retention now happens before entering the copy ledger's
  mutex. No added GPU waits were introduced. There was no crash report.
- `constant-fence-locks-47` (PID19368) completed packet/descriptor capture again,
  but deferred constants stayed pending because waits could predate observation.
  Actual completion is now refreshed without inventing a dependency for those
  unknown producers. The timeout is still a failure, not a passing bank test.
- `constant-fence-progress-48` (PID21424) resolved 1,960 additional GPU-backed CBV
  bindings through the timeline. All captured consumers settled, with no overflow
  or replay. The full bank gate still failed.
- `constant-write-reasons-49` (PID16868) yielded 2,809 complete native descriptor
  snapshots and 2,251 linked-family draws / 15,037 CBV bindings. There were 6,810
  ready bindings (1,954 from the timeline), 2,724 missing and 5,503 rejected.
  **Zero draws have every declared CBV under the stronger proof.** All diagnostic
  launches above were closed after their checks/rejections.

The last detailed failure breakdown distinguishes actionable paths:
VS b1 has 1,159 cross-queue rejections (PS b1: 34); many frame/global blocks
(VS b8/b13, PS b0/b12/b13) hit alias barriers; PS b2 has 184 unmappable-source
failures; VS/PS b4 is missing from the observation window. The runtime recorded
774 GPU signals, 448 waits and 167 CPU signals. Next, inspect fence reuse/CPU
signals for the remaining camera dependencies, establish actual placement/alias
ownership for the globals, and acquire persistent material inputs without
guessing their contents. These are separate problems, not one cache-size issue.

Single-pass draw routing, common early CPU preparation and whole-frame speedup
remain unimplemented/unproven. The new tests prove the collector's controlled
cases, not full-scene correctness. The earlier `constant-order-40` bank result
must not be reused as approval for a live merged pass.

### Final Control and Remaining Crash

`constant-timeline-normal-50` (PID12768) crashed before save loading. The exact
170D0253 DLL/PDB archive was used with `tools/crash/read_dump.py`. Fault:
`Cyberpunk2077.exe+0x14BA9E`, indirect call `[r10+0x18]` in the native dynamic
buffer resize helper. R10 was `EXE+0x204DBAC`, not a valid allocator function
table. The array header had capacity/count 1 and a data pointer into the EXE.
The scan contains a Codeware frame but no VR-module frame; this is not an unwind
or proof of which module corrupted the object. New observer counters were not
present in the minidump memory ranges, so their values cannot be inferred from
the dump. Root cause remains unresolved, not classified as random. Report:
`Cyberpunk2077-20261001-004206-12768-4368`. CrashReporter was closed.

An unchanged repeat, PID16536 (`constant-timeline-normal-51`), reached gameplay
and passed HUD/packet checks, but failed image validation: the simulator's saved
preview height had shrunk to 16, producing a 1920x14 screenshot. That run was
closed and not counted as a pass. The owned test runner now temporarily sets
only its preview window to 1280x720 and restores the original settings bytes
after exit (preserving unrelated intervening changes). It does not change eye
resolution, FOV, rendering options or frame/mirror rate. This inspection setup
must not be presented as an FPS optimization or compared to the old tiny preview.

`constant-timeline-normal-52`, PID16148, then passed all nine ordinary checks:
HUD 1,016 ms / 87 successful submissions / 55 source advances, valid both-eye
1280x720 image, repeat captures, restored controls and no crash. Family assets,
extra binding observers and retained constant uploads were zero. The game was
closed and the simulator settings were restored, including original 2193x16
preview dimensions. Image inspection confirms scenery and hands in both eyes;
it is not a full-resolution or merged-pass image comparison.

Latest installed DLL SHA256:
`170d02539ea3d4f2d5b662a375715d1435c93461ab8c669f812cd86b6c5296ca`.
PDB: `200555bb2bb2b00d1970a5eca31b113f2a4a194691830ce57f11926611af97ef`.
Archive: `build/single-pass-20260927/deploy-constant-timeline-normal-50`.
All game/plugin/launcher settings hashes were preserved. Verification now has
19 CTests, 84 single-pass Python tests and 13 launcher Python tests passing.
There is still no successful live bank under the stronger complete-order proof,
no merged scene pass, no shared early CPU preparation and no demonstrated FPS
gain. No task-owned game/replay/GPU-test process remains running; no commit/push.

## Exact Alias Scopes and a Matched Pass Bank

The 2026-10-01 checkpoint below supersedes the earlier zero-complete-order result.
`ConstantTrace` now exports a bounded immutable journal of recording positions,
writes, payload identities, submits, fences, completion observations, aliases,
draw/slot identities and resolution decisions. `constant_trace_replay` uses the
same C++ ordering implementation offline. Binary events are 64 bytes; the
placement extension adds operation 20 and heap/offset/size to alias events.
`analyze_constant_trace.py` summarizes the artifact without GPU replay.

`constant-trace-53`, PID9024, yielded 61,690 events and 10,137 decisions reproduced
with zero mismatches. All 140 CPU signals came from `EXE+0x95AF92`, on one fence
with **no GPU signals**, not from nested Queue::Signal calls or reused GPU fences.
The CPU-reset hypothesis was not confirmed. Capturing actual completed GPU fence
values immediately before each new submission then added valid CPU-observed
completion dependencies, without adding waits. A late observation cannot alter
an earlier consumer. Tests cover this distinction. `constant-completion-54`
(PID5604) reached 205 complete records, all MAIN; its bank still correctly failed
because no complete VRCAM counterpart was available.

An independent resolver bug was fixed with a failing-then-passing regression:
two old unordered writes must not poison a later full write proven after both.
The resolver now keeps maximal predecessor writes. This fix alone did not change
the saved live trace's decisions and is not credited for the following gain.

The main remaining rejection came from treating `(nullptr, known-resource)` alias
barriers as global invalidations. Actual placed-resource creation now records
heap identity, offset and allocation size, and retains the heap through a private
resource interface. Texture endpoints are included, not only buffers. A barrier
for a different heap or disjoint range no longer invalidates a constant buffer.
Unknown endpoints still fail conservatively; genuinely overlapping aliases still
invalidate. Tests include a real GPU readback after activating a disjoint placed
buffer, an overlapping alias rejection and release of the caller's heap reference.

`constant-alias-ranges-55`, PID22812, passed all 13 diagnostic checks. It captured
408 complete constant-only draw records across both eyes, with zero cross-queue
rejections. Offline geometry correlation found 113 unambiguous geometry pairs in
the same native pass (argument plane 3); VS b1 differed per eye and VS b5 matched
for all 113. The 73,361-event journal reproduced 10,267 resolutions exactly.

The bank test was then strengthened: a same-family pair alone is no longer enough.
`select_constant_pass.py` requires matching geometry, eye/frame/family identities,
full constant ownership and the same normalized native pass identity. Geometry
reports are tied to the exact capture file hash, PID and DLL. Native bank requests
accept a bounded sequence-pair batch and call CreatePass once. Tests verify
multi-draw deduplication, invalid bounds, ambiguous/mismatched pairs and separation
of different passes.

`constant-pass-bank-56`, PID4000, passed all **14** checks:

| Evidence | Result |
| --- | ---: |
| Complete constant-only native draw records | 410 |
| Selected matched pairs in argument plane 3 | 105 |
| Bank draw records / descriptors | 105 / 420 |
| Owned constant bytes after deduplication | 9,984 |
| Journal events / exact reproduced resolutions | 66,334 / 10,189 |
| Native draws replaced | 0 |

These are bank records, not an executed 210-to-105 draw reduction. A matched packet
can occupy only part of a native instance batch. Full instance coverage, strong
geometry-buffer ownership, remaining dynamic graphics state and real common
recording/array-target integration must be verified before replacement. Early
shared CPU preparation remains absent. Persistent material CBVs (notably b4),
GPU-only b2 sources and partial b0 writes remain outside this constant-only pass.
The next implementation must not revive the old late-MAIN diagnostic wait.

`pass-bank-normal-57`, PID19260, passed all nine ordinary launch checks using the
same DLL. HUD: 1,016 ms / 86 submissions / 54 source advances. Family assets,
extra binding observers, upload retention and bank controls were zero. Both eyes
were nonblack; game and test preview were closed/restored. No crashes occurred in
these launches. Game/plugin/launcher settings hashes remained unchanged.

Current DLL SHA256:
`f9942b12363080352293a18d944a2e083e9812215e5ce8bec4e64d0a1e66a1a0`.
PDB: `1f818737f43ca2b8d45eb9aa73483e497056ed54f87a1529d1120648f42d3d64`.
Archive: `build/single-pass-20260927/deploy-constant-pass-bank-56`.
Verification: 19 CTests and 93 single-pass Python tests passed; launcher tests
were unchanged. No game/replay/GPU test is running. No commit, push, full-scene
single-pass claim or FPS improvement claim.

## Owned Geometry and Prepared Commands

The next checkpoint retains native IB/VB resources at their actual binding call
sites instead of looking up historical raw GPU addresses. `NativeIaBinding`
checks the supported EXE function hashes, decodes the current native registry
records, and validates both logical and actual resource bounds before retaining
the resource. Unknown callers or code versions fail closed. `NativeGeometryLeases`
records per-draw ownership and keeps submitted resources through command fences.
This proves resource lifetime and ranges, not immutable GPU contents at a later
execution position.

`native-geometry-leases-58` reached a bank but rejected valid vertex updates
outside the 16 slots observed by the collector. The fix accepts the native
32-slot binding range while tracking only supported slots; unsupported shader
input masks remain rejected. `geometry-slot-range-59`, PID12064, passed all
16 checks: 2,657 of 2,814 draws had complete ownership, and all 178 draws in the
89 selected pairs were owned. No crash or draw replacement occurred.

Full native instance batches are now copied and checked, including bytes beyond
the previously inspected packet prefix. `verify_full_instances.py` requires exact
native range, count, stride, source identity and matching complete bytes for the
two eyes. This is still a CPU snapshot check, not a GPU execution-position proof.

`StereoGeometryCommands` builds compact private instance streams, retains the
original index and non-instance vertex resources, and can emit geometry commands
into a direct command list. Rebasing requires a verified shader contract: one
per-instance stream at slot 7, step rate 1, and no extended start-instance shader
semantics. The caller must still provide the correct PSO, constants, dynamic
state, array targets, barriers and scheduling. It does not replace game draws.

The new WARP GPU test compares a controlled reference shader using native
StartInstanceLocation values 3 and 9 with compact streams using zero. Nonblack
128x64 targets match byte-for-byte, including SV_InstanceID behavior. The test
also poisons caller CPU data and drops the prepared wrapper before submission to
check independent ownership. This is not a comparison of original game shaders
against a merged live pass.

`prepared-geometry-60`, PID12428, passed all 16 checks:

| Evidence | Result |
| --- | ---: |
| Matched complete native instance pairs in argument plane 3 | 102 |
| Selected draws with strong geometry ownership | 204 / 204 |
| Shared constant bank records / descriptors / bytes | 102 / 408 / 9,472 |
| Prepared geometry commands / private instance bytes | 102 / 45,264 |
| Largest matched native instance batch | 71 |
| Prepared commands recorded in the game | 0 |
| Native game draws replaced | 0 |

`geometry-normal-61`, PID5536, passed all nine ordinary checks after a warning-only
descriptor assignment cleanup. Stable HUD confirmation required the world to stay
ready, the native menu to stay closed, fresh sources and successful OpenXR HUD
submissions. It measured 1,016 ms, 88 submissions and 55 source advances; a cached
HUD source or stalled submissions do not satisfy the readiness gate. Focus was
verified on the game after 5.006 s and on Codex 1.126 s later. The HUD unit tests
also cover stale/future sources, menu state, torn publication, world loss, crash
checks and diagnostic-control restoration.

The normal run had zero family assets, extra binding observers, retained constant
uploads, bank requests and geometry requests/resources. Both-eye preview checks
passed. No crash occurred in runs 59-61. After each batch the owned game was
closed (termination after the close timeout where necessary), controls and prior
preview settings were restored. User rendering settings were preserved.

Current DLL SHA256:
`8352e2a349712f8e07d03528495c7d4c9092f12f77d91bbb4ba762d0f466748c`.
PDB: `c94416e114b0d4a2adcbfee9ab8e75d21e0a1941a899b92451284037d5ff7318`.
Archive: `build/single-pass-20260927/deploy-geometry-normal-61`.
Verification: 21 CTests, 100 single-pass Python tests and 13 launcher Python tests.
There is still no live merged pass, shared early CPU preparation or measured FPS
gain. Remaining work includes dynamic state, GPU-position geometry content,
original-shader comparison, array outputs and scheduling without the old late
MAIN wait. No task-owned game, replay or GPU test remains running; no commit/push.

## Complete Prepared-Pass Recorder

`StereoGeometryPass` now combines the paired constant bank, compact owned
instance streams, strong geometry/PSO references, dynamic state and two-layer
RTV/DSV outputs in one ordered recording. Each draw has an explicit view mask
1, 2 or 3. The caller supplies initialized array resources and establishes states,
geometry-content validity and producer/consumer ordering. Recording consumes one
private DIRECT command list; this is not an in-place native-state restoration API
or permission to skip game draws.

Native draw capture now retains the original PSO and captures topology, stencil,
blend factors and VRS state. The new blend-factor hook is part of the opt-in
binding observer. Pipeline family contracts include output formats, sample count,
primitive type, PS presence and restrictions on unobserved dynamic raster state.
Unknown or mismatching relevant state rejects pass construction. Color passes
with a shading-rate image remain unsupported until that image's contents and
scheduling are proven.

The hardware GPU tests use independently compiled original and paired variants of
a controlled shader, not two executions of the same converted shader. Three
prepared draws with masks 3/1/2 are compared against the corresponding separate
eye draws. Color, depth and stencil planes match byte-for-byte; buffers have
nonzero instance origins, differing camera constants, blend factors, stencil
references and scissors. Caller data is overwritten after Create and the wrapper
is destroyed before GPU submission to verify owned storage and fence lifetime.
The reference images are required to be nonempty and D3D12 validation errors fail
the test. This is a recorder/fixture proof, not yet a replay of real game shaders
against immutable captured geometry.

Live integration uncovered two specific issues:

- `prepared-pass-62`, PID27012: the target descriptor observer was still gated on
  legacy probe assets. Binding-observer mode now also captures RTV/DSV metadata.
- `prepared-pass-targets-63`, PID15824, and `pass-validation-64`, PID8536: the native
  PS-less depth pass retained a VRS image. The explicit validation record isolated
  only that unsupported state bit (16384), with the first selected draw rejected.
  Its family is asset 42 (one-based), whose verified native PS size is zero.

For a PS-less PSO only, VRS is normalized to 1x1 with no image in the private
recording. This does not change depth/stencil coverage, which remains at full
sample resolution under VRS according to the
[DirectX specification](https://microsoft.github.io/DirectX-Specs/d3d/VariableRateShading.html).
A separate hardware regression renders the original PS-less pass with an actual
2x2 shading-rate image and the prepared pass without it; depth and stencil match
exactly. Passes with a PS still reject unproved rate images. No global VRS setting
or image quality setting was changed. These rejected runs did not crash and were
closed by the test wrapper.

`prepared-pass-vrs-65`, PID7552, then passed all 16 launch checks:

| Evidence | Result |
| --- | ---: |
| Matched full native instance pairs | 89 |
| Selected draws with geometry and dynamic state owned | 178 / 178 |
| All captured draws with original PSO/dynamic state owned | 2,806 |
| Constant bank records / descriptors / bytes | 89 / 356 / 8,960 |
| Private instance bytes | 36,720 |
| Complete prepared pass / validation failures | 89 draws / 0 |
| Target layers | 2 |
| Prepared pass recorded or executed in the game | No |

This constructed a compatible subset of one native pass, not the entire scene or
the whole native pass including every material and eye-only draw. Neither target
contents nor geometry contents were moved to a new GPU position. Immutable
geometry acquisition, original game shader image comparison, complete pass order,
target initialization/copy-back and common early CPU preparation remain required.
No whole-frame FPS gain has been established. The old late-MAIN wait remains off.

Installed DLL SHA256:
`cf990828439a4e002b9d60b5394c88fcc063bbae805d72d7781113314db81425`.
PDB: `e814da46617f869deece10ec46d88e7cfdbdf7b2d0794ca871334bb66fd8ad82`.
Archive: `build/single-pass-20260927/deploy-prepared-pass-vrs-65`.
Verification: 23 CTests (including both hardware pass comparisons), 101 single-pass
Python tests and 13 launcher tests passed. User rendering settings were preserved.

`prepared-pass-normal-66`, PID17736, passed all nine ordinary launch checks on the
same DLL: HUD 1,015 ms / 88 successful submissions / 54 source advances, both-eye
1280x720 preview, repeat packet capture, restored controls and no crash. Family
assets, extra binding observers, constant uploads and geometry capture resources
were all zero. Both run 65 and run 66 were visually inspected: scenery and hands
are present in both eyes. These previews do not compare native versus merged game
rendering, and their transient FPS counters are not performance benchmarks.
Both owned games were closed after testing; prior preview settings were restored.
No game, CrashReporter, capture replay or standalone GPU test remains running.
The work remains uncommitted; no push was made.

## Geometry Copied at Its GPU Position

The next checkpoint addresses mutable geometry rather than treating strong
resource references as a content snapshot. Native IA decoding now retains the
current registry handle and command context. At the actual draw, read-only lookup
of `context+0x568` obtains the buffer state; the command-list identity, resource,
GPU range and class must still match, and pending barriers at `context+0x534`
must be empty. This lookup never calls the native insertion helper. EXE hashes
cover the IA functions, state-use helpers and the map implementation.

`geometry-state-67`, PID8448, passed 16 checks but established that none of the
selected complete draws had all states known: their static buffers use native
class 1, which deliberately bypasses both state-map update paths. Upload heaps
have the fixed GENERIC_READ contract; dynamic buffers had native state 1.
Creation code at `0x220110` chooses COMMON/GENERIC_READ for class 1, and fixed-read
pool constructors `0x21DCD4` and `0x21E238` use GENERIC_READ. These complete function
bodies are now also hash-checked. API creation receipts are recorded when available;
an observed incompatible initial state is rejected. Early allocations without
receipts use the verified native fixed-read allocator contract, not a generic
fallback for unknown D3D12 resources. This fallback requires a live matching
class-1 binding, DEFAULT heap and no UAV flag. Read-state status 8 denotes the
receipt path, 9 the early native-constructor path. A new EXE fails closed.

The buffer read/promotion distinction follows Microsoft's
[resource-state rules](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12).
Enhanced barriers are not mixed into native recording: their
[legacy interop rules](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html#interop-with-legacy-resourcebarrier)
still require a known legacy transition to COMMON and would not solve an unknown
state. The native state reader is specific to this engine/version, not a general
cross-queue resource-state tracker.

`GeometrySnapshot` records exact index/vertex views and only the used instance
subrange into an owned packed DEFAULT buffer, followed by readback. Explicit
source states are restored; fixed/implicit generic-read sources need no source
transition. No graphics root, PSO, viewport or target binding is changed. Copies
are not deduplicated across draw positions. A snapshot cannot be consumed until
the original command recording has been reset/destroyed and its exact submission
fence is complete. A replay, including a duplicated list within one submission,
is rejected. Polling does not add CPU or GPU waits to the game.

`NativeGeometrySnapshots` wires the bounded acquisition to actual native draw and
queue submission points. `--freeze-geometry` is opt-in and requires the existing
constant-capture mode. The allocation budget is 256 MiB, with an 8 MiB per-draw
logical limit. The bank builder uses only sealed GPU snapshots in this mode. It
compares every used geometry byte between the eyes, verifies GPU instance bytes
against the original CPU capture, and only then shares equivalent owned buffers
for the prepared pass. Ordinary rendering still does not execute that pass.

The new GPU regression writes A to a source buffer, snapshots it, overwrites it
with B, snapshots B, restores A, snapshots A again, then overwrites the source
again. It verifies the A snapshots pair and the B snapshot does not. It also
checks compact instance origins, stale CPU instance data, pending fences, source
recording sealing, reset-generation handling, duplicate-list rejection and the
handoff to `StereoGeometryCommands`. D3D12 validation errors fail this test.

`frozen-geometry-68`, PID20460, acquired 88 complete snapshots but rejected 320
early static allocations whose creation preceded the API observer. It was closed
without a crash and is not counted as a successful batch. After adding the
verified early-allocation path, `frozen-static-69`, PID19724, passed all 16 checks:

| Evidence | Result |
| --- | ---: |
| Completed and sealed GPU geometry snapshots | 408 / 408 |
| Copied logical geometry bytes | 44,233,920 |
| Selected native draws with complete GPU copies | 156 / 156 |
| Byte-identical two-eye geometry pairs | 78 |
| Prepared pass records / constant descriptors | 78 / 312 |
| Owned constant bytes / compact instance bytes | 9,984 / 33,936 |
| Native game draws replaced | 0 |

Thus GPU content acquisition is now demonstrated for the selected pass subset.
This is not yet an image comparison using the original game shaders, a whole-pass
replacement, common early CPU preparation or a whole-frame FPS improvement.
The immediate next step is an original-root/one-eye binding path for the owned
constant bank, then original native PSO versus paired PSO rendering of these
immutable inputs into private targets. Production scheduling before both-eye
consumers, target initialization/copy-back, full material coverage and shared CPU
preparation remain required; the diagnostic geometry copies are not the intended
permanent optimization path.

`frozen-normal-70`, PID16944, passed all nine ordinary checks after tightening
missing-receipt error handling and duplicate-submission rejection. HUD: 1,015 ms,
87 successful submissions, 54 source advances. Family assets, extra binding
observers and geometry capture resources were zero; freeze enable was zero.
Both run 69 and run 70 previews were inspected and showed scenery/hands in both
eyes. Their transient FPS counters are not benchmark evidence. Both games were
closed after testing; settings and prior preview dimensions were preserved.

Current DLL SHA256:
`72d408ab730d9e8f0f499ba90c4f8f6ff5b563a32256dd652c169917053d6272`.
PDB: `02b3b21cd4bf720ab7090f2b7ad0d8b5d090d23003de9798991e432a9a253d87`.
Archive: `build/single-pass-20260927/deploy-frozen-normal-70`.
Verification: 24 CTests, 103 single-pass Python tests and 13 launcher tests passed.
No game, reporter, replay or GPU test is running. No commit or push was made.

## Original Game Shaders Compared on GPU

`StereoBindingBank` now has an opt-in original-root binding path. `BindSourceEye`
uses the same owned constant bytes as the paired path but binds the native root
signature and native CBV table layout for one eye. Finite CBV tables support
nonzero descriptor offsets and multiple ranges; unused entries are initialized
with null CBVs, using the documented
[unbound descriptor behavior](https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html).
This reference path currently excludes mixed SRV/UAV/sampler tables and duplicate
source-offset bindings; it is not full material-reference support.

`StereoDepthComparison` renders the original PSOs separately for the two eyes,
then renders the paired PSOs through `StereoGeometryPass`. It uses GPU-complete,
immutable geometry and owned constants, private D32S8 targets, a private queue and
a completion fence. It neither waits on the CPU nor changes native output targets.
Recording-resource ownership is detached after submission so the private list and
its retained storage do not form a reference cycle. The GPU submission batch keeps
them alive until completion. A new comparison cannot replace one still in flight.

Depth clear values come from the captured PSO depth-test contract. Only known,
depth-writing, compatible depth-only PSOs are admitted. Both depth and stencil
planes are copied, following the
[planar depth/stencil layout](https://github.com/microsoft/DirectX-Specs/blob/master/d3d/PlanarDepthStencilDDISpec.md).
Comparison covers defined depth/stencil components, not undefined padding bits.
Per-eye output must be nonempty, finite and exactly equal. NaNs, invalid counts,
incomplete results and blank reference images cannot pass the verifier.

The standalone hardware test now compares three paths: manually bound original
shader, original shader bound through the owned bank, and paired shader. Its CBV
is deliberately at offset 1 in a three-descriptor table. A negative control swaps
the paired PSO's array-layer routing and must report differences in both eyes.
This checks the verifier itself, not just its success path.

`native-depth-71`, PID11424, exposed the native b1 layout: source parameter 0,
descriptor offset 1. The initial singleton-only reference path rejected it before
rendering. Finite table reconstruction fixed this without changing shader math.
The run was closed without a crash and is not counted as a successful batch.

`native-depth-tables-72`, PID27776, passed all **17** checks, including the actual
original-game-shader comparison:

| Evidence | Result |
| --- | ---: |
| Complete sealed GPU geometry snapshots | 408 |
| Captured logical geometry bytes | 44,233,824 |
| Selected original native draws / paired draws in private targets | 222 / 111 |
| Target resolution per eye | 1485 x 1485 |
| Reference depth pixels written, VRCAM / MAIN | 99,070 / 99,001 |
| Different depth pixels, VRCAM / MAIN | 0 / 0 |
| Different stencil pixels, VRCAM / MAIN | 0 / 0 |
| Maximum absolute depth error, VRCAM / MAIN | 0 / 0 |
| Invalid depth values, VRCAM / MAIN | 0 / 0 |
| Native game draws replaced | 0 |

Unlike the earlier recorder fixture, this uses the game's original retained PSO
and its actual converted shader family on the captured game geometry/constants.
It establishes output equivalence for this selected depth-pass subset in private
targets. It does not establish complete scene coverage, initialization from the
live native depth target, safe placement before both-eye consumers, removal of
duplicate native draws, common early CPU preparation or any whole-frame FPS gain.
Production scheduling and shared preparation are still the next major work; the
late-MAIN diagnostic wait must not return as a permanent solution.

`native-depth-normal-73`, PID21816, passed all nine ordinary checks using the same
DLL. HUD: 1,015 ms / 85 successful submissions / 55 source advances. Family assets,
extra binding observers, constant uploads and geometry capture resources were
zero. Freeze enable was zero; ordinary rendering did not request a comparison.
Both run 72 and run 73 previews were inspected, with scenery/hands in both eyes.
The previews show ordinary game rendering, not the private comparison targets.
Both games were closed after testing and the prior preview settings restored.

Current DLL SHA256:
`c56832ec48b1f8673b5533d02ac735b1bc8254d94578e73148d05ba5b1f40ab6`.
PDB: `e28ad21625e17ed2f8896a9ff99bc408e11fc163915cde4089a146a6b7d202d1`.
Archive: `build/single-pass-20260927/deploy-native-depth-tables-72`.
Verification: 24 CTests, 105 single-pass Python tests and 13 launcher tests passed.
No task-owned game, reporter, capture replay or GPU test remains running. Settings
were preserved; no commit or push was made.

## CPU Preparation Order and Rejected Graph Co-Selection

`NativePreparation` now records owned output arrays at gather, filter, finalize
and stage completion, together with graph selection, culling calls and native
packet consumption. Records are 112 bytes, with bounded 16-byte packet storage.
Per-thread scopes preserve nesting and isolate unclassified calls; a rejected
nested context cannot attribute its output to its parent. The +0x10 argument word
is the pass/plane identifier, while +0x14 varies with the work context and is not
used as a cross-eye pass key. Unknown helper calls remain classified separately.
Only an explicit bounded request activates copying. The reader restores controls
on success, failure and process exit without masking the original crash.

`preparation-order-74`, PID17412, exposed two collector issues: the consumer hook
was still gated only on the older packet recorder, and unclassified preparation
contexts incorrectly set a global failure. That run was closed and not accepted.
After correcting those issues, `preparation-consume-75`, PID27608, passed all ten
checks. Its two-frame trace contained 734 events, 112 complete preparation scopes
and 62 consumption calls. The depth pass had exactly one matching complete
prepared list per eye/frame. Native cull reuse stayed disabled.

The trace changes the integration decision:

- Graph preparation selects one active view at a time, alternating VRCAM and MAIN
  through the same transient storage address.
- In the first observed frame, VRCAM depth preparation ended at event 120 and its
  234 packets were consumed at event 130. MAIN graph preparation completed only at
  event 222; its depth list ended at event 314 and was consumed at 315.
- Both eyes had 234 depth packets, but their raw packet sets had zero common
  packets at gather. Transient pool indices cannot be treated as common mesh IDs.
- MAIN's plane-6 output was one prepared list consumed in four partitions, so an
  exact whole-array match is not the right check for each individual consumer.
- The observed timestamps include instrumentation cost. They are order evidence,
  not CPU speedup measurements or the proposed late-MAIN wait.

Disassembly identifies the outer scheduling boundary: `0x219730` calls
`GraphContextPrepare` at `0x2199C8`, then builds/executes per-view render graphs.
The manager does support multiple views and allocates per-view containers, but
the outer render request also supplies output resources and composition context.
Those contracts are not interchangeable with a list of view names.

A one-shot experiment supplied an owned two-name request vector to the original
preparer, leaving the caller's vector and camera fields unchanged. Runs
`paired-prepare-76` (PID15996) and `paired-request-source-77` (PID15288) never entered
that path: the initial guard incorrectly rejected MAIN's valid zero CName. The
existing `is_main_view` implementation and captured request vector verified this.

After correcting the guard, `paired-main-name-78`, PID19072, crashed. This is a
test-induced integration failure, not classified as random. Exact report:
`Cyberpunk2077-20261001-082814-19072-25964`; exact DLL/PDB archive:
`build/single-pass-20260927/deploy-paired-main-name-78`.

The fault is `Cyberpunk2077.exe+0x20A3FA` in DrawComposition. The dump proves:

- The work-context view has name 0 (MAIN).
- The resource key at RBP+0x18 is `0x31CF52F9`.
- The resolved texture handle at RBP is 0.
- RSI is `0xAFFFFFFF50`, the unsigned `(handle-1)*0xB0` offset; the fault reads the
  dimensions at `[RSI + registry + 0x2F226]` from that invalid record.

The paired-selection telemetry globals were not included in the minidump, so the
exact number of successfully prepared views/containers cannot be inferred from
the dump. The native instruction path nevertheless establishes the missing MAIN
composition resource. Appending MAIN to an RTT render request is not a complete
paired CPU-preparation integration. Skipping DrawComposition or lending an old
target would conceal this dependency, not fix it.

The unsafe two-name invocation has been removed. `PreparePair` now reports
admission state 4 and forwards the original request unchanged until both output
bindings have a real owner. `--check-pair-admission` tests this rejection; the old
`--pair-prepare` spelling is only an alias for that test, not an enable switch.
This prevents the newly introduced crash path; it does not claim that early
paired preparation is working. CrashReporter from run 78 was closed and the game
was confirmed absent.

`paired-admission-79`, PID2184, passed all eleven checks with the rejection path:
native work unchanged, controls restored, fresh post-test HUD (1,109 ms, 94
successful submissions, 65 source advances), both-eye images, repeat packets and
no crash. The game was closed and the preview settings restored. This is a
negative/admission test, not a successful paired-rendering test.

Current DLL SHA256:
`8d1d1d569bb89f7552c54d0b149fee45849b631c1deda7b35b4d98ec083522b6`.
PDB: `700f4ba3a301b4cc77aa52dbb522b94abf37de963fc4edc2bf0a51c64bf3f8a9`.
Archive: `build/single-pass-20260927/deploy-paired-admission-79`.
Verification before the final normal launch: 25 CTests, 108 single-pass Python
tests and 13 launcher tests passed. No native draw replacement, common early CPU
preparation or FPS gain is claimed. The next integration must separate CPU
visibility/material preparation from the full per-view output/composition graph,
or explicitly construct and own both output domains at the outer request level.

`preparation-normal-80`, PID4976, then passed all nine ordinary checks with the
same corrected DLL. HUD: 1,016 ms / 87 successful submissions / 55 source advances.
Preparation capture and pair requests were zero, as were family assets, extra
binding observers and retained geometry/constant resources. Both run 79 and 80
previews were inspected: scenery and hands are present in both eyes. The game
was closed after each batch and prior preview settings restored. No game,
CrashReporter, RenderDoc replay or GPU test remains running. No commit or push.

## Owned Native CPU Query and Fine Visibility

`NativeCpuCollector` now owns a separately allocated native collector wrapper,
metadata and copied camera/view flags. Its factory (`0x916C78`), initialization
(`0x79B8B4`) and deleting destructor (`0x91AD80`) are executable-hash checked.
The original view and original metadata are never replaced. `cpu-collector-81`,
PID4540, passed 11 checks including native initialization/destruction. Source
and clone header limits were 584, with 82 metadata bits. These values are not
interpreted as counts of prepared meshes.

The next mode runs the native query preparation (`0x79E748`) with its own copied
work context and native job scope. It retains the frame owner, source query and
both begin/end job groups. The native Advance operation can move pending work
from end to begin, so checking end alone is insufficient. Cleanup waits for both
source and private groups without blocking a rendering thread; the native scope
destructor links the private work into the live parent dependency. A failure
without proven completion retains one bounded diagnostic owner through process
exit instead of destroying storage still referenced by tasks.

`cpu-query-82`, PID896, passed 11 checks. The independent queries had matching
type RVA `0x2B11A78`, all eight frustum planes and defined query metadata matched,
both job groups completed, and native owners were released. This does not prove
equal occlusion-buffer contents or equal visibility by itself. DLL/PDB archive:
`build/single-pass-20260927/deploy-cpu-query-82`; DLL SHA256:
`349f8e44d4779ced5335df1636fd2f12484e24760688f49f9a413bd111850d3b`.

`NativeFineVisibility` copies actual native 40-byte candidate records and the
ordered ID suffix appended by the original fine test. The same native fine test
then runs against the private query on these owned ranges. Its output has room
for every candidate before entry, so no native allocator owns/grows our vector.
Duplicates and order are preserved. Input validation, changed container shape,
capture overflow or a missing range reject the result. Only an explicit bounded
request enables this path; ordinary game calls still execute exactly once.

`cpu-fine-83`, PID4648, failed the startup HUD gate before the CPU experiment was
armed: QuickBoot reported a player/world, but native menu stayed 1 and HUD source,
prepare and submission counters stayed zero. No crash was detected. The test
correctly refused readiness and closed the game. Its log is retained alongside
`startup-evidence.json`; the original transition ring was disabled in this
non-family launch, so this run does not establish why native menu remained set.
Owned launches now enable the bounded menu transition diagnostic independently
of shader-family assets. No code forces the menu value or relaxes the HUD gate.

`cpu-fine-startup-84`, PID25520, passed all 11 checks without a Continue fallback.
The private query reproduced 1,082 native visible IDs across 180 ranges and
2,428 candidates, with zero differing ranges. Both original/private occlusion
contexts and their active flags were nonzero. Owned range payload was 105,776
bytes. Initial HUD: 1,031 ms / 89 submissions / 53 source advances. Post-probe HUD:
1,094 ms / 93 submissions / 70 source advances. Both-eye scenery/hands were
visually inspected; these preview images still show ordinary native rendering.
The game was closed, controls zeroed and prior preview settings restored.

Run 84 DLL SHA256:
`90fcdd4f42c2545ef39dd6459dbe0b53ea8c79cfd45f1567d561e9527a20cc97`.
PDB: `577bd803b973022b6afd29b95780c54571334cf640c579eace663ec5e0d3434d`.
Archive: `build/single-pass-20260927/deploy-cpu-fine-startup-84`.
At this checkpoint 26 CTests, 111 single-pass Python tests and 13 launcher tests
passed. No native draw was replaced and no whole-frame FPS improvement claimed.
Fine ranges alone exclude coarse-rejected groups; the next check owns the entire
native scene candidate pool and evaluates coarse plus fine visibility itself.

## Independent Scene-Pool Traversal

`NativeSceneVisibility` now owns the hash-pool traversal input at scene+`0x7E0F0`,
including every bucket AABB and 40-byte candidate record, not just buckets that
passed one eye's coarse test. It validates pool size/capacity/stride, slot and
chain indices, duplicates/cycles, finite ordered bounds, candidate capacities,
bounded allocations and the unchanged header. The native coarse/fine functions
evaluate this owned pool into an independent list of visible IDs. Preparation
callbacks outside this pool and material/draw-command preparation are not covered.

The source scene-test hook retains both native culling job groups after submitting
the original work, so comparison also waits for all observed materialization
jobs. It does not hold its mutex across the native scheduler: a submitted worker
may execute inline. The original culling output remains untouched, and a source
query replay provides a second control alongside the private-query replay.

Two rejected checks caught integration mistakes before any replacement was used:

- `cpu-scene-85`, PID1216, copied 1,989 buckets and 6,897 candidates, but the new
  coarse-call declaration incorrectly narrowed its third parameter to a byte.
  The archived DLL emits `mov r8b,1`; the engine reads `r8d`. This violates the
  actual parameter-width contract, consistent with the
  [Microsoft x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention).
  It produced 571 visible buckets, so that test was rejected. The declaration is
  now `uint32_t`, also pinned by the C++ test callback type.
- `cpu-scene-abi-86`, PID16076, correctly produced 179 visible pool buckets and
  1,068 IDs, but comparison still included all 180 observed fine ranges / 1,082
  IDs. The new provenance control subsequently established that one 14-ID range
  is outside this pool. It must be tested separately, not counted as a missing
  bucket or silently discarded from a claim of whole-scene coverage.

Neither failure was a crash. Each wrapper closed its game and restored preview
settings immediately after the rejected check. The CPU collector/query and fine
range comparison still passed in both reports.

`cpu-scene-provenance-87`, PID2272, passed all 12 checks. Three complete CPU probes
ran within one owned launch, each with fresh native allocation, query jobs,
culling jobs and cleanup. Each produced:

| Measurement | Result |
| --- | --- |
| Owned pool buckets / candidates | 1,988 / 6,897 |
| Visible pool buckets | 179 |
| Native / private pool IDs | 1,068 / 1,068 |
| Different pool IDs | 0 |
| Original-query replay differs from native / private | No / No |
| Separate observed fine range outside this pool | 1 range / 14 IDs |
| All observed fine ranges / IDs, including outside-pool range | 180 / 1,082 |
| Different fine ranges | 0 |
| Owned pool payload bytes | 339,496 |

Both source/private software-occlusion contexts were active. Initial HUD was
stable for 1,016 ms with 88 submissions and 53 source advances; post-probe HUD
for 1,094 ms with 92 submissions and 75 source advances. Focus timing was verified
at +5.020 s for the game and +1.064 s thereafter for Codex. All request/state
controls were restored to zero, the game closed and preview settings restored.

Run 87 DLL SHA256:
`d06d02094ac18ca47483a318d4be9193a3f62a2e5ece1f335ddb215c46c2b45f`.
PDB: `ff65b536582beeaacbbce0f621de95a88268963146d7464370ebb0a8d68d5c70`.
Archive: `build/single-pass-20260927/deploy-cpu-scene-provenance-87`.
Verification: 26 CTests, 112 single-pass Python tests and 13 launcher tests passed.

This is an independently evaluated, complete native candidate pool for the same
eye, with owned inputs and exact native visibility. It is not yet an early paired
MAIN/VRCAM query, common material preparation, replacement of native commands or
an FPS improvement. The next step is constructing the peer query at this early
boundary using the already validated private camera/pose path, checking it
against MAIN's later native result, and sharing the pool traversal between eyes.
No late-MAIN wait or full-graph two-view co-selection has been re-enabled.

`cpu-scene-normal-88`, PID16608, passed all nine ordinary launch checks with the
same DLL and no CPU probe requested. HUD: 1,016 ms / 87 submissions / 56 source
advances. Both run 87 and run 88 previews were visually inspected: the scene,
hands and HUD content are present in both eyes. The ordinary launch had no family
assets, additional binding observers or retained geometry/constant captures;
CPU collector request/state remained zero. The game was closed, preview settings
restored, and no task-owned game, reporter, capture replay or GPU test remains.
No commit or push was made.

## Early Peer Query and Mutable Candidate Bounds

The opt-in `--cpu-peer` probe now creates both private queries while VRCAM's
native query scope is still live. MAIN's published pose at view+`0x1E20` is used
with the existing owned-camera helper. Width/height must match; the verified
camera rebuild (`0x1E412C`, size `0x47F`, FNV64 `4D1D8CEDD636AE76`) touches only
the private copy. Query setup independently clears jitter before deriving its
frustum. Its only reads through context+`0x18` are viewport width/height, supplied
through private storage. Source camera/history/counters are never overwritten.

At the VRCAM scene-pool boundary the probe checks that both native query job groups
have finished, then evaluates the same owned candidate pool for both eyes. It
does not wait for MAIN. The later native MAIN query and culling outputs are
retained for comparison. A Present epoch rejects matching a later Present
interval; this is not by itself proof of an unchanged native simulation snapshot.
Query, culling and owner lifetimes are tracked independently for each eye.

`cpu-peer-89`, PID28480, demonstrated the intended early order:

- Both private queries were complete at VRCAM's scene-culling boundary.
- MAIN's early list was computed before its native query was observed.
- Early VRCAM / MAIN pool results were 1,068 / 1,043 IDs; both matched their later
  native pool results exactly. The multiset intersection was 1,038 occurrences,
  with 35 single-eye occurrences. These are visibility IDs, not GPU draw counts.
- MAIN's private/native query type, frustum planes and defined query metadata
  matched byte-for-byte. Fine tests also matched all observed MAIN ranges.
- The input-pool equality gate failed, so the whole peer probe was rejected.

`cpu-peer-inputs-90`, PID12504, split that input check into consumed fields and
non-input storage. Verified coarse code reads AABB XYZ, not W lanes; verified
fine code reads candidate ID, six bounds floats and two masks, not the final four
bytes. Allocation addresses, unused W lanes and this unused tail are reported
but are not culling inputs. This did NOT make the peer test pass: nine candidates
had different bounds between VRCAM and MAIN. All other checked input categories
were identical. The requested three repetitions stopped after this first failed
check; no repeated success is claimed.

`cpu-peer-bounds-91`, PID14800, retained before/after bounds and native preparation
entry points. Eleven changed candidates were observed in this later run, all
with vtable RVA `0x2AECEF0` and preparation method `0x1E77D0`. Changes are real,
including sub-millimetre coordinate differences and larger vertical/extents
changes on distant objects. They are not simply a uniform stereo-IPD offset or
unused padding. Early/native visibility still matched in this scene, but that
does not justify ignoring the changing bounds in other scenes or head poses.

Relevant native preparation path established by disassembly:

- `0x1E9120` takes a work context plus a visible-ID range, initializes a gather
  context with the output collector, and calls `(ID-16)->vtable+0xF8` per object.
- For the changed objects this is `0x1E77D0`. It tests feature bit `0x51`, invokes
  `0x1E86B8` and builds view-dependent rendering descriptors.
- `0x1E86B8` checks a per-view cache under view+`0x1D98`, using the gather-context
  value at `+0x2C`, then calls virtual+`0xF0` when needed.
- That virtual is a jump from `0xAE5670` to `0x1E8A54`. This function calculates
  view-distance values at object+`0xA0/+0xA4`; it does NOT explain or write the
  observed bounding-box changes. Their writer/update phase is not yet identified.

The next integration must account for mutable input before claiming common
preparation for the complete scene. Neither relaxing bounds equality nor relying
on coincidentally unchanged visible IDs is sufficient. No early result is fed
into native rendering, and no production draw/culling work is skipped yet.

`cpu-peer-normal-92`, PID13552, failed before arming any renderer experiment:
world/player proof was present, native menu stayed 1, and HUD source/submission
counters stayed zero. No crash was detected. The 64 diagnostic menu records had
been filled by repeated identical requests, hiding later transitions. The bounded
ring now records only real mode changes. This is diagnostic correction, not a
claim that the intermittent native startup-menu condition has been fixed; the
HUD gate remains mandatory and no menu-state override has been introduced.

Archives for rejected peer checks: `deploy-cpu-peer-89`, `deploy-cpu-peer-inputs-90`
and `deploy-cpu-peer-bounds-91` under `build/single-pass-20260927`. Their owned game
processes were closed and preview settings restored. No crash was reported.

The follow-up `cpu-peer-normal-93`, PID15328, passed all nine ordinary checks with
the experimental peer path unrequested. Initial HUD: 1,016 ms / 88 submissions /
56 source advances. The preview was inspected with scene and hands in both eyes.
The game was closed, all diagnostic controls reset, and preview settings restored.
No task-owned game, reporter or capture replay remains running. Settings hashes
still match the deployment manifest. No commit or push was made.

Current DLL SHA256:
`64faad00a88fe158a6b735802aed7bc54790bf2db26daf5486bb4c65c4d49bed`.
PDB: `dc7586ea428a984a57c11a34d033fc7d61512adf81527dc40d48e19f1908a05d`.
Archive: `build/single-pass-20260927/deploy-cpu-peer-menu-events-93`.
Local verification: 26 CTests, 113 single-pass Python tests and 13 launcher tests
passed. The broader peer acceptance test still deliberately rejects changing
bounds; no full shared-preparation or performance claim is made.

For the next scheduling step, official references were rechecked: Unreal's
[UpdateAllPrimitiveSceneInfos](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FSceneInterface/UpdateAllPrimitiveSceneInfos)
explicitly covers scene additions/removals and transform changes; Unity's
[BRG bindings](https://github.com/Unity-Technologies/UnityCsReference/blob/master/Runtime/Export/Rendering/BatchRendererGroup.bindings.cs)
validate that the returned culling JobHandle depends on outstanding buffer jobs.
Our inference for this port is to identify and own the scene-update dependency
before publishing common culling inputs, not assume that two queries between the
same pair of Presents automatically see one immutable scene. These references
do not establish the missing REDengine update boundary; native evidence must.

## Bounds Writer Attribution and Particle Dependencies

The direct spatial-pool updater is `0x1E558C` (size `0x1A9`, FNV64
`92FA499B9D21DB2C`). It reads the object's bucket key/element index and writes six
floats at candidate+`8..0x1C`. A false return can still mean the candidate bounds
were written: it also reports whether the coarse bucket needs updating. Its
three native call sites are `0x1DA28E`, `0x1E57E3` and `0x29381A0`.
The latter two are bounds-setting/update methods on the render proxy.

`NativeBoundsTrace` records only one requested peer probe. It is now installed
only with `--bounds-trace` / `CVR_STEREO_BOUNDS_TRACE`; ordinary launches do not
install this extra detour. The entry observer copies inputs, caller, native node,
thread, timestamp and an unwound native stack. It never changes the spatial pool.

The first ordinary C++ wrapper was not sufficiently conservative about the native
register contract. Runs 94-97 repeatedly failed startup with the native loading
layer still active. A control with the bounds hook uninstalled, run 98, loaded
successfully. The hook was replaced by `NativeBoundsThunk.asm`, which saves all
volatile GPRs, XMM0-5 and flags, restores them after the observer and tail-enters
the original routine with its original stack/return address. A dedicated MASM
fixture deliberately clobbers these registers in the observer and verifies that
the original sees the exact input values and stack. This fixture passed.

The new trace format is **entry-only**. It does not claim native completion times
or atomic snapshot proof; `bounds_trace.py` reports call entries between/during
snapshot markers and rejects overflow or missing boundaries. The older begin/end
C++ wrapper was removed rather than retained as an alternate mode.

Separate startup observations identified the native menu source: `0x29CB44`
consults a getter at `0x29D098`. That getter locks a weak owner and reads owner+
`0x204`; its owner vtable is `0x2CCE228`, whose native RTTI registration identifies
`inkLoadingLayer`. Failed starts had state 2; successful starts had state 4 and
the native menu value became zero naturally. Visible loading controllers alone
therefore do not establish completion. The added observation is read-only and
test-environment gated. QuickBoot's actual load method and all native menu values
were left unchanged. This does not claim every earlier intermittent startup
failure had the new bounds wrapper as its cause.

`cpu-bounds-abi-99`, PID18172, reached stable gameplay with the preserving bounds
hook installed: HUD 1,016 ms / 87 submissions / 57 source advances, no Continue
fallback. Its full peer test still rejected mutable inputs, as intended. The
trace contained 20 records, 14 call entries and eight entries between VRCAM's
snapshot end and MAIN's snapshot start. All eight changed IDs were accounted for:

- All eight came through `0x1E5738 -> 0x1E558C` (return RVA `0x1E57E8`).
- Six were inside native node `SimulateOnScreenCPUParticles`, work `0xCBB23C`,
  through `0x136C44 -> 0x137790 -> 0x1E5738`.
- Two came from the background worker `0xB9C200`, through
  `0x137564 -> 0x137790 -> 0x1E5738`.
- No observed call entered during either copied snapshot. This is weaker than a
  proof that no write overlapped a snapshot, because only entry is observed.
- The raw scene counter at manager+`0x28` was 483 at both boundaries. Its equality
  must not be interpreted as proof of immutable data or identical simulation time.

Disassembly of these paths establishes the relevant dependency domain:

- `0x136C44` partitions the emitter table at scene+`0x7E190`, count `+0x7E19C`,
  and selects its per-view metadata bitset. This explains the previously observed
  82-bit collector metadata; it is not a count of scene meshes.
- `0x137564` ORs emitter visibility bitsets across views, then processes their
  complement for the background/offscreen path.
- `0x137790` rebuilds emitter bounds from its instances, calls `0x137868` for each,
  then publishes the aggregate bounds through `0x1E5738`.
- `0x137868` handles accumulated time at instance+`0x24`, state at instance+`0x18`,
  a state stamp at `+0x7C`, and the global particle epoch at EXE+`0x3438210`.
  It also has onscreen-specific per-view output work (including `0x622C00`).
  Simply skipping the whole second-eye call could lose that work; neither particle
  freezing nor a blanket second-eye simulation skip has been introduced.

The new evidence replaces the hypothesis of arbitrary mesh-bound writes with a
specific native particle simulation/output dependency. Common preparation must
account for it, either by staging its shared work before publishing paired inputs
or by explicitly preserving the genuinely per-eye particle output stages. The
complete renderer objective remains unchanged; a passing static-only check would
not establish full single-pass operation.

Run 99 archive: `build/single-pass-20260927/deploy-cpu-bounds-abi-99`.
DLL SHA256: `41ce1431d395ac920e67d0101cb5463c1dbdd31cc734e1f605477fafdeb82325`.
PDB: `d18c7ea47f9dbb38cf6d9b20a3662bcbc455be4cbed4fbca4309ca5714ba236d`.
Local verification: 27 CTests, 116 single-pass Python tests and 13 launcher tests.
No native rendering or simulation work has yet been removed by this stage.

`cpu-bounds-normal-100`, PID28184, passed all nine ordinary launch checks with the
same DLL and without the bounds detour requested. HUD: 1,016 ms / 90 submissions /
61 source advances. Both-eye preview was inspected. The game was closed, controls
reset and preview settings restored. DLL, PDB and all four settings files match
the deployment manifest; calibration remained absent. No game, reporter, capture
replay or GPU test remains running. No commit or push was made.

The particle scheduling investigation also rechecked Unity's official
[PrepareCamera documentation](https://docs.unity.com/en-us/engine/6000.6/script-reference/unityengine/vfx/vfxmanager/preparecamera)
and [VFXManager bindings](https://github.com/Unity-Technologies/UnityCsReference/blob/master/Modules/VFX/Public/ScriptBindings/VFXManager.bindings.cs).
Unity explicitly separates per-camera preparation from executing camera rendering
commands, and recommends preparation before culling in a custom SRP. This supports
the architectural distinction needed here; it does not prove how REDengine's
particle instance stamp/time accumulation behaves across its two view invocations.
That native behavior remains to be verified before sharing or rescheduling it.

## Particle Instance State and Native Continue

The preserving entry thunk now also observes `0x137790` without changing its
registers, original stack or fifth (onscreen) argument. The MASM fixture covers
both observer variants and verifies the untouched fifth argument as well.
Particle tracing is still explicitly opt-in. It reads only emitters present in
the native scene emitter table, copies bounded per-instance state, and separately
records the later state when aggregate bounds are about to be published.

`ParticleRecord` is 112 bytes. It records the instance/state identities, view,
onscreen flag, global particle epoch, state stamp, raw scene counter and accumulated
time. The later time probe also captures particle-system delta and the last
parameter-block delta. These fields distinguish repeated function entry from
actual time advancement; call counts alone are not simulation-step counts.

Runs `cpu-particle-state-101` (PID16072) and `cpu-particle-startup-102` (PID18788)
failed the mandatory HUD gate before renderer probes were armed. The native
loading layer remained in state 2. Preserving registers in the earlier bounds
observer therefore did not eliminate the separate intermittent startup problem.
Both games were closed without overriding the native menu value.

Runtime RTTI and menu observations supplied the real Continue route:
`SingleplayerMenuGameController::HandleMenuItemActivate(PauseMenuListItemData)`.
The item has `label`, `eventName` and `action`; the actual Continue item uses
`PauseMenuAction.QuickLoad` (5). QuickBoot now observes the two real `AddMenuItem`
overloads, retains only a recognized Continue item, and invokes that handler once
after menu/save readiness. It releases retained controller/item references after
dispatch. New Game and Exit entries are never selected. A thrown handler does not
fall back to a second load. Missing item observation retains the older direct-load
compatibility path, so successful readiness must still be established by HUD.

The actual Lua file is executed in three new mock-runtime tests using a local
build-only `lupa` dependency: delayed one-shot QuickLoad, rejection of New Game/Exit
activation, and no duplicate load after a throwing menu handler. All passed along
with the existing 13 launcher tests.

`cpu-menu-continue-103`, PID19932, used the native menu path, confirmed by CET's
`resumed via native Continue menu item` log. HUD reached 1,016 ms / 88 submissions /
57 source advances with no synthetic Continue confirmation. It captured 707
particle-state records: 660 entry samples across 330 instances, each encountered
twice, and 47 bounds-publication samples. Particle epochs were 9199/9200 while
the raw scene counter remained 513. No entry already had the current epoch, and
none had a positive accumulated catch-up time. The complete peer test still
rejected the changing bounds; this run is not a passing single-pass acceptance.

`cpu-particle-time-104`, PID25224, again reached native gameplay/HUD via the menu
path. It recorded 709 particle samples, again 330 repeated instances, epochs
9311/9312 and a raw scene counter of 501. For the observed publication samples,
the last parameter deltas were approximately 0.0160557 s for the first epoch and
0.0188122 s for the second. These are instrumented-frame values, not benchmark
timings or a proof that animation runs at twice the intended speed.

Native `0x138008` builds the parameter block at emitter+`0x140`: delta at `+0x80`,
onscreen state at `+0xAC`, update enable at `+0xB4`. The update flag gates portions
of kernels such as `0x138300` and `0x136DD8`; emission update `0x138C98` also reads
it. Some time/state work is outside that gate. This is a concrete point for
separating shared simulation from camera output, not permission to zero the flag
or delta indiscriminately. No simulation flags, epochs, accumulated times or kernel
parameters have been changed by this work.

Run 104 archive: `build/single-pass-20260927/deploy-cpu-particle-time-104`.
DLL SHA256: `7705a7ef2fa5675b2a6855728a55f3be47ea54858c80351aa0c71f1d97c7ca14`.
PDB: `32e56309a087bb78452807a85565ac2ef8aa0ba8670687e3f7a292a93e9b9f0f`.
Verification so far: 27 CTests, 118 single-pass Python tests and 16 launcher tests
(including three real-Lua fixture tests). No native draw replacement or FPS gain
is claimed; the renderer goal remains incomplete.

`cpu-particle-normal-105`, PID2368, subsequently failed the loading/HUD gate even
though `resumeMethod` confirmed the native menu route. The UI-path change alone
therefore does not resolve the intermittent loading-layer state-2 condition.
No renderer probe was armed, no crash was detected, and the wrapper closed the
game and restored preview settings. Keep this failed control in the record; the
two preceding successful loads are not evidence of universally reliable startup.

`cpu-particle-normal-106`, PID10348, reproduced the same failed control. The next
launcher change therefore gates the *initial* resume as well: the real menu/save
callbacks arm QuickBoot, but an expiring run-token handshake releases Continue
only after advancing native loading-layer samples report state 4 for at least
100 ms. No game/global menu variable is written. This distinguishes a constructed
main menu from completion of the initial loading layer.

`cpu-native-ready-107`, PID7512, passed that initial gate and then exposed a real
visible `inkInitialLoadingControllerSupervisor` waiting for Continue. Four short
Space injections did not dismiss it, so the batch correctly failed and closed.
Continue injection now waits 250 ms after verified game activation, rechecks
foreground, holds Space for 250 ms, releases it and waits another 250 ms before
restoring Codex. The initial +5 s game / +1 s Codex focus sequence is unchanged.

`cpu-native-ready-108`, PID27348, passed all nine ordinary checks. It passed the
initial native-ready gate, dispatched `menu-continue`, needed two bounded Space
presses at the visible loading supervisor, and then reached a stable HUD for
1,047 ms / 93 submissions / 74 source advances. This is a successful control of
the revised path, not a claim that all possible startup races are eliminated.
The game was closed and prior preview settings restored; diagnostic controls
were zero. The installed DLL/PDB are still those from run 104.

The run-108 preview was visually inspected with scene, hands and HUD in both eyes.
Final local checks: 27 CTests, 118 single-pass Python tests and 21 launcher tests
(including four Lua-runtime fixtures and focus/input sequencing tests) passed.
All four settings hashes, DLL and PDB match the run-104 deployment manifest.
Preview dimensions returned to 2193x16. No game, crash reporter, capture replay
or GPU test remains running. No commit or push was made.

## Paired Selection Under MAIN's Output Root

The outer dispatcher at `0x219730` selects graph builders from frame data
`+0xF90`. Its job caller returns at `0xAA3957`; the caller retains responsibility
for the native job scope and frame-owner release. MAIN's frame data owns the
composition pair at `+0x168` and output provider at `+0xF88`. A bounded paired
selection now keeps this root intact and passes a local two-name request to
`GraphContextPrepare`. The original request vector is not rewritten.

`main-root-pair-109`, PID13724, passed all 11 checks. Both active views had their
own nonzero collectors, equal 2560x2560 output dimensions and MAIN's owned output
bindings. The later HUD reached 1,015 ms / 90 submissions / 64 source advances.
The trace contained five depth consumptions across two frames: the original
standalone VRCAM, paired MAIN/VRCAM, then the ordinary two-eye frame. Thus this
first test added a third eye render; it was not an optimization.

`merged-root-110`, PID15060, verified the paired warmup but did not suppress a
standalone root. It failed the merge test without a crash. Run 111 accidentally
used that same old DLL after a failed build and is not new-code verification.
The build error was an SEH block inside a function with C++ object unwinding;
the bounded diagnostic read now uses `ReadProcessMemory` instead.

`merged-root-observe-112`, PID13724, supplied the missing live root evidence.
The expected caller `0xAA3957` was correct, but both MAIN and our standalone
VRCAM arrived with build mode **0**, not the native RTT-only mode 4. The VRCAM
request still contained its exact one-name key `0x77AD6D6871650500`; MAIN had
the zero name. The suppression predicate incorrectly required mode 4. It now
accepts only the observed mode 0 and the exact single VRCAM name, after a
verified paired warmup. Tests also reject MAIN, multi-name requests, mode 4,
missing warmup, non-gameplay and a second suppression.

Run 112 also verified startup readiness: 1,047 ms / 93 submitted HUD frames /
80 source advances with native menu 0. The wrapper closed the game after the
failed merge check and restored the preview settings. No native draw replacement,
shared mesh preparation or overall FPS improvement is established by these tests.

`merged-root-full-113`, PID12308, passed all 13 bounded checks with that predicate
fixed. Exactly one standalone root was suppressed. The paired frame consumed
MAIN depth at sequence 138 and VRCAM depth at sequence 315, using independent
collectors; the next frame returned to ordinary separate roots. Total depth
consumptions were four over two frames, not the five from the warmup. MAIN depth
preparation was now available before VRCAM consumption. The later HUD reached
1,094 ms / 98 submissions / 71 source advances, and both-eye preview pixels were
nonblack. The preview was visually inspected after returning to normal rendering;
it is not a direct pixel validation of the one merged frame.

Important remaining evidence gap: the eye-copy CPU event was sequence 287, before
VRCAM depth consumption at 315. Native command lists can be recorded out of GPU
execution order, so neither this ordering nor the existing `eye_copy_recorded`
check proves that the copied image contains that frame's final VRCAM output.
The bounded test proves root selection, list consumption and copy recording only.
Correlate the copy's command list with native submissions/fences and the producing
view before extending this into a continuous mode. Do not relabel CPU recording
sequence as GPU order or treat a later ordinary preview as merged-frame proof.

`merged-root-control-114`, PID13528, passed all nine normal-launch checks, with
the paired-root hook disabled. Readiness required 1,047 ms / 95 successful HUD
submissions / 83 advancing source serials, native menu 0 and live world status.
Both owned games were closed, with no crash reports and preview restored to
2193x16. All four settings files and absent calibration match the deployment
manifest. Final local verification: 27 single-pass CTests, the HUD GPU submission
test, 118 single-pass Python tests and 21 launcher tests passed; `git diff --check`
reported no whitespace errors.

Installed archive: `build/single-pass-20260927/deploy-merged-root-full-113`.
DLL SHA256: `63cc3b67b25f5f11e7fa16c692e8c5d69c8877e02e1e2481a1a0361a7ffc14da`.
PDB SHA256: `276e18e880482c5f6321fa5b2dd3b5f99c11fc43311960067713897e8e5a06ef`.
No permanent paired mode, once-only mesh preparation, native merged GPU draws or
overall FPS improvement is claimed. The development goal remains active. No
commit or push was made.

## Submitted Output Order and Consecutive Paired Roots

The preparation trace now observes depth draw-list scopes, list resets and
actual native `ExecuteCommandLists` submissions. It also retains the exact
view and native node attribution while the node epilogue records its eye copy;
restoring the parent node's TLS no longer loses the diagnostic attribution.
No shader conversion or draw replacement is enabled by this observation.
The bounded record capacity is 8192; full packet ownership remains capped at
524288 packets and capture length at four frames.

The checker distinguishes recording generations and requires one witnessed
submission of each recording. It accepts a depth scope before the copy in the
same list, or earlier separate submissions to the same queue. It does not invent
an ordering edge across queues or between lists in the same batch. This follows
Microsoft's [ExecuteCommandLists contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists):
separate calls are ordered, while lists within one call may overlap execution.
See also [multi-engine synchronization](https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization).

`merged-root-order-115`, PID17892, passed all 13 checks. Its eye copy was recorded
at sequence 456 before the depth scope ended at 522, but their submissions were
correctly ordered: depth list at 597, copy list at 641 on the same queue. Thus
the earlier CPU-order concern was parallel recording, not reversed GPU submission.
The copy was attributed to the exact VRCAM view. This establishes depth-to-copy
ordering, not final-image pixel equivalence or GPU completion.

The existing composition loan wrote `{value, null owner}` unconditionally into
VRCAM frame data. Both views now share MAIN's frame data, so that path must not
replace an already owned composition pair. `vd_lend_composition` preserves any
nonzero existing owner; normal borrowing into unowned RTT frame data is unchanged.
A paired-root return observation checks that the original value and owner remain.
The native root returns before subsequent render tasks and is not a GPU fence.

The bounded merge request now supports 1-3 consecutive frames. A subsequent
standalone VRCAM root is suppressed only after the previous paired root call
returned with ownership intact, and never twice in the same observed frame.
The final normal tail frame proves the request stopped. Invalid budgets and
lost ownership reject further pairing; tests cover these transitions. The
request, capture controls and merge limit are restored by the reader.

`merged-root-burst-116`, PID17344, passed all 13 checks with three merged frames
followed by one ordinary frame. It observed three root suppressions, three
paired graph selections and three returned roots with the same owned composition
pair. There were exactly eight depth consumptions, one per eye in each frame.
All three VRCAM copies were tagged to node `0x1EE760` and their exact view.
The depth/copy submission sequence pairs were 573/625, 1209/1253 and 1829/1873,
each on one queue in separate calls. MAIN preparation preceded VRCAM consumption
in all three merged frames; the tail reverted to VRCAM-first and shared transient
collector reuse. That is a changed scheduling opportunity, not shared mesh work.

After the burst, fresh HUD remained stable for 1,078 ms / 96 submissions / 69
source advances. Both preview eyes were nonblack after ordinary rendering resumed.
The wrapper closed both runs without a crash and restored preview settings.
No continuous production mode, once-only geometry preparation, native merged GPU
draws or overall FPS benefit is established yet.

Run 116 archive: `build/single-pass-20260927/deploy-merged-root-burst-116`.
DLL SHA256: `81f5c1ef824e459c5d11f2e8046c521cda269280f4a2d51af4e145ff88cc6516`.
PDB SHA256: `3dbaddbfd1dfdfea475e1e52dafae34aa8cbcdc0c4057ce54b798e6a2b53615f`.

`merged-root-control-117`, PID28460, passed all nine ordinary checks with no
paired-root hook. HUD readiness was 1,016 ms / 93 submissions / 73 source advances.
Its preview was visually inspected: both scene images, hands and HUD were present.
The owned game was closed. DLL/PDB and all four settings hashes match the run-116
manifest; calibration remains absent and preview dimensions returned to 2193x16.
No game, reporter, capture replay or GPU test is running.

Final verification: 27 CTests, 123 single-pass Python tests and 21 launcher tests
passed. The stricter decoder was also replayed against run 116's owned binary
trace: exact per-frame eye counts, three consecutive skips/returns, and all three
GPU list-order checks passed. `git diff --check` found no whitespace errors.
No commit or push was made; the full single-pass goal remains active.

Next implementation work is a sustained, bounded paired interval with output
completion/readback while pairing is active, followed by moving both mesh-list
preparations before the first eye's draw. The paired root now keeps both native
collectors alive at once, but gather/filter/finalize still execute independently
and on demand for each eye. Do not replace those lists from pointer/packet
equality alone: the earlier ownership and mutable-particle checks still apply.

## Sustained Pairing and Active-Mode Image Motion

The paired coordinator can now run for a bounded 30-1200-frame interval independently
of the expensive preparation trace. Run admission requires the existing verified
warmup, matching native function hashes and an installed paired-root hook. Its
controls default to zero. The interval preserves the same root/output guards and
finishes on the next Present boundary after its final paired root, not immediately
when that root's CPU dispatch returns. This allows the final eye copy and native
depth work to be counted. A 45-second watchdog and stop request also end the run.

The light counters observe actual native depth consumption per view, VRCAM-tagged
eye-copy recording and the submission of those copy lists. Recording generations
are retired by native list reset. A reset before submission, replayed copy list,
unknown copy view or lost root ownership fails the interval. The counters are
not GPU-completion fences; the simulator image request provides the separate
displayed-output check during the active interval. No heavy packet buffers grow
in this mode, as covered by the native fixture.

`paired-interval-118`, PID12504, passed all 15 checks: exactly 1200 paired roots,
1200 suppressed standalone VRCAM roots, 1200 depth consumptions for each eye,
and 1200 recorded/submitted VRCAM copy lists. All failure counters were zero.
Fresh HUD was sustained for 1000 ms / 89 submissions / 62 source advances while
pairing was active. Both-eye screenshots were taken around paired frames 87-97
and 190-200; the run was still active on both sides of each image request.
Both were visually inspected with scene, hands and HUD present. The later normal
HUD and all ordinary packet/cleanup/crash checks passed, and the game was closed.

`paired-motion-119`, PID26096, repeated the full 1200-frame interval with a
simulator-only ten-degree yaw and return. All 15 checks again passed. The three
images were captured while pairing was active at approximately frames 126-137,
304-316 and 378-390, with simulator yaw 0, 0.175 and 0 radians. Both scene regions
changed strongly on the turn (mean absolute RGB differences 44.58 / 44.51) and
returned near the initial image (1.44 / 1.46). The turned image was visually
inspected. These are motion smoke-test metrics, not full-image or temporal-quality
equivalence. The exact initial pose was restored, no keys or focus changes were
needed for the turn, and the wrapper closed the game afterward.

New fixture coverage includes long-run completion and cancellation, rejected
frame budgets/unknown copy sources, exact per-eye/copy counters, both-eye image
motion/return, finite/wrapped poses and refusal to overwrite concurrent pose
changes. A restoration-state bug found by the latter test was fixed: the helper
now updates its own expected pose after returning to the original pose.

Installed archive: `build/single-pass-20260927/deploy-paired-interval-118`.
DLL SHA256: `04d85a153c5823544266e3ecba9c76ecaf908eeb9eb8907fc13ce60179a32849`.
PDB SHA256: `46d0b8486274ae4dc035068fa5213a95bf8b0cc1e5185162190a57c5e44012b9`.
This is sustained paired-root scheduling, not yet once-only mesh preparation
or native view-instanced draws. No overall FPS improvement is established.

### Next Mesh Preparation Entry

Static inspection of the current executable identifies a narrower next step.
`PrepareStage` at `0x1D57210` is 0x6A bytes and reads only context bytes `+0x18`
and `+0x19`: gather (`0x15375C`), filter (`0x1D57100`), optional `0x953474`, then
finalize (`0x379568`). It does not read the context word at `+0x14`, which differed
between views in earlier traces. This distinction matters when preparing a peer
list early rather than inventing a matching pass/context ID.

For the observed depth stage (stage 3, mode bytes 0/0/1, zero filter flags), both
native bucket addresses are at collector storage +`0x34C0`. `0x15375C` traverses
28 linked-list heads in 0xE0 bytes; each node uses next at +8, packet data at +0x10,
and count at +0x20. `0x15381C` sums those counts. Finalize mode 0 sorts the 16-byte
packet array through `0x379B28`. This establishes a candidate eager preparation
entry, not that the peer's bucket data is ready or immutable at the first eye's
call. Those dependencies and native sorted output still need validation before
replacing the later peer preparation. The IDA connector was unavailable; these
facts came from disassembly of the installed executable using its PE unwind ranges.

### Control Crash and Recovery

`paired-interval-control-120`, PID9144, crashed before startup readiness and before
any rendering probe was armed. This ordinary launch explicitly removed
`CVR_STEREO_PAIRED_ROOT`. The PID-matched report is
`Cyberpunk2077-20261001-131745-9144-21092`. Its fault is game RVA `0x1DB1EC`,
`mov eax,[rcx+0x58]`, with noncanonical RCX `0xAE393B6200C959AF`. Stack-scan
candidates include `0x1DB19E`, `0x1DB166`, `0x9FB7FA` and `0x9FB124`; this is
a scan, not an unwind or proof of causation. DLL control globals were not
included in the minidump's captured memory and were not assumed to be readable.

The same game fault RVA and earlier stack-scan pattern were recorded before
this paired-root work in `docs/framegen-dlaa-native-20260930.md` (PID4232) and
`docs/fixed-vrs-20260930.md` (PID18428). That establishes a preexisting signature,
not its root cause or proof that it is harmless/random. No speculative game-code
guard was added. The report was retained, CrashReporter closed, and the wrapper
confirmed the game had exited and restored preview settings.

`paired-interval-control-121`, PID10948, then passed all nine ordinary checks on
the same DLL. HUD readiness was 1,031 ms / 92 submissions / 80 source advances.
The run controls (`RunRequest`, `RunState`, `RunStop`) were also explicitly read
as zero before shutdown. The game was closed and preview restored. The intermittent
startup fault remains an unresolved baseline risk, not a passing test.

Final local tests so far: 27 CTests, 129 single-pass Python tests and 21 launcher
tests. The active-interval image-motion validation is a stronger milestone than
the earlier post-test preview, but the full goal still requires shared mesh
preparation and native view-instanced draw replacement with measured benefit.

## Eager Depth Preparation and Its Producer Dependency

`NativeDepthPreparation.hpp/.cpp` adds an opt-in, frame-local early-sort path.
It owns 16-byte-aligned packet copies, validates bounded 28-head bucket chains,
rejects cycles and unstable snapshots, and checks view/storage/frame identity.
The native gather remains responsible for the engine-owned output array and
allocation. Unit fixtures cover comparison, replacement, changed inputs,
changed resource eligibility, stale frames, missing early work and native
exception propagation. Ordinary launches leave `CyberpunkVR_EagerDepthMode=0`.

One critical correction was made **before deployment**: the full native stage
is not a pure function of its packet bytes. Filter predicate `0x1D57310` reads
renderer resource metadata, including type at table entry +0x50, and for some
types calls the resource's virtual readiness method and checks a field at +0x5C.
Therefore early work only calls native mode-0 sort/finalize on owned raw packets.
The normal VRCAM gather/filter still executes at its original point. Cached
sorting may be used only if those filtered inputs exactly equal the early raw
input; otherwise the normal native finalizer runs. No resource filter is reused
from an earlier moment and no live game packet buffer is sorted speculatively.

Native mode-0 sort `0x379B28` and its partition/insertion helpers compare packed
key fields and move 16-byte packets. Input alignment matters: these helpers use
aligned SIMD loads/stores. Compatibility fingerprints exclude existing detour
prologues and check the remaining native stage/gather/filter/finalizer bodies.
The first build after the source glob regenerated needed a second MSBuild call
to include the new compilation unit; that failed link was not deployed.

`eager-depth-compare-122`, PID22152, reached a valid world, passed the three-frame
paired warmup and completed all 1200 paired roots/depth/copy counts. **The eager
comparison itself failed**: MAIN and VRCAM were each observed 1200 times, but
the peer depth bucket was empty on every MAIN preparation. `prepared=0`,
`missing=1200`, `verified=0`, `replaced=0`, with no snapshot/ABI/output mismatch.
No early sort or native sort replacement was accepted. The game was closed and
settings restored. This is a producer-readiness failure, not a successful CPU
optimization and not a reason to weaken the input validation.

### Actual Packet Production

The preparation trace now records `MaterializeWorker` (`0x36DDC4`) entry/return.
The entry snapshot owns its metadata; the native worker resets/releases/frees
its task before returning, so the exit observer never dereferences that task.
`0x36E054` first evaluates up to 32 tagged bucket inputs via the tester's vtable
+0x30, then processes visible IDs directly through `0x1E9120` or launches
128-ID chunks. `0x1E9120` constructs a gather context using `0x623FD8` and calls
each render proxy's vtable +0xF8 method. The gather context receives the actual
collector storage, not a second camera guessed from a viewport or dimensions.

`paired-materialize-123`, PID19228, passed all 13 checks. It captured 60 complete
materialization workers over four frames. In merged frame 1, MAIN materializers
ran at sequences 111-126, followed by MAIN depth preparation/consumption at
226-237. VRCAM materializers only ran at 401-416, followed by its depth at
523-530. Frames 2 and 3 showed the same ordering. Thus both collectors exist
early, but the second one's geometry is actually produced later.

`paired-cull-phase-124`, PID9384, passed all 13 checks and added exact context
fields. The earlier `DoCulling` calls have work-context +0x30 **bit 1 clear**;
the native function immediately returns without scheduling culling work. Later
calls have that bit set and produce the observed materializers. Upper bits of
that byte are not defined by the native context copier and must not be treated
as a stable mode enum. The context copier `0x1EC7EC` copies only bits 0/1 there
and only a **byte** at +0x34. The effective view namespace is its low byte:
MAIN 0, VRCAM 1, even where an unmasked 32-bit read contains 512/513.

Materializer task +0x48 matched its view's collector storage in all observed
tasks. Task +0x50 is the query/tester interface. This rules out reading the wrong
collector as the explanation for run 122's empty early bucket. The legacy
`node_owner_bit` profiler comment was corrected: this bit is an execution gate
on DoCulling, not proof that a view owns shared simulation work.

The next scheduling change belongs before the first eye's real culling/draw
sequence, after graph resources and both view inputs are ready. It must retain
native work-context ownership, preserve the peer's byte-sized namespace, and
join native job prerequisites; merely calling the early declaration-phase node
or moving the sort cannot provide the missing packets. Query initialization and
mutable particle inputs remain dependencies to preserve, not nodes to skip.

Run-124 archive: `build/single-pass-20260927/deploy-paired-cull-phase-124`.
DLL SHA256: `5017b90d1793fab3c3fe273728f2a031e681fab2760dead78f46431715813f50`.
PDB SHA256: `642def52b7c0735bc760a1bdc9b283859f7eabc4a629e9bfadc7f77de02d8ca0`.
No native merged GPU draws, shared once-only mesh preparation or measured FPS
gain has been established. The goal remains active; no commit or push was made.

`eager-depth-control-125`, PID19084, passed all nine ordinary checks with the
experimental controls zero. HUD was stable for 1,016 ms / 91 submissions / 73
source advances, and the normal stereo preview was visually inspected. The game
was closed, all settings hashes matched the run-124 manifest and preview dimensions
returned to 2193x16. No game, reporter, capture replay or required test process
remains running. Local verification: 28 CTests, 131 single-pass Python tests,
21 launcher tests and clean `git diff --check`.

## Native CPU Dependency Scheduling

Run `paired-graph-plan-126` (PID23044) passed all 13 checks and captured the
combined graph before declaration: 155 active nodes, 928 edges (809 phase 0,
119 phase 1), with all endpoints valid. `NativeGraphSnapshot` copies sparse
node/edge slots through their ordered handle lists and checks stable header,
handle lists and bytes before publication. It does not mutate the game graph.
The two bytes at node +0x1C are **topological order positions**, not depths.
Both were permutations of 0..154 in this capture; the decoder names them
`phaseOrder`. Older run-126 JSON used the inaccurate name `levels`.

The exact current-executable consumer establishes what can be changed:

- `0x289C78` allocates one begin/end native job-group pair per graph node.
- `0x1441E0` consumes only phase-0 edges. It maps their node handles to active
  ordinals and connects the corresponding native job-group prerequisites.
- `0x1E6488` separately queues all original node jobs and copies their original
  definitions and per-view fields. It does not consume the temporary edge list.
- `0x1ECF2C` copies node +0x1D into work context +0x38, independently of CPU
  prerequisites. Dispatch/resource recording uses that byte as its order/slot.
- The declaration/execution bit comes from the native resource-pool phase;
  no node is manually called with a fabricated execution flag.

In run 126, MAIN collector initialization was node 5 and scene culling node 6.
VRCAM equivalents were 88 and 89, with synthetic bridge 148 between them.
42 CPU dependencies from MAIN's render work/terminal node into 88 delayed all
of its geometry production. Global initialization and the earlier independent
MAIN node 35 were also prerequisites and are retained. The source node IDs
are evidence from this graph, not hard-coded identifiers in the planner.

`NativePreparationSchedule` identifies the two collector nodes and culling
requests from native work RVAs and definition flags. It moves only VRCAM
collector initialization, its bridge and scene culling (flags 17). MAIN's
collector, all three MAIN culling requests and four MAIN particle tasks must
finish first. Removed MAIN barriers are propagated to every outgoing frontier
of the moved region, retaining them before all other VRCAM work. The moved
scene culling then precedes those MAIN draw tasks. The planner rejects unknown
shapes, aliased collector/metadata ranges and cycles; it retains every original
edge between unmoved nodes. The original graph, node definitions, phase-1 order,
view contexts, native job ownership and rendering work remain unchanged.

The dependency hook passes a local, owned partial graph only to the synchronous
`0x1441E0` consumer. That consumer copies relationships into native job groups
and retains no graph pointer. Actual dispatch still uses the original cached
graph, so no temporary graph memory is borrowed by worker jobs and no cached
graph restoration races are introduced. The hook and related callers/context
layout are fingerprinted and installed only for the paired-root test process.
The mode defaults to zero. This is a development scheduling experiment, not a
persistent setting or a once-only mesh-preparation implementation.

### First Runtime Checks

`paired-cpu-schedule-check-127`, PID18376, passed all 13 checks. Three combined
graphs planned successfully without applying changes: 809 original CPU edges,
943 planned edges, three moved nodes, eight producer prerequisites, 42 moved
barriers and three frontier nodes. Both views had distinct collector and metadata
ranges; metadata was storage +0xC5C40 for each, matching the native owner layout.
As expected without application, VRCAM materialization still followed MAIN depth.

`paired-cpu-schedule-apply-128`, PID10616, passed all 13 checks with three actual
applications and zero planner rejections. In **all three frames**, every recorded
VRCAM materializer completed before MAIN's first depth preparation. Both native
depth consumptions and the eye depth-to-copy submission-order checks passed.
The normal tail, fresh HUD and both-eye image checks passed. Both owned games
were closed, preview dimensions restored, controls returned to zero, and settings
hashes remained unchanged. No crash was recorded in either run.

Run-127/128 DLL SHA256: `3faceb39e271993f32fa7e1c81e5aa83da24c44131486fa2caafab45a9026b6b`.
PDB SHA256: `caf00c44d8291e71a0716abc89d8ceafd8221edbe810d5bf825013c9ab081a78`.
Archive: `build/single-pass-20260927/deploy-paired-cpu-schedule-127`.
Local tests passed: 30 CTests, 133 Python tests, 21 launcher tests, and the new
planner replayed the actual run-126 graph successfully. These results establish
earlier native packet production, not image equivalence, reduced mesh preparation
count, merged GPU draws or an FPS gain. Sustained-mode validation follows below.

### Sustained Early Preparation and Reuse

The same schedule is now supported during the bounded native paired interval,
not just full-trace frames. The runner requires a successful bounded application
first, checks exactly one accepted application per frame, and restores the mode
after draining the interval. Unknown/failed plans remain a native fallback but
make the test fail; a lower application count is never reported as success.

`paired-cpu-interval-129`, PID15916, passed all 15 checks. Across 1200 frames:

- CPU schedule: 1200 accepted applications, no failures/fallbacks.
- Paired roots, MAIN/VRCAM depth, eye copies and eye-copy submissions: 1200 each.
- Early VRCAM depth: 1200 nonempty inputs, 1200 identical late filtered inputs,
  1200 bit-identical native sort outputs, zero missing/changed/stale/error cases.
- Stable HUD and both-eye images were inspected while the changed schedule was
  active. Ten-degree yaw changed both scene ROIs (mean absolute RGB 44.21/44.14);
  return to the original pose reduced differences to 1.65/1.67. Pose restored.

`paired-cpu-replace-130`, PID22180, passed all 16 checks in one owned launch. It
first repeated the 1200-frame reference comparison (all 1200 matched), then ran
another 1200 frames using the early sorted list at the late VRCAM stage. There
were **1200 actual late-sort replacements**, no changed input, missing input,
output difference, stale frame or snapshot error. Each interval had all 1200
root/depth/copy/submission counts and all 1200 schedule applications. Both
intervals passed active-mode HUD and head-turn checks. For replacement, outward
ROI differences were 44.56/44.47 and return differences 1.45/1.47. Both temporary
poses were restored. Active and returned stereo screenshots were visually read.

This resolves run 122's empty-before-MAIN producer dependency for the tested
depth stage. It does **not** establish a reduction from two eyes' mesh preparation
to one: each eye still runs native materialization, the early VRCAM sort still
exists, and native resource filtering stays at its original late point. It is
also not a same-frame original-versus-modified scene-image equivalence proof.
The graph snapshots/validation are intentionally expensive; screenshot FPS or
the later normal-mode profile must not be presented as a scheduling speedup.

`paired-cpu-control-131`, PID26432, passed all nine ordinary checks on the same
DLL with the paired-root environment removed. HUD readiness was 1063 ms / 96
submissions / 83 source advances. All preparation, graph, schedule and eager
controls were zero; the ordinary stereo preview was visually inspected.

All three launches closed their owned game, restored the 2193x16 preview and
preserved settings hashes. No PID-matched crash was recorded. No game, reporter,
capture replay or required test session remains running. Reusable MSBuild worker
processes are not active build sessions; all build/test command handles exited.

Current archive: `build/single-pass-20260927/deploy-paired-cpu-interval-129`.
DLL SHA256: `f959905f355860107903684ea04e813b894d59ea12ea85232c184a43a17b271a`.
PDB SHA256: `d086553822c0d2f45e9a301345a2b8425b066e7f1970bb1423d095d3f879bdcd`.
Final local verification: 30 CTests, 134 single-pass Python tests and 21 launcher
tests passed. The graph decoder naming fix and new scheduling tests are included.

Next integration must use this early availability for a real shared pass: preserve
correct per-eye cameras/history, acquire current-frame compatible mesh/resources
and issue native paired draws into two-layer outputs without replaying both
original passes. Once-only common CPU preparation remains necessary, not something
these schedule/reuse counters prove. No production merged draw, measured overall
FPS gain or complete single-pass implementation is claimed; the full goal remains
active. No commit or push was requested or made for this work.

## Early Paired Camera Constants

The next native GPU integration cannot use the existing post-submission constant
bank unchanged. Preparation traces show MAIN depth submitted before the later
VRCAM depth recording, even after early CPU materialization. For example, run 128
frame 1 recorded MAIN depth at sequences 276-309 and submitted it at 361; VRCAM
depth was only recorded at 488-497. A common draw at MAIN therefore needs current
peer constants earlier, not an artificial wait for the second eye's submission.
Command-list addresses can be reused within a frame; sequence/reset generations
must be retained when interpreting these traces.

The View Instancing specification leaves per-view shader computation and output
routing explicit. A two-layer target alone does not supply the two camera blocks,
and shared shader side effects need separate consideration. See the primary
[D3D12 View Instancing specification](https://microsoft.github.io/DirectX-Specs/d3d/ViewInstancing.html).
The existing paired shader/root/array code is retained; this change supplies its
missing early camera data rather than replacing it with another shader prototype.

### Incorrect Forecast Rejected

`--paired-cameras` synchronizes a filtered camera trace with the CPU preparation
trace at the same Present boundary. `paired_camera.py` selects the camera upload
immediately preceding each eye's actual first stage-3 depth invocation, matching
view/frame/side. It does not choose the later camera with the smallest byte error.
The trace ABI remains 4616 bytes; its formerly reserved final word now identifies
the predicted peer side. The legacy trace reader/exploratory analyzer understand
that field too. All new controls default to zero and are restored after capture.

`paired-camera-133`, PID12296, reached a valid world and completed three scheduled
roots, but correctly **failed** camera comparison. Reversing the old VRCAM-first
forecast selected the peer's *next* R2 sample. In frame 1, the peer counter was 540:
the forecast chose phase 12, while native VRCAM used phase 11. Its already stored
camera at view +0x70 had that exact current sample. The same one-sample error
recurred in frames 2/3. Pose and rotation matched; projection/inverse/jitter words
did not. These predicted bytes were private diagnostics, not uploaded into the game.

Under the combined graph, both current cameras have already been prepared. The
old +0x70-as-previous-input assumption only applied to the earlier separately
prepared MAIN path. The corrected paired path reads VRCAM's current camera and
its **separate** native history at view +0xEF0, double-checking stable owned copies.
It validates matching projection parameters/dimensions before sharing the remaining
frame metadata. It no longer predicts R2, advances a counter or guesses peer pose.

Static inspection of native camera serializer `0x1E2C94` and its history helpers
establishes the history layout used by `DecodeShaderCameraHistory`:

- valid byte +0; fixed-point previous position +8; quaternion +0x20;
- previous view matrix +0x30 and unjittered projection +0xB0;
- prior basis vectors from view-matrix columns 0, 2, 1, each with W=0;
- temporal weight +0x180, previously unavailable to the old forecast.

The pure decoder retains owned data and rejects absent history, nonfinite/negative
weight, nonfinite matrices or a history projection that is not unjittered. The
existing native-order camera math and native inverse then produce all 848 shader
bytes. Legacy VRCAM-first diagnostics still use their older forecast and explicitly
unavailable word 147; the new prepared path certifies **all 212 words**, including
147, only when the exact native comparison succeeds.

### Stationary and Moving Validation

`paired-camera-ready-134`, PID6128, passed all 13 checks. It captured 218 camera/list
records, and all 848 shader bytes matched the later native VRCAM depth camera in
all three paired frames. Compared numeric input matrix/pose/jitter ranges also
matched. The native local camera is a stack copy; its address is not view +0x70.
Unconsumed/local control fields outside the selected numeric ranges are not
claimed to be a byte-identical 904-byte object.

`paired-camera-turn-135`, PID10984, correctly failed the **motion coverage** gate,
not byte equality: all first three paired frames matched but still had the old
pose. The simulator step first appeared in normal tail frame 4. The pose and test
controls were restored, and the game closed. A stationary match was not accepted
as evidence for temporal correctness while moving.

Camera-only trace bursts can now request seven paired frames plus one normal tail.
Ordinary probes retain their three-pair/four-frame bounds. Record/packet capacity
limits are unchanged. `paired-camera-motion-136`, PID18364, passed all 13 checks
with 442 camera/list records. The seven early VRCAM camera blocks matched all
212 native shader words, with **zero unknown words and zero differences**.
Frames 4-7 had genuinely different current and historical poses during the
ten-degree step; all seven CPU schedules and depth-to-copy ordering checks passed.
The exact simulator pose, controls and preview dimensions were restored and the
game closed. This proves early camera/history bytes for the observed depth pass,
not complete scene-image equivalence or native draw replacement.

`paired-camera-control-137`, PID26172, passed all nine ordinary checks on the same
DLL. Stable HUD: 1031 ms, 93 submissions and 67 source advances. All preparation,
camera trace and schedule controls were zero before shutdown.

### Repeated Startup Fault

The first attempt, `paired-camera-132` (PID2044), crashed during save loading,
before HUD readiness or any camera/pair probe request. Its PID-matched report is
`Cyberpunk2077-20261001-155716-2044-11972`. The exact build archive is
`build/single-pass-20260927/deploy-paired-camera-132` (DLL
`b7b2f201c2495d28f0bad009a27bae69a9df1dd63d1ed6da861fd3d7802a8732`).
The dump again faults at game RVA `0x1DB1EC`, reading [RCX+0x58], with the exact
noncanonical RCX `0xAE393B6200C959AF` seen in run 120. Stack-scan candidates include
`0x1DB19E`, `0x1DB166`, `0x9FB7FA` and `0x9FB124`. Static code reads a cached
reference at entry +8 before that state check; this does not identify the writer
or lifetime failure. The repeated older signature is evidence, not proof of a
root cause or that the crash is harmless/random. No speculative invalid-pointer
guard was installed. CrashReporter was closed and the report retained.

Current archive: `build/single-pass-20260927/deploy-paired-camera-motion-136`.
DLL SHA256: `2e07ed4bae669b3ad673636d9ee36657984ae00e719c7c73de52078219ee6f89`.
PDB SHA256: `996880cb11a2babf0bd9beadb2f738ef6e288a29cc502a47f9d8cefda8aae4a1`.
The remaining production work is still common native mesh/draw preparation,
current-frame geometry/material ownership and actual paired GPU draws into
two-layer outputs. The validated current cameras remove a specific late-input
dependency; they do not prove those remaining contracts or an FPS improvement.

The camera association audit was subsequently strengthened. The initial live
selector matched view/frame/time but could pick a neighboring worker's upload.
Those uploads happened to have equal bytes, but that is weaker provenance. The
current selector also requires the exact depth invocation's **thread and work
context**. Replaying the immutable raw camera/preparation files from runs 134/136
with these stronger constraints still passes all 3/7 frames, including moving
frames 4-7. `preparation-cameras-audit.json` in each run records the source DLL,
PID and SHA256 of the original report, camera bytes and preparation records.
The original live reports were not rewritten to hide the earlier association.

Final verification after this audit: 31 CTests, 143 single-pass Python tests,
21 launcher tests and `git diff --check` passed. Installed DLL/PDB and all saved
settings hashes match the run-136 deployment manifest. No owned game, crash
reporter, capture replay or required test/build command remains running. No
commit/push was made. Full single-pass remains active work, not completed.

## Early GPU Depth Execution in the Native Frame

`EarlyDepthPass` and `NativeEarlyDepth` now record actual paired PSOs while MAIN's
native depth draw is being recorded, before VRCAM's later draw/submit. This differs
from the older frozen-geometry comparison: vertex/index/instance bindings are used
at their real native GPU positions, not copied and replayed after both eyes finish.
Outputs remain private two-layer depth/stencil arrays. **No native draw is skipped
and nothing is copied back into game targets yet.** The native passes remain the
reference while this current-frame contract is validated.

The current supported material contract is vertex-only CBVs b1 (848 camera bytes)
and b5 (112 geometry bytes), no pixel shader/SRV/UAV, fixed raster state and
single-sample reverse-Z depth write. The two b1 values are the actual MAIN upload
and the prepared VRCAM camera/history validated in runs 134/136. Native b1 binding
is tracked per recording thread and exact native handle; an unrelated rebind
invalidates the pending camera. b5 is read twice from the owned mapped-upload
registry at the actual draw, then copied into an immutable `StereoBindingBank`.
Its late VRCAM bytes must match before accepting the corresponding reference.

The pass restores native graphics/compute roots, tables, heaps, PSO, render target,
viewport/scissor, topology, stencil/blend, view mask and VRS state. The references
use the original PSO and original bindings at each eye's own draw position. Command
recordings retain private arrays/descriptors/banks through `CommandResources`.
MAIN submission supplies a GPU fence; a different reference queue waits on that
fence, not on a CPU timer. Readback is accepted only after the reference submission
fence completes. Reset before required submission rejects the probe.

The new same-queue and cross-queue GPU tests pass with the D3D12 debug layer and
bit-identical depth/stencil, including one two-instance MAIN draw versus two
one-instance native peer draws. These tests do not exercise the entire closed
engine scheduler. The live probe is bounded to one frame and 1-128 selected MAIN
draws. Family assets/binding observers must be explicitly enabled for that child
process; ordinary launches remain disabled.

### Live Bring-Up and Batch Identity

Run `early-depth-gpu-138`, PID1044, stopped before arming the GPU probe because the
reader incorrectly expected family-loader state 1 (loading) instead of state 2
(loaded). The check was corrected to match the actual loader, with all preflight
values recorded. No crash or native rendering replacement occurred.

`early-depth-gpu-139`, PID23292, actually recorded eight paired draws during MAIN,
but only seven full peer batches matched. It correctly rejected the result before
declaring GPU equivalence. `early-depth-identity-140`, PID26628, reproduced this
and exported exact source/native draw identities. MAIN sequence 117 contained four
instances (180 indices, input mask 143). VRCAM had matching geometry in batches
of three and one rather than one batch of four. Different batch sizes must not
be confused with different mesh identity.

`InstancePartition` adds bounded full-byte matching of one native peer batch to
an unconsumed contiguous span of a source instance stream. Reused or ambiguous
spans are rejected; each source instance needs coverage exactly once. This is
coverage bookkeeping, **not permission to ignore SV_InstanceID semantics** or
to infer pixel equivalence from transform bytes. The GPU comparison remains the
authority for the observed selected output. Reverse-direction merging across
multiple MAIN items and genuine per-eye-exclusive instances are not solved by
this first partition matcher.

`early-depth-partitions-141`, PID14264, passed all 16 launch checks. Eight paired
GPU draws, covering 74 instances, matched eight native VRCAM reference draws in
this particular run. Both 1485x1485 eyes had zero depth/stencil differences and
zero invalid depth values; reference coverage was 3288/3277 written pixels. Every
selected instance was covered. Unlike run 140, this run's selected first eight
draws happened to have one-to-one batches, so live split-batch coverage is not
claimed from it. The synthetic GPU test covers that split case independently.

### Wider Test Remains Rejected

`early-depth-wide-142`, PID16488, deliberately expanded the selection to 128
paired draws instead of accepting the small passing subset as the final scope.
It recorded all 128 during MAIN; 121 source batches were fully covered by 122
native reference draws. Coverage was 1096 of 1121 source instances. There were
no camera/material byte mismatches, bank failures or queue failures, but the
remaining seven source batches were incomplete, so the test correctly failed
and did not report an image comparison or a native optimization.

Incomplete source sequence / covered-total instance pairs:
`456:0/3`, `465:0/2`, `474:0/4`, `537:0/1`, `553:0/5`, `781:3/4`, `1183:0/9`.
These may include batch regrouping and per-eye visibility differences; absence
of a whole-batch match is not itself proof of culling. The required next step is
a current-frame **per-instance union with eye masks**, driven by both native CPU
lists, rather than rendering every MAIN instance into VRCAM or shrinking the
selection to make the test green. Native draw removal/CPU reuse must wait for
that visibility/material/resource contract and its GPU verification.

`early-depth-control-143`, PID20052, passed all nine ordinary checks with no family
package, no binding observers, no early-depth request/state and other controls zero.
HUD readiness was 1047 ms / 95 submissions / 80 source advances. All owned games
were closed and preview settings restored. No crash occurred in runs 138-143.

Current archive: `build/single-pass-20260927/deploy-early-depth-partitions-141`.
DLL SHA256: `4bec24b4a54f678034f747f2dae4be3d46c01816183388b8f08a2fe0962037fd`.
PDB SHA256: `420198c504ae8b71900c6c57175a25c0a2b88b579fd99b80114b793108231887`.
Local verification: 34 CTests and 145 single-pass Python tests passed. The family
registry/descriptor observers are very expensive; launch screenshots or CPU sums
are not an FPS benchmark. Full CPU mesh preparation and native recording still
run for both eyes, and the diagnostic adds common/reference GPU work. No overall
FPS gain, permanent single-pass mode or completed goal is claimed.

## Per-Instance Matching Across Native Batches

`InstanceCoverage` replaces whole-batch matching in the early GPU comparator.
It matches complete 48/64-byte instance records across any number of compatible
MAIN batches, preserves multiplicity and emits contiguous selected ranges in the
native peer batch. Planning does not mutate coverage; successful reference
recording commits it transactionally. Geometry, PSO/root, viewport/scissor,
stencil/blend/topology, b5 material bytes and the prepared peer camera must match
before instances enter one candidate set. Unsupported/unmatched peer instances
remain outside the selected reference; original game rendering is unchanged.

Splitting a native reference batch can change its draw-local instance ordinal.
`HasIndependentInstanceOrdinal` therefore parses the DXBC input signature and
rejects a declared `D3D_NAME_INSTANCE_ID`, malformed/missing signatures and unknown
layouts. It is combined with the existing rebase/extended-base-instance gate.
`Family::instanceOrdinalIndependent` records that result. Offline inspection of
the four b1/b5-only depth VS families found no InstanceID input (some use VertexID,
which is retained because index/base-vertex geometry is unchanged). The selected
contract is not applied to arbitrary shaders by assuming their ordinal is unused.

New fixtures cover merged/split batches, repeated identical records, selected
ranges with holes, consumed coverage, transactional failure and shader-signature
rejection. The earlier native-instance-partition fixtures remain useful but no
longer define the live comparator's cross-batch matching capability.

### Broad Coverage and the Last Mismatch

`instance-coverage-144`, PID21660, recorded the full 128 requested paired MAIN
draws. 127 source batches were completely covered; the remaining four-instance
batch was covered 3/4. It correctly remained a failed full-coverage test, without
claiming GPU equality or shrinking the request. No source camera/material or
bank/queue failure was reported.

`instance-streams-145`, PID19608, reproduced 127/128 and owns full instance streams
plus material blocks at their native draws. Its 136-byte instance rows identify
native draw sequence, side, blob offset/bytes/stride and all 112 material bytes.
The immutable exports are `preparation-early-instancerows.bin` (39,304 bytes) and
`preparation-early-instanceblob.bin` (110,784 bytes), accompanied by the native
draw records. Publication occurs before the terminal state, and a state recheck
under the draw mutex prevents late appenders from changing a published failed
capture. Exception publication uses that mutex too.

The unmatched instance in MAIN sequence 754 is index 0 of four, at fixed-point
world position approximately (-3235.187, -6737.785, 108.299). Its **complete 48-byte
transform and all b5 bytes occur identically in VRCAM sequence 2171, instance 9**.
However, the geometry contract is different:

| Field | MAIN 754 | VRCAM 2171 |
| --- | ---: | ---: |
| Index count | 180 | 57 |
| Index buffer address | 12564571276 | 12564571636 |
| Index buffer bytes | 474 | 114 |
| Instance count | 4 | 10 |
| Input slot mask | 143 | 143 |

The PSO/root/material match, while VB0-3 addresses/ranges differ too. This is
evidence for different native geometry/LOD selection for the same instance,
not a missing or slightly changed transform. It must not be "fixed" by forcing
one eye's LOD onto the other. The common plan needs a MAIN-only geometry entry
and a VRCAM-only geometry entry with the corresponding eye masks.

The remaining implementation dependency is early visibility/geometry identity,
not another relaxation of late equality. Both CPU collector lists are available
before MAIN, but the current early GPU probe still starts from MAIN's native draw
stream and discovers peer coverage later. It does not yet use those lists to emit
the complete per-eye union before MAIN submission. Native packet draw metadata
already includes the geometry choice, so that is the integration point to pursue.

Useful current-executable evidence for that integration: in ordinary kind-0
flush `0x1F191C`, the frame returning at `0x1F19DA` has R14 pointing to the 64-byte
native draw definition and RBX to the packet-consumer state. State +0x48 is that
draw definition, +0x50 object data, +0x58 material state. The surrounding native
consumer is `0x1F1208` (0x714 bytes). `0x1F1E44` reads the mesh pointer at draw +8,
submesh at +0x2A, and dispatches geometry setup `0x1F2B68`; that setup can use
override/deformation paths, which must not be silently treated as immutable
static geometry. No new mutation/call into those native routines was added.

`instance-coverage-control-146`, PID14332, passed all nine ordinary checks with
early/family observers disabled. HUD was stable for 1093 ms / 98 submissions / 81
source advances. All three owned games were closed and their preview restored;
no crash occurred. Experimental native draw replacements remain zero.

Current archive: `build/single-pass-20260927/deploy-instance-streams-145`.
DLL SHA256: `70e14e3498b54cd417df1aa669d18b0c455bedbb5a81bb37892a6f8df7713131`.
PDB SHA256: `7e81296acb04bd65204cac210dfa0229ce3b55d177124985c449d048e9222a5d`.
The full single-pass objective remains unfinished: per-eye union/masks must be
available early, followed by native draw replacement and shared CPU preparation,
then output/performance validation. No commit/push or FPS gain is claimed here.

## Early Per-Instance Eye Masks

`NativeEyeMask::EyeMembership` now owns prospective geometry membership from the
sorted MAIN packet span and a double-checked snapshot of the already materialized
VRCAM raw depth buckets. Both are decoded at MAIN stage-3 entry, before its draw
recording. `ResolveLive` reuses the native pool/upload resolver, and each input
snapshot is read twice. Exact instance bytes and multiplicities are retained;
the 64-byte draw definition keeps its meaningful geometry/LOD fields while union
padding is normalized. No native packet or LOD is overwritten.

This is a geometry membership key, not a complete material/resource contract.
It deliberately does not authorize skipping game draws. The actual late VRCAM
draw must still match the PSO/root, all used native geometry bindings, raster
state, 112-byte material block and full prepared camera. A geometry match in the
CPU list alone cannot prove those mutable inputs.

At a MAIN draw, bounded stack unwinding reaches the exact `0x1F19DA` regular-mesh
flush return. Full body fingerprints for `0x1F191C` and the unpatched portion of
`0x1F1208`, renderer/flush/state identities, ordinary-instance flags and native
instance count guard the 64-byte draw-definition read. This currently supports
the regular 48-byte instance path, not arbitrary/skinned consumers.

The private paired depth pass emits contiguous instance runs with mask 3 (both
layers) or mask 2 (MAIN layer only). The native MAIN reference still draws the
complete original batch. Peer coverage starts entirely false; predicted MAIN-only
instances are never premarked as matched. An actual matching peer for such an
instance fails the test. At group end, every actual coverage bit must equal its
early prediction. The separate masks/coverage exports and verifier account for
every selected instance, including the difference between common and MAIN-only.
The PSOs enable view masking as required by the
[DirectX View Instancing specification](https://microsoft.github.io/DirectX-Specs/d3d/ViewInstancing.html).

Run 147 (PID9988) failed before GPU recording because a whole-list completeness
requirement rejected 44 unobserved upload ranges. These ranges cannot be read as
zero or treated as missing geometry. Membership now records their geometry as
unknown: a known positive instance match is usable, but absence cannot be inferred
for that geometry. Run 148 (PID5816) reached 109 draws and exposed an overly broad
unknown-geometry rejection, which also rejected known positive matches. The
corrected rule retains the unknown-absence guard without rejecting known records.
Neither failed run was accepted as full coverage; neither crashed.

Run 149 (PID27328) passed all 16 launch checks and the full requested 128-source-
draw comparison. There were 1122 common instances and one MAIN-only instance,
recorded as 129 paired instance runs. Both 1485x1485 layers were bit-identical in
depth and stencil to their selected native references; written pixels were
165659/165920, invalid depth and maximum error were zero. No camera/material-bank,
queue or native-origin failure occurred. The owned game was closed and preview
settings restored. The post-test stereo screenshot was also visually inspected.

The GPU fixture now covers mixed masks, split common references and a MAIN-only
case with a completely empty peer reference, including cross-queue ordering.
Readback can close an empty reference on a caller-provided peer list after MAIN
submission. The live controller records that list even when no selected peer
instance is drawn, and rejects switching to a different peer command list.

This advances early per-eye selection, not full native single-pass rendering.
The selected-MAIN test does not emit VRCAM-only LOD geometry early, does not cover
all 44 unobserved upload ranges, and still replaces zero native draws. Completing
the union, resource ownership/early binding, removing duplicate native recording
and shared CPU mesh preparation remain required before any FPS claim.

Run 150 (PID6664), on the final DLL, repeated all 16 checks: 128 source draws,
1135 common instances, one MAIN-only instance and 129 paired runs. Exact depth
and stencil comparison passed again at 1485x1485, with 223456/223328 written pixels
and no invalid depth or maximum error. Run 151 (PID2980) passed all nine ordinary
checks without family/binding observers. Its HUD was stable for 1062 ms, with 96
successful submissions and 80 source advances. Post-test stereo images from both
runs were visually inspected. All owned games were closed; no game, replay or
CrashReporter remained. No crashes occurred in runs 147-151.

Final local checks: 37 CTests, 148 single-pass Python tests and 21 launcher tests.
The report decoder only permits an empty peer reference when early membership
contains no common instances, still requires nonempty MAIN output and rejects
any peer reference pixels in that case. The default comparison retains its
nonempty-both-eyes requirement. Saved raw comparisons from runs 149/150 were
revalidated with this final decoder. `git diff --check` passed.

Current archive: `build/single-pass-20260927/deploy-early-eye-masks-150`.
DLL SHA256: `35dfb3758b1681af8f2df65d40d0afd49f592cc536c5ae4839b3f1922da2731d`.
PDB SHA256: `68d6323c437388c70036096a86f60c54f7e09f55e3d6d8a84d0667d80941d59a`.
The installed game/plugin UserSettings, vrport.ini and launcher settings retain
their pre-run hashes, calibration remains absent and the temporary simulator
preview was restored. Framegen stayed off; rendering settings were not reduced.
This remains an opt-in correctness path with substantial observation overhead,
zero experimental native draw replacements and no measured FPS improvement.

## Early VRCAM-Only Instances

The membership plan retains original per-eye multiplicities separately from
the remaining MAIN selection counters. `Extras` selects the positive VRCAM-minus-
MAIN difference for a geometry binding exactly once, preserving the peer record
order and complete instance bytes. It does not infer absence from unknown MAIN
upload ranges. Using the original counts is important: subtracting already
selected MAIN instances would incorrectly classify common instances as extras.

`EarlyDepthPass::Main` can now upload those additional instances into an owned
buffer and render them with mask 1, using the current native geometry binding
and the already owned paired constant bank. The temporary slot-7 binding is
restored before drawing the native MAIN reference. Command-resource ownership
keeps the upload alive until GPU completion. Normal native game draws remain
unchanged. The source item retains both streams and checks the appended records
against actual late VRCAM draws with the same strict geometry/material/camera
contract; a peer-only prediction without a reference fails coverage.

`EarlyDepthExtraCoverage[128][2]` publishes confirmed/requested extra instances.
The extended mask counters distinguish recorded extras, extra draw runs, known
peer-only candidates and unrecorded candidates. Instance corpus rows with side 2
contain the appended CPU-owned stream and identify its MAIN geometry donor;
they are not native MAIN or VRCAM draw records. A combined MAIN/extra source can
hold 32 KiB, while an individual observed native batch remains capped at 16 KiB.

Run 152 (PID7664) passed all 16 launch checks: 128 selected MAIN draws, 1117 common
instances, one MAIN-only instance, and 27 VRCAM-only instances in three additional
runs (1/4/22 instances). All 27 known peer-only candidates were referenced; none
remained unrecorded in this sample. The private 1485x1485 layers matched the native
references bit-for-bit, including depth and stencil; written pixels were
268548/265491. No camera/material-bank/origin/queue failure or invalid depth was
reported. The stereo screenshot was visually inspected. Run 153 (PID24848) passed
all nine ordinary-launch checks with observers off, HUD stable for 1063 ms / 95
submissions / 76 source advances.

### Inactive Index Overrides

An ordinary sequential CPU capture initially suggested 123 VRCAM-only instances
on 21 geometry definitions. Further native-code inspection identified inactive
draw+0x1C bytes in that key. `0x1F2B68` only uses the passed offset when the index
override handle at draw+0x10 is valid; otherwise `0x1F2D7E` replaces it with the
mesh descriptor's +0x78 value. Unused draw+0x1C bytes included stack/address residue.
Normalizing only this inactive offset reduced that same offline sample to 12
known VRCAM-only / 3 MAIN-only instances, from 123/114. This is key correction,
not a rendering-detail reduction or permission to merge active overrides.

The live geometry key now performs that normalization. Fixtures require different
inactive offsets to match and different active offsets to remain distinct. The
existing broader `MeshCandidateKey` is not changed by this narrower geometry-key
correction. Run 154 (PID22024) passed all 16 checks on the corrected build: 128
source draws, 1135 common instances, one MAIN-only instance and eight VRCAM-only
instances in three runs (1/3/4). All eight known extras were verified. Depth and
stencil were identical in both 1485x1485 layers, written pixels 250448/246875,
zero invalid depth and zero maximum error. Different fresh runs have different
native instance counts; their difference is not a performance measurement.

These samples still leave 44 unobserved upload ranges per eye and do not cover
every MAIN draw or shader. A geometry with no eligible MAIN donor can remain
unrecorded; the explicit candidate counter must not be read as complete-scene
support. Native draw replacements remain zero. The next integration must cover
the whole compatible depth target and preserve all other native depth draws,
then replace duplicate native recording using independently validated early
inputs. Per-draw whole-depth copies would defeat that optimization. A target-
level handoff must copy complete depth/stencil plane subresources with matching
sample counts and correct states, as required by Microsoft's
[CopyTextureRegion contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copytextureregion).

Additional native evidence for later early binding: ordinary mesh setup
`0x1F2B68` reads a 0x90-byte submesh descriptor at `[mesh+0xC0]+submesh*0x90`.
It has both a unified-buffer path and a separate per-stream handle path
(`0x1F6438`), plus override/deformation branches. The 32 bytes at descriptor+0x30
are copied into the geometry-constant state. In run 150's 289 captured b1/b5-only
draws, all used one PSO; only b5 words 16/17/18/20/21/22 varied, the first 64 bytes
were identity and the final 16 zero. This observation is not a global immutable-
material guarantee. Fresh resource ownership, queue readiness and full binding
equivalence are still required before novel geometry can be issued early.

Run 155 (PID14608) passed all nine ordinary checks on the final build, with family
and binding observers off. HUD readiness was 1047 ms / 96 submissions / 83 source
advances. Its stereo screenshot was visually inspected. Both ordinary controls
and both early GPU runs closed their owned games; no crashes occurred in runs
152-155. Final process inspection found no game, replay or CrashReporter.

Local verification: 38 CTests, 149 single-pass Python tests, 21 launcher tests and
`git diff --check` passed. Fixtures include peer-only GPU output, native slot-7
restoration, cross-queue lifetime, exact duplicate multiplicity, original-versus-
consumed counts, unknown MAIN ranges, one-time extra selection, combined 32-KiB
source matching and explicit incomplete-extra report rejection. Raw GPU results
from runs 152/154 were revalidated with the final report validator.

Current archive: `build/single-pass-20260927/deploy-early-peer-only-154`.
DLL SHA256: `6bfc3e4f02a1bc44024a4edde481a73d6ad31b45a5c071fb12c420922b3f234b`.
PDB SHA256: `6882d98d51b83e5ec7e40fd03cb734349a83b327f33d6b75c4d89a0c95cd0297`.
Installed binaries and all preserved settings were checked against the deployment
manifest after run 155. Calibration remains absent; temporary preview dimensions
were restored. No commit or push was made. Native once-only mesh preparation,
native draw replacement, permanent single-pass mode and measured FPS benefit
remain unfinished requirements of the active goal.

## Whole Depth Targets and Native Round-Trip

`EarlyDepthWholeTarget=1` is a bounded, opt-in extension of the early comparison.
It routes every indexed draw in the selected stage-3 native group, not only the
128 converted candidates. `EarlyDepthPass` now owns `StereoTargetArray` outputs,
single-eye DSVs for unconverted draws, the live native depth destinations, and
post-copy readback slots. MAIN initialization imports its actual native starting
depth/stencil into the paired output. VRCAM uses that early seed; the independent
native reference retains VRCAM's own actual initialization, so an incompatible
seed fails the full-image comparison rather than being erased.

Converted MAIN geometry renders into both array layers using the existing eye
masks and peer-only instance streams. Unconverted MAIN draws render unchanged
into its single array layer. During VRCAM recording, only unmatched instance
ranges are added to the paired VRCAM layer; its complete native draw still supplies
the independent reference. All native-reference draws on the final implementation
render directly into the original 2D DSV. At group completion, the actual native
2D output is imported into the reference array and copied back once, then both
native planes are captured separately for comparison. Thus the test checks the
original 2D view semantics, the complete paired depth group, and actual native
copy-back pixels without replacing the paired result with its reference.

The source hook returns handled for these routed calls. `DrawRecorded` explicitly
records their actual output command list for native preparation ordering; it is
not a measure of saved GPU draws. `NativeCounts[8]` reports enablement, both full
draw counts, MAIN fallback draws, remaining VRCAM runs, both exports and imports.
`NativeDifferences[4]` compares native post-copy depth/stencil against the native
reference for each eye. The validator requires all seen draws, both exports,
both imports and zero image/copy-back differences. Complete native reference
draws are still executed, so **net native draw replacements and FPS benefit are
not established by this mode**.

Unknown/non-indexed/indirect work and depth clears/copies within this group abort
the test and preserve the native reference prefix before normal recording
continues. Internal probe commands do not trigger these guards. Queue-order
callbacks continue after a failed probe so already recorded private work still
gets its cross-queue dependency; peer submission no longer depends on a final
readback having been recorded.

### Native Shader Admission

`NativeDepthPolicy` admits only known depth-only single-sample pipeline shapes
without other shader stages or stream output. Verified family resource metadata
or shader reflection must show no UAV bindings. Direct resource-descriptor-heap
indexing is also rejected: absent explicit UAV bindings alone does not prove
absence of writes when a shader can create a UAV handle from the heap. The SFI0
feature gate applies to both known-family and reflected shader paths. Malformed
containers, missing required DXIL feature information and unknown resource types
fail closed. Existing converted-family eligibility is unchanged.

Legacy `D3DReflect` cannot inspect some native DXIL. Optional
`CVR_STEREO_REFLECTION_DXC` selects an explicit absolute dxcompiler.dll and uses
`IDxcUtils::CreateReflection` for those shaders. It is set only by the child-launch
`--reflection-dxc` option and is explicitly removed from normal child environments.
No compiler runtime is added to the release package. Tests cover DXBC/DXIL reads,
explicit UAV writes and SM6.6 direct-heap UAV writes.

### Live Runs

Run 156 (PID23260) stopped at native draw 1324 after routing 162 MAIN draws. Its
vertex shader was not in the converted family package and legacy reflection
rejected the DXIL (policy status 4). The native prefix was preserved, the test
failed rather than dropping the remaining geometry, and the game was closed.
There was no crash.

Run 157 (PID14212), with the explicit Windows SDK DXC, completed 225 MAIN and 226
VRCAM draws and all GPU comparisons, but the separate depth-to-eye-copy order
check failed because routed draws bypassed the old source hook's `DrawRecorded`
call. The order check was not weakened; the replacement path now records its
actual list. Run 158 (PID4800) then passed all 16 checks: both depth/stencil layers
and native copy-back matched exactly, with 414347/414903 written pixels. The
native depth list submission 735 preceded the eye-copy submission 783. That
version still rendered reference draws into private single-layer array views.

Run 160 (PID17888) repeated all 16 checks after strengthening references to render
into the original game 2D DSV. All 225 MAIN / 226 VRCAM draws were covered, including
97 MAIN fallbacks and 97 remaining VRCAM runs. The converted subset was 128 draws,
1119 common instances, one MAIN-only and 27 peer-only instances. Both 1485x1485
layers matched depth/stencil exactly, with 414297/414779 written pixels and zero
invalid depth/max error. Native post-copy differences were zero for all four
eye/plane checks, and queue ordering passed.

The synthetic whole-target fixture also injects an incorrect peer-only transform.
It requires the paired comparison to detect that error while native output stays
identical to the true native reference. This guards against a vacuous comparison
created by copying a reference over the candidate before reading it.

Run 159 (PID21276) was a normal-launch control that stopped when Windows refused
the foreground-window transition, before save readiness. It was not a game crash
or a successful control; the owned process was closed and preview restored. A
transient native UserSettings hash during this aborted startup returned to the
original `21139c862524ea021d584c9f8e6b41b55b5363d3702730af8edbee385972dc00`
after the successful run 160. The settings were not manually overwritten.

The next implementation still needs a no-reference runtime path with a correct
fallback when late validation disagrees. Simply copying the precomputed VRCAM
depth first and then falling back to normal draws is insufficient: stale shared
depth cannot be removed that way. A conditional resolve/native fallback, with
validated initial depth/stencil and stable descriptor/instance ownership, is a
candidate design. No such predicated optimized path is claimed here. Duplicate
CPU scene/mesh preparation also remains, as do wider pass coverage and a measured
performance improvement.

Run 161 (PID2652) passed all nine ordinary-launch checks on the final DLL, without
the family/binding observers or explicit DXC environment. HUD readiness was
1046 ms / 94 submissions / 76 source advances. The final run-160 and run-161 stereo
images were visually inspected. The earlier run-158 screenshot lacked visible
HUD text, whereas run 160 (direct native 2D reference) and ordinary run 161 showed
it. That cause is not isolated; the no-reference path must preserve in-pass depth
consumers and hook side effects as well as final depth/stencil pixels. Do not use
run 158's array-reference comparison alone as proof of those semantics.

Final verification: 44 CTests, 150 single-pass Python tests, 22 launcher tests,
and `git diff --check` passed. All test/build sessions completed. Native-reference
and native-copy GPU data from runs 158/160 also passed the final report validator
when reread from the saved artifacts. All owned games were closed; no game,
capture replay or CrashReporter remained. Runs 156-161 produced no game crash.

Current archive: `build/single-pass-20260927/deploy-whole-depth-native-ref-160`.
DLL SHA256: `42952a7b6659c497d40fd8561f5310e779234ba9d2f9ff17ea2bde9417366768`.
PDB SHA256: `bf0032911e42809f44f8702078111816702f9af30b90f3c12aa57367cf0a1bb6`.
After run 161, game UserSettings again matched the original `21139c...` hash;
plugin UserSettings (`9750f5...`), vrport.ini (`a2a457...`) and launcher settings
(`14bcc0...`) remained unchanged. Preview dimensions were restored, framegen
remained disabled and no resolution/detail settings were lowered. No commit or
push was made. The goal remains active: net draw elimination, shared CPU mesh
preparation, permanent mode, broader coverage and FPS validation are still due.

## Conditional No-Reference Depth Route

`EarlyDepthWholeTarget=2` executes the selected paired depth work without complete
native reference draws. MAIN converted batches and unchanged single-eye fallbacks
write the paired MAIN layer, copied into native MAIN at group end. The second
array retains only native initial depth/stencil, not a secretly rendered reference.
This path does not allocate the full comparison-image readback buffer. It rejects
framegen-enabled admission and leaves all controls off by default.

`DepthCommitGate` combines two independent conditions. A CPU-owned 64-bit upload
starts at zero and is set only after actual peer camera/material/geometry and
instance coverage validate. On the GPU, a compute pass compares both native
initial depth/stencil layers bitwise and clears admission on any mismatch. The
DEFAULT decision buffer is then transitioned to PREDICATION. CPU admission is
frozen before the peer list is submitted; an early submit leaves it zero rather
than allowing a late change under GPU execution. The queued dependency on MAIN
is GPU-side, with no CPU wait for the second camera.

D3D12 predication applies to copies and clears as well as draws, so this switch
does not require a fullscreen depth/stencil resolve shader. Predication samples
the buffer at each SetPredication call, hence the immutable submitted decision.
These semantics are specified in Microsoft's
[Predication documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/predication).

VRCAM conditionally copies the prepared layer, including both planes, into its
native target. For a matched native batch, optimized remainder runs execute under
one predicate and the original whole batch under the opposite predicate. Fully
unmatched batches execute once without predication. A rejected prediction never
leaves stale shared depth in native VRCAM; fallback commands keep their native
position relative to other work. Existing native predication is rejected. Depth-
copy source reads as well as writes in the group abort before that operation.

`EarlyDepthOptimized[8]` reports enablement, GPU decision, MAIN command count,
unconditional peer commands, optimized peer ranges, fallback peer batches,
reference count and CPU admission. The verifier checks complete accounting and
requires zero references. Net GPU draw-command reduction derives from the GPU
decision and recorded branches. Comparison images and native-difference fields
are explicitly null in this mode: an absent reference is not reported as a
zero-error image comparison.

### Independent GPU Fixtures

The gate fixture tests matching initialization, a depth-only difference at one
edge pixel, a stencil-only difference at one edge pixel, CPU rejection, frozen
late admission and complementary predicated copies. The depth fixture compares
optimized native output against separately rendered original 2D targets for both
eyes. It covers success, explicit rejection of an intentionally incorrect peer
transform, and GPU rejection from different initial depth or stencil. All outputs
match the independent references. A hardware pipeline-statistics query around
the peer geometry reports one input primitive for the admitted remainder and
three for each native fallback case. Equal final pixels alone do not establish
that predicated draws were actually suppressed.

### Live Admission and Fallback

Run 162 (PID25088) forced CPU rejection. All 16 checks passed with GPU decision 0,
zero references and all 226 native VRCAM batches active (97 unconditional plus
129 fallback batches). MAIN emitted 229 commands, including splits/extras, so
this rejection sample executed four more commands than its 451-source baseline.
It is not counted as an optimization gain.

Run 163 (PID15600) passed all 16 checks with CPU admission 1 and GPU decision 1.
The 225 MAIN / 226 VRCAM source calls produced 228 MAIN commands and 97
unconditional VRCAM commands. All 129 recorded VRCAM fallback batches were
predicated off; no additional partial remainder runs were needed. Reference
count was zero: **451 to 325 executing draw commands, net -126**, within this
single bounded depth group. Native eye outputs/HUD remained available and its
post-test stereo image was inspected. The screenshot is after the bounded
interval, not a measurement of steady optimized FPS.

Run 164 (PID4088) checked the whole native-reference path on the same DLL. All 16
checks passed with 226 draws per eye, bit-identical 1485x1485 depth/stencil and
zero native copy-back differences. Written pixels were 414447/415105. It is a
separate fresh reference run, not a same-frame native image reference for run
163. The no-reference GPU fixture supplies that direct same-frame algorithm
check; broader live image coverage remains necessary.

This early path now suppresses repeated GPU geometry without always executing
full references. It still prepares both CPU view lists and records conditional
fallback commands. It still observes a bounded frame with expensive family and
descriptor instrumentation, creates temporary banks/targets and has no proven
steady-state FPS improvement. Persistent resource reuse, lean runtime tracking,
GPU-time measurements, shared CPU preparation and broader pass/scene coverage
remain requirements of the full goal.

Run 165 (PID22908) passed all nine ordinary checks with experimental controls and
family/binding observers off. HUD readiness was 1063 ms / 96 submissions / 75
source advances, and its stereo image was visually inspected. All four owned
games (162-165) were closed; none crashed. Final inspection found no game, replay
or CrashReporter. Mode-2 admission/fallback and mode-1 reference reports were
revalidated from the saved artifacts after the runs.

Final verification: 49 CTests (including the hardware primitive-count checks),
151 single-pass Python tests, 22 launcher tests and `git diff --check` passed.
All required build/test sessions are terminal. The installed binary/settings
hashes match `build/single-pass-20260927/deploy-optimized-depth-162/manifest.json`;
calibration remains absent and the temporary preview size was restored.

Current DLL SHA256: `aca10abae874ea0f5be59016cf9955da924fa58fcca2f64e22cc295e80d48c50`.
PDB SHA256: `ba4648986c4972d776ae145d019a764bf461273118758a79c6f415c870d9b914`.
No commit or push was made. Net GPU command elimination is now demonstrated for
the bounded selected depth group; the broader single-pass objective remains
active and incomplete, particularly continuous operation, CPU preparation reuse
and measured frame-time/FPS benefit.

## GPU Timing and Native Baseline (2026-10-01)

Added `EarlyDepthWholeTarget=3`: a timestamp-only baseline for the same node
`0x23A938`, stage 3, paired root and early CPU schedule. It records every native
indexed draw but returns false from the interception so all original command
handling still executes. No private depth arrays, clears, copies, paired draws,
or extra cross-queue waits are inserted. Separate per-eye readback buffers and
both completion fences protect timestamps even when the native queues differ.
`EarlyDepthGpuTimes[6]` publishes MAIN begin/end/frequency followed by VRCAM
begin/end/frequency. Mode 2 starts before initialization and ends after native
copy-back/gate readback, so its private work is included. Query resolve itself
follows the end stamp in both modes. Frequency comes from the actual submitting
queue; no cross-queue timestamp subtraction or CPU timing is used. This follows
[Microsoft's timing contract](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing).

`depth_benchmark.py` calls the preparation capture in-process, alternating A/B
order after a discarded-but-retained warmup pair. Each sample requires a newly
stable HUD and exact preparation/copy-order checks. It keeps all samples, raw
timestamps, process/DLL/target identity, source draw counts, negative savings and
cleanup evidence. It does not claim total-frame time or FPS from this sum of two
depth ranges. The launcher first verifies full native depth/stencil references
in that same game, then runs the benchmark and closes the owned process.

Run 166 (PID10796, DLL `74886057e852d89c39f31e5121afd8ed26955dc7c746a51893a12587d3b10b38`)
passed all 17 checks and six measured pairs. Native median: **0.073264 ms**;
no-reference mode-2 median: **0.402080 ms**. Median paired saving was negative,
**-0.325504 ms**. Every measured pair had 225 MAIN / 226 VRCAM source calls.
This disproves a GPU-time benefit for this bounded implementation despite its
lower executing draw count. Neither continuous-mode nor overall FPS benefit is
established. The stereo/HUD post-test image was inspected; no crash occurred and
the game was closed (termination after the normal close timeout).

Following that result, the initialization path no longer clears arrays that are
fully overwritten before reading. Mode 1 no longer imports unused initial
reference layers, which are captured from actual native 2D output at completion.
`StereoTargetArray::ImportEyes` imports both layers under one source/destination
transition pair, instead of restoring depth-write state between identical-source
copies. Mode 2 also no longer restores native bindings twice per paired draw.
GPU gate checks and original fallback commands are unchanged. A new fixture
preserves matching nonzero initial depth/stencil at an edge pixel; existing
different-depth/stencil fixtures still reject admission. Array-import fixtures
cover color, motion-vector, depth/stencil formats, padded rows, repeated use and
invalid masks under both WARP and hardware validation.

Run 167 (PID23940, DLL `4f7f2b1c9339bcec79c1c0a668c07c8feb0e6bed9c6d094ee35edcfd37692abe`)
passed all 17 checks. Full reference depth/stencil and native round-trip errors
were zero (414509/414753 written pixels). Across six timing pairs, native median
was 0.072512 ms and optimized median 0.373040 ms; median paired saving remained
negative at -0.300752 ms. The cleanup is useful, but is not a native GPU speedup.

The next change keeps the private MAIN depth array bound between mode-2 draws,
instead of rebinding the unused native 2D DSV after every intercepted draw.
Every following draw still binds its required array view, including original-PSO
fallbacks. FinishNative and AbortNative explicitly restore the original native
DSV after exporting the accumulated MAIN layer. Draws after completion are
rejected. A hardware abort fixture draws additional native geometry immediately
after abort, without rebinding its target, and compares both complete output
images against independent native references. This verifies restoration, not
just final depth equality before the next native command.

Run 168 (PID24152, DLL `6e88362190030ee7242fc093bf1c7e4f67abf6254dae97db2268bb2e332851b2`)
passed all 17 checks and six timing pairs. Its reference check had zero depth,
stencil and native round-trip differences (414450/414788 written pixels).
Native median was **0.073632 ms**, mode-2 median **0.316720 ms**; median paired
saving was **-0.241776 ms**. Each measured pair still had 225/226 source draws
and mode 2 removed 125 executing GPU draw commands. The measured bounded path
has improved from run 166, but remains slower than native. Its temporary arrays,
per-draw banks, restoration/predication and cross-pass copies cannot be presented
as a completed performance optimization.

The post-test run-168 image has an additional large upper-right HUD copy beside
the smaller panel visible near the center; runs 166/167 showed only the large
panel. A subsequent ordinary run 169 shows only the smaller panel. Hand poses
also differ between 166/167 and 168/169. These are post-interval images, not
same-frame optimized/reference images; the causal link has not been isolated.
Do not claim composition-level image equivalence from passing depth or nonblack
pixel tests. Keep this HUD/composition discrepancy open for continuous-mode work.

Run 169 (PID10796, a new process despite PID reuse from 166) passed all nine
ordinary checks with family/binding observers and experimental controls off.
Fresh HUD readiness: 1031 ms, 93 submitted frames and 70 source advances.
All four runs were closed after their complete test plans; no crash occurred.
No game, replay or CrashReporter remained. Preview restoration was reported by
every owned run. Installed DLL/PDB and all four settings hashes match
`build/single-pass-20260927/deploy-depth-bound-168/manifest.json`; calibration
remains absent. PDB: `1cb32e5daaa29a9877c5042b6c762ac120f9671a19ed37bb7af0bfb74ed4b6c6`.

Final tests: 52 CTests, the hardware array-copy fixture, 156 single-pass Python
tests, 22 launcher tests and whitespace validation passed. No commit or push.
All required build/test/launch sessions are terminal. The goal remains active:
extend array lifetime and compatible pass coverage, remove per-draw allocation
and heavy observation from runtime, implement shared CPU preparation, resolve
composition behavior and demonstrate steady-state frame-time/FPS improvement.
The 0.074 ms native cost also bounds how much this particular depth group alone
can save in this scene; do not keep treating it as the whole geometry workload.

## Observer Cost and Heap Lifetime (2026-10-01)

The next step targeted the large CPU cost of the runtime metadata observers,
which must be reduced before a persistent shared pass is usable. Added opt-in
`ObservationProfileEnable` and 8 pairs of count/QPC-tick counters. They report
inclusive wall intervals, including contention; phases may nest or overlap on
different threads. They are not CPU utilization or frame-time estimates.
`--observer-profile` integrates an eight-second sample into the owned launcher.

Run 170 (PID22536, DLL `7110fc4024ce3f1d96b0efb110da0861929ba8b2832ba188be17af5f1b5b1362`)
passed all 17 checks. It falsified the initial suspicion that upload stack
unwinding dominated: unwind was 62.2482 ms across 5891 calls, whereas CBV
metadata recorded 31817.0448 ms / 563495 calls and descriptor copies
49806.0039 ms / 1103455 calls over 8.018 seconds. Those large cumulative wall
values include multiple threads waiting for the same metadata mutex.

Replaced per-descriptor version tree nodes with 4 KiB address pages containing
small contiguous interval vectors. Both partial-page and whole-page queries
remain byte-exact, without assuming descriptor alignment or device stride.
Value interning now indexes a 64-bit hash and ID, stores the 160-byte value once,
and compares all bytes to resolve collisions. The update lock uses a Windows
critical section with bounded spin count 4000, falling back to `std::mutex` if
initialization fails. Microsoft's
[critical-section guidance](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initializecriticalsectionex)
describes bounded spinning for short, highly contended regions; the live tests,
not that guidance alone, establish the result for this workload.

Metadata now follows actual descriptor-heap lifetime, including CPU-only heaps.
An IUnknown attached to each heap queues its cookie when the heap dies. The
callback never takes the store lock or calls other D3D objects; retirement is
drained by subsequent metadata operations. This avoids lock-order inversion
with D3D12 private-data destruction. Slots and versions are reclaimed, copied
descriptors in other live heaps keep their references, and a delayed old-cookie
retirement cannot erase a replacement heap at the same address.

Run 171 (PID9180, paged store) and run 172 (PID17888, added heap retirement)
passed their reference checks and initial timing samples, but stopped on sample
04-optimized with no usable material records. Run 172's new failure evidence
identified the exact cause: descriptor failure 4, store overflow bit 8, and
**1048577 version intervals**. There were still 25 live heaps. No crash occurred;
the capture restored its controls and each owned game was closed. The incomplete
benchmark series are retained and must not be reported as passed.

The former one-million aggregate metadata limit was below the actual live
descriptor capacity. Run 173 measured **1177010 slots across 25 live heaps**.
D3D12 allows multiple heaps and differentiates individual shader-visible heap
limits from CPU staging heaps; see
[Shader Visible Descriptor Heaps](https://learn.microsoft.com/en-us/windows/win32/direct3d12/shader-visible-descriptor-heaps).
The bounded aggregate metadata limits are now 4194304 slots/intervals and
1048576 unique values. Removing the duplicate full-value interning keys offsets
memory cost; overflow still rejects tracking rather than evicting live evidence.
The reader exports live heap capacity and rejects descriptor failure even when
depth images happen to compare equal.

Tests include the existing copy/version/submission contracts, 12000 byte-oracle
update/query cases across page boundaries and near UINT64_MAX, a live population
of 1049000 slots, exact hash-collision handling, 1500 distinct heap retirements,
late-cookie reuse, and real D3D heap destruction by eight concurrent threads.

Run 173 (PID15700, DLL `0362b0474ecee2c462fc79e3c16577a24f6ef31f807b24c955185051561945d3`)
passed all 18 checks, including the profile and eight A/B pairs. The reference
comparison had zero depth/stencil and native round-trip differences; written
pixels were 414256/414841. All 18 timing captures, including warmup, were later
revalidated with the stricter descriptor-overflow checks. Final optimized-sample
metadata held 1069867 slots, 1071328 intervals and 204160 unique values, below the
measured live capacity and without overflow. The former failing threshold was
crossed successfully. No quality, resolution, framegen or async-compute setting
was changed.

In comparable eight-second observer profiles, average inclusive CBV-metadata
interval decreased from 56.46 us to 15.39 us; descriptor-copy interval decreased
from 45.14 us to 16.47 us. These are observer costs, not native-game FPS gains.
GPU depth timing is effectively unchanged: native median 0.072672 ms versus
optimized 0.315376 ms, median paired saving -0.243552 ms. Shared CPU preparation,
temporary banks/arrays, whole-scene pass coverage and continuous rendering still
need work. Faster metadata tracking does not make the current GPU path faster
than native.

Run 174 (PID4156) passed all nine ordinary checks with binding/family observers
and experimental controls off. HUD readiness was 1016 ms / 92 submissions /
70 source advances. Both run-173 and run-174 post-test stereo images were
inspected; neither shows run-168's extra upper-right HUD copy. That discrepancy
has not been causally isolated and is not declared fixed. All owned games were
closed, no crash occurred, and final process inspection found no game, replay or
CrashReporter. Preview dimensions returned to 2193x16.

Verification: 52 CTests, 159 single-pass Python tests, 22 launcher tests and
whitespace validation. No commit or push. Installed DLL/PDB and settings belong
to `build/single-pass-20260927/deploy-descriptor-budget-173/manifest.json`.
PDB SHA256: `36631c60d4703b13c8545753834af79e7a400af300ad393fd96dd4e68a2a2c17`.
The full single-pass goal remains active and incomplete.

## Continuous Depth and Resource Reuse

Added `EarlyDepthRunRequest`, `RunState`, `RunStop`, per-frame GPU rows and
aggregate run/arena counters. This path owns a bounded preparation interval and
reuses the existing verified early CPU schedule. It separates binding tracking
from bulk diagnostic capture: native draw, descriptor and preparation record
counts do not advance during the interval. Native IA resource discovery/copying
is limited to the actual depth group; other native rendering still executes.
Instance-upload observations are refreshed each frame so previous unwritten
ranges cannot be mistaken for current input. Their CPU allocation may be retained
when size matches, but validity ranges are cleared.

`EarlyDepthPass::Restart` is mode-2-only and checks both GPU completion fences
before changing any prior-frame storage. Fence values advance per generation;
a previously completed value cannot admit a newer pending generation. The GPU
admission buffer is transitioned from its previous PREDICATION state back to
COPY_DEST before uploading the next zero-initialized decision. Pool exhaustion
has a native-render fallback, not a CPU wait. The resource lifetime approach is
consistent with Microsoft's
[fence-based resource-management guidance](https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management),
but this implementation uses a bounded pool and rejects reuse instead of blocking
the CPU for a free slot.

The reuse fixture runs four generations with changing camera constants, a CPU
rejection, a GPU rejection from different initial depth and renewed admission.
It blocks a later peer queue on a test fence and requires Read/Restart to remain
S_FALSE despite the previous generation's completed fence values. Every native
output is compared with an independently rendered reference. Both side-specific
timestamps and the primitive-count fallback checks remain active.

Run 175 (PID4936, DLL `5c69aa4bce49a8d30c5ee2ad3c525ec890f125ec7591628851089698b204527f`)
reached 49 recorded / 48 GPU-completed frames, all 48 admitted, with one depth
set and 48 reuses. It stopped at the unchanged active-HUD freshness check before
any active-mode screenshot. It was not a crash or a passed interval. Per-draw
constant banks still created 6272 separate banks; that allocation path was then
removed from continuous rendering.

`StereoBindingBank` now supports an append-only CBV arena for one immutable
paired root, with bounded descriptor count and upload bytes. Root layout is
validated once; subsequent appends must match that complete plan. Every draw
gets distinct descriptor/data offsets, and CPU write-combined stores are fenced
before returning. Previously referenced entries are never overwritten while
recording. Only the owning depth pass's fence-checked restart resets the arena.
The paired root uses explicit descriptor/data-volatile flags; see Microsoft's
[root-signature 1.1 contract](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-version-1-1).
No generic SRV snapshot or shared-table ownership is inferred by the CBV-only API.

Arena tests cover invalid layouts/SRVs, missing values, descriptor/byte capacity,
transactional failure and offset reset. The GPU reuse fixture now appends two
different MAIN camera/depth entries in the same list and changes constants again
in the next generation. Exact native output comparisons catch overwriting an
earlier draw's constants as well as stale data after restart.

Run 176 (PID27872, DLL `319c9c3f579f005f4a3282c8332f67d1ccb13a5459588738944199c6cbd2b737`)
passed its full **240-frame continuous depth interval**: all 240 GPU decisions
admitted, one target set / 239 reuses, one constant arena / 239 resets, 30720
constant-draw entries, no busy-pool skips, zero references, and unchanged bulk
trace counts. There were 108240 native source calls versus 78240 executing GPU
draw commands across those depth groups. This is not CPU command elimination or
FPS gain. Active HUD readiness and two active-mode stereo images passed; the
second image was visually inspected and showed both eyes and one HUD panel.

That launch's subsequent cancellation test did not pass. Sixty depth frames
completed correctly, but preparation had already begun another paired root and
then a native tail. Reused frame-data storage was checked against the stale
previous composition, raising preparation failure 3. The game did not crash and
was closed after cleanup. Do not describe run 176's entire launch as passed.

The cancellation fix adds a paired-root generation token to the outer root
call. Completion validation now runs only when that call actually admitted a
new paired root; an ordinary later root at the same address is not an old root's
completion. Actual changed composition within a paired call still fails. Depth
stop is propagated before the preparation frame boundary, and a pair already
admitted before stop is finished rather than abandoned. Tests cover normal root
address reuse during stop, genuine composition corruption and stop between RTT
suppression and MAIN preparation.

Run 177 (PID12544) stopped in the normal interval after 14 admitted frames when
the fifteenth frame hit reason 20: the generic instance matcher chose identical
bytes from a MAIN-only source. It was not a crash, and cleanup restored normal
controls. Matching now accepts the actual rendered-view masks and excludes
MAIN-only sources from peer reuse. A later eligible duplicate can still match;
otherwise the peer instance remains an ordinary native remainder. Commit rejects
a forged match to an unrendered layer, and MAIN-only coverage stays false rather
than being marked as reused. Tests cover both duplicate selection and a new peer
instance with no eligible source. This changes source eligibility, not the
camera/material/geometry equality checks or the GPU admission gate.

Run 178 (PID16956) reached 222 admitted frames, then stopped at reason 22 on the
223rd MAIN group: optional peer-only expansion could not prove its input. That
strict diagnostic behavior was unsuitable for a continuous renderer. Live mode
now leaves unavailable MAIN predictions on their original MAIN draw path, and
leaves unavailable peer-only additions for actual native VRCAM remainder draws.
`EarlyDepthRunDeferred[2]` reports those two cases. No unavailable input is promoted
to a shared instance. Reference/one-shot probes retain their strict failures.
A frame with no eligible prediction retains all native geometry and explicitly
denies GPU admission. The interval verifier accounts for that fallback without
counting it as a shared draw.

The deferred-extra GPU fixture omits the speculative peer-only instance and
requires two native remainder primitives, with exact complete depth/stencil
matching the independent original render. The membership fixture separately
tests known positive shared coverage alongside an unknown MAIN range: positive
sharing remains usable, but extra peer-only instances cannot be inferred.

### Completed Interval and Shutdown

Run 179 (PID23252, DLL `87f5ff047c87db8863e5eb673781b2a8d7efa0a4b1d7a2d20ac25f47722230ab`)
passed all 19 launch checks. The 240-frame interval completed and admitted every
frame, with one depth set / 239 reuses and one CBV arena / 239 resets. The 30720
constant-draw entries no longer require that many GPU banks. There were 108240
source calls versus 78240 executing GPU draw commands, zero references and no
busy-pool skips. Bulk trace counts remained unchanged. Both deferred counters
were zero in this particular run; their fallback behavior is covered by the
targeted CPU/GPU fixtures, not claimed as exercised by this live interval.

Cancellation requested after 60 completed frames drained the already admitted
61st pair. Depth, paired-root completion, both view-depth counts, eye copies and
submissions all ended at 61, with no failure and no partial extra root. A fresh
HUD was verified after both intervals, and all controls returned to zero. The
active-mode stereo image was inspected. The reference warmup had zero full
1485x1485 depth/stencil and native copy-back differences (414465/414751 written
pixels). Saved normal/cancelled reports were revalidated after the run.

Median summed GPU depth time across the continuous interval was 0.3168 ms. This
does not beat the earlier measured native cost around 0.073 ms, and the current
CPU prediction/matching path is still expensive. There is no verified overall
FPS improvement. The actual advance is continuous no-reference rendering of the
selected depth group with GPU-completion-safe resource reuse, isolated diagnostic
recording, and tested shutdown. Per-eye CPU preparation is still duplicated;
whole-scene compatible pass coverage and longer-lived arrays through downstream
passes remain unimplemented. The earlier HUD/composition discrepancy is not
declared generally solved by this selected scene's successful images.

Run 180 (PID25080) passed all nine ordinary checks with experimental controls,
family assets and binding observers off. HUD readiness was 1016 ms / 92 submitted
frames / 76 source advances; its stereo image was inspected. All games from
runs 175-180 were closed after success/failure, and none crashed. No game, replay
or CrashReporter remained. Preview dimensions returned to 2193x16, all four
settings hashes remained unchanged and calibration remained absent.

Final verification: 54 CTests, 163 single-pass Python tests, 22 launcher tests and
whitespace validation. All required tool sessions are terminal. Installed DLL
and PDB match `build/single-pass-20260927/deploy-depth-fallback-179/manifest.json`.
PDB SHA256: `f0dc8ef6f434c5e2c70151d2eea5b2c38b356287a804a0d48f49921da76e970d`.
No commit or push. The full single-pass objective remains active and incomplete.

## Continuous CPU Preparation Profile (2026-10-02)

Added five count/QPC-tick pairs in `EarlyDepthRunCpu` and their frequency. These
are inclusive CPU wall intervals. MAIN planning, MAIN emission and peer work
are nested within `drawTotal`; membership preparation is separate. They are
not GPU times or a direct measurement of critical-path frame time.

Run 181 (PID16928, DLL `34311e8fa0881e06a719790a7a317b6de6a25459049eb001841aedd1cd0b3bd4`)
passed the 240-frame/cancellation suite. Across 240 normal frames, membership
preparation took 1096.9534 ms total (4.5706 ms/frame), MAIN planning 1317.822 ms,
MAIN emission 684.9469 ms, peer matching/emission 362.7091 ms, and all draw
handling 4685.0546 ms (19.5211 ms/frame). Nested stages must not be added again.

Run 182 (PID28628, DLL `7f15b5563e5ccc5f2ba4b3f62970c664c82a6ba0e28f73fb310682e11d43f8a5`)
tested shorter instance-resource critical regions and spin-assisted native/scene
tracking locks. It passed correctness checks, but membership remained
1098.6512 ms and draw handling 4758.5878 ms over 240 frames. There was no useful
measured improvement, so that experiment, its extra epoch bookkeeping and its
new mutex header were removed. The previously validated descriptor-store mutex
from run 173 remains unchanged.

The retained change is a geometry-only mode of the existing packet decoder.
It skips shader/state/program reads and skips unused object metadata for ordinary
48-byte instances. It uses the same complete instance-stream decoder as the full
path, including skin metadata, preuploaded ranges, exact bounds and unknown data.
This data drives only prospective eye membership. Actual PSO, camera, material
and IA matching are unchanged. The full diagnostic decoder remains the default
for reference captures. Normal-packet fixture reads fall from five-seven to two;
skin cases retain their required metadata reads.

Live mode still performs two independent geometry reads and compares their owned
data. Its first frame additionally runs the full decoder for both eyes and
compares every relevant geometry field and every readable instance byte, without
assuming matching blob offsets (full blobs also contain command programs).
`EarlyDepthRunGeometryChecks` records checked eyes and mismatches. Reports reject
missing checks, one checked eye or any mismatch. Fixtures cover CPU/upload,
single/multiple/skin streams, excluded geometry, unreadable matrices and corrupted
projection data/ranges.

Run 183 (PID6204, DLL `59165322b4283c579deb9db56f16598f3e9a21794f251fcd29578db53626bb66`)
passed all 19 checks, all 240 normal frames and cancellation at 61 frames. Both
intervals reported `[2,0]` projection checks. Membership preparation fell to
714.5200 ms total, **2.9772 ms/frame**, about 1.59 ms/frame below run 181. Draw
handling remained 4773.9645 ms total, so the optimization is specific to input
preparation and is not evidence of an overall FPS gain. The full goal still
requires shared native CPU preparation, broader GPU pass coverage and reduced
cross-pass copies; this change does not supply those missing pieces.

Run 184 (PID11224) passed all nine ordinary checks with binding/family observers
off. HUD readiness was 1016 ms / 93 submissions / 76 source advances. The active
run-183 image and ordinary run-184 image were inspected. Both show both eyes and
the expected HUD panel; this is still not scene-wide image equivalence proof.
Run-183 normal/cancelled reports were revalidated with the projection checks.

All runs 181-184 were closed and none crashed. No game, replay or CrashReporter
remained. Final tests: 54 CTests, 164 single-pass Python tests, 22 launcher tests
and whitespace validation. Preview dimensions returned to 2193x16; settings
hashes match the deployment archive and calibration remains absent. Installed
DLL/PDB match `build/single-pass-20260927/deploy-geometry-inputs-183/manifest.json`.
PDB SHA256: `f75e84b50f86e62d501b7bb3ea9932c6654b8ff644d33254981a5882adf7f93b`.
All required sessions are terminal. No commit or push; the complete single-pass
objective remains active, including actual native CPU preparation reuse and
verified end-to-end performance improvement.

## Native Proxy Preparation Boundaries (2026-10-02)

The native proxy dispatcher at `0x1E9120` is now observed during bounded paired
preparation traces. Its complete 0x108-byte body is checked against FNV64
`DED60A539B29A3F2` before installing the environment-gated hook. It constructs a
gather context through `0x623FD8`, then calls `(visibleID - 16)->vtable+0xF8`
for every visible ID. The original routine still executes once per invocation;
this addition does not replace or suppress native work.

New kinds 20/21 own entry/return evidence. Schema 2 (`Record.reserved=2`) stores
two 16-byte packets per proxy: `{ID, gatherMethod}` and `{vtable, updateMethod}`.
The update method is vtable+0xF0. `flags0` is the proxy count, while `count`
continues to count 16-byte storage units. Headers/IDs are rechecked, allocation
is bounded, collector storage must match the view, and completion uses the owned
entry metadata without dereferencing the expired native range. The decoder
preserves multiplicity and rejects incomplete/mismatched scopes. Old schema-0
captures remain readable but cannot satisfy the typed live acceptance check.

Run 186 (PID20904) rejected its three-frame camera-motion requirement: all three
native/predicted camera comparisons matched, but no moving frame was captured.
It was not a crash or a camera-constant mismatch. The error now distinguishes
missing motion from unequal constants. Run 187 (PID9068) used seven merged frames
and passed all 13 checks, with movement in frames 4-7. Run 188 (PID10468) passed
the nine-check ordinary control with no paired-root environment.

Run 189 (PID6468) repeated the seven-frame test with typed proxy inputs and passed
all 13 checks. There were no incomplete proxy scopes. First paired-frame inputs:

| Gather RVA | Vtable RVA | Update RVA | MAIN calls | VRCAM calls | Common occurrences |
| --- | --- | --- | ---: | ---: | ---: |
| 0x13E254 | 0x2ADA910 | 0xAE5670 | 247 | 255 | 243 |
| 0x1E77D0 | 0x2AECEF0 | 0xAE5670 | 10 | 10 | 10 |
| 0x1E9228 | 0x2AC22A0 | 0x1E8778 | 711 | 727 | 710 |
| 0x1E9228 | 0x2BE90F8 | 0xD169C0 | 2 | 2 | 2 |
| Other five gather methods | Distinct native types | Per type | 73 | 74 | 73 |

Total: 1043 MAIN / 1068 VRCAM invocations, 1038 common ID/type/method occurrences.
After turning, common occurrences were 1022, 914, 915 and 925 in frames 4-7.
These are dispatcher invocations, not counts of successfully emitted packets,
saved calls, or elapsed CPU time. In particular, an early-out inside a method
still counts as an invocation. Capture copying adds overhead.

The exact installed EXE was disassembled offline while preserving its hash and
entry-range hashes in `proxy-types-189/native-mesh-paths.json`. The main mesh path
is now localized more precisely:

- `0x1E9228` reads view feature flags, consults view+0x1D98, compares per-object
  cache stamps, locks object+0x8C, invokes virtual+0xF0 when needed, calculates
  distance from the gather camera, and calls `0x1E9B88` with a per-eye context.
- `0x1E8778` starts with the view-distance update `0x1E8A54`. Its ordinary path
  calls `0x1E8B48`, which updates LoD/history bits in the per-view cache. Its
  instanced path selects/copies instance transforms and may upload them.
- `0xD169C0` shares that base updater but adds another scene-side append. Thus
  equality of the 0x1E9228 gather entry alone does not classify all side effects.
- `0x1E9B88` selects per-view LoD/transition data, updates streaming demand and
  object reporting fields, then invokes `0x1EA24C`.
- `0x1EA24C` iterates material/submesh records and invokes `0x1EA780` for packet
  construction. Those lower preparation boundaries are the next candidates for
  splitting shared templates from per-eye selection/emission. Their resource
  lifetime and mutation contracts are not established merely by this listing.

No entire proxy callback can yet be skipped. Sharing a cache pointer or LoD
result would change per-eye work and violate the unchanged-detail requirement.
This narrows the actual once-only CPU integration to mesh/material preparation,
with native per-eye culling, history, selection and side effects retained.
The overall goal is still incomplete: common native preparation, wider GPU pass
coverage, retained cross-pass arrays and positive end-to-end performance remain.

The typed run matched all seven early/native camera blocks, observed motion in
frames 4-7, restored the pose, passed post-test HUD and both-eye image checks, and
closed its owned game. No run 186-189 produced a crash report. Archive:
`build/single-pass-20260927/deploy-proxy-types-189`.
DLL SHA256: `4a7c20b53e15c2ff076fb6a883e794acfd3d80ae825d81d998878d55e137f25d`.
PDB SHA256: `a6e4686e3852ae7aa7e6fae720c3a6b5c5ede5a8ced63245350c444c8345319a`.
Local validation: 54 CTests, 165 single-pass Python tests and 22 launcher tests.
No commit or push was made; there is no new FPS-gain claim from this stage.

## Retained CPU Dependency Plans (2026-10-02)

The material investigation continued through `0x1EA780`, `0x1EB7BC` and
`0x1EB374`. The latter two already use native material/shader caches, but also
maintain frame-local registrations. In particular, `0x1EB7BC` checks a native
pool epoch at +0xB74300 before reusing variant index/count data. `0x1EB374`
resolves a cached composite shader handle and calls `0x1EB718` to ensure the
current native shader tuple is registered. These are not pure functions whose
entire results can simply be borrowed from a previous eye/frame. Their entry
disassemblies and the EXE hash are saved in
`schedule-variants-193/native-cache-paths.json`; no material callback was skipped.

The implemented optimization removes repeated construction of our CPU scheduling
plan. Previously, every paired frame repeated reachability/cycle validation,
edge-set construction and native shadow allocation for the same input graph.
`PlanCache` now retains up to four immutable prepared variants. A hit requires
the same image base and exact equality of every captured node/edge field,
including raw bytes, handles, order, work pointers, vtables and definitions.
No graph field is ignored. New/changed input uses the original validated builder.

Fresh native graph capture and live collector/metadata independence checks still
run on every invocation. The original cached game graph is never changed. The
hash-verified `0x1441E0` only reads the shadow's edge list and node ordinals and
creates dependencies in the fresh native job groups. A shared immutable owner
keeps each shadow alive through that synchronous call even when another graph
evicts its cache entry. Prepared owners cannot be copied, preventing stale
internal vector pointers. Tests cover input changes, image changes, bad snapshots,
forced rebuilds, LRU eviction and retention of an evicted native-call owner.

This dependency/lifetime discipline also follows the general guidance in Epic's
[mesh drawing pipeline documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/mesh-drawing-pipeline-in-unreal-engine):
cached commands must be invalidated when their input dependencies change, and
per-view inputs cannot simply be treated as immutable mesh data. This is design
guidance, not evidence that REDengine has Unreal's resource ownership model.

`PreparationScheduleCache` defaults to 1 but has no effect outside the already
opt-in scheduling experiment. `ScheduleStats[12/13]` count reused/rebuilt plans.
`ScheduleCpu[6]` contains count/tick pairs for graph capture, plan preparation and
native dependency creation, with `ScheduleCpuFrequency`. These are CPU wall
intervals, not total frame time or GPU time. The benchmark restores the cache
control and all prior renderer test controls before returning.

Run 190 (PID16468) stopped before any experiment: the world status was ready but
the current viewport sample was absent/undersized. No crash was detected. The
launcher now waits within its existing deadline for a usable restored viewport,
resets its settling interval while it is unavailable, and requires it throughout
HUD sampling. The actual reason for that single invalid window sample is not
established. Run 191 (PID4616) subsequently passed all 14 checks, including the
new one-launch off/on/on/off benchmark (240 frames per interval).

Initial single-entry cache A/B results, including all rebuilds:

| Cache | Hits / rebuilds | Capture ms/frame | Plan ms/frame | Native dependencies ms/frame |
| --- | ---: | ---: | ---: | ---: |
| Off | 0 / 240 | 1.3703 | 10.5756 | 0.0944 |
| On | 240 / 0 | 1.3861 | 0.0062 | 0.1013 |
| On | 214 / 26 | 1.3620 | 1.1470 | 0.1043 |
| Off | 0 / 240 | 1.3460 | 10.4063 | 0.0990 |

Across those two samples per mode, plan time fell from 10.4910 to 0.5766 ms per
paired frame. This is removal of prototype scheduling overhead, NOT a gain over
the ordinary game or evidence that native mesh preparation is now shared.

Run 192 (PID21892) passed all 19 depth checks, all 240 continuous frames and the
61-frame cancelled interval with the single-entry cache. All frames were admitted,
with one target/constant arena per interval and no references, busy slots or
fallbacks. The normal interval had 181 hits / 59 rebuilds and 3.0240 ms/frame of
plan work; the cancelled interval had 43 hits / 18 rebuilds and 3.6143 ms/frame.

The final implementation retains four exact variants. Run 193 (PID13072) passed
the same 19 checks and 240/61-frame intervals. Its normal interval had 192 hits /
48 rebuilds and 2.4813 ms/frame of plan work; the cancelled interval had 59 hits /
2 rebuilds and 0.4141 ms/frame. These are separate launches with different graph
churn, not a controlled one-versus-four-entry speed comparison. Every normal
frame was admitted, 30720 constant draws used one arena with 239 resets, bulk
trace counts stayed unchanged, and both-eye geometry projections were `[2,0]`.
Cached paired and active-depth images were inspected with both eyes and HUD.

Run 194 (PID16008) passed all nine ordinary checks with no paired-root or binding
observers requested. HUD readiness: 1015 ms, 92 submissions, 68 source advances.
The owned game was closed and preview dimensions restored. A later audit found a
shutdown crash from run 192, described below. Final native/Python tests: 54 CTests, 166 single-pass
tests, 23 launcher tests. The full goal still requires shared native mesh/scene
preparation, wider pass coverage, retained arrays and verified net FPS improvement.

Current archive: `build/single-pass-20260927/deploy-schedule-variants-193`.
DLL SHA256: `391c32f0cad3c2a35fd7971bd867e5c4f0bbde87fb2ef855401ef86f09967d14`.
PDB SHA256: `588e1d9fac1d09f93b8764e7c311617a7d4e90649af8ce994a51fb1dde9b0f18`.

Run 195 (PID11192) repeated the four-interval A/B benchmark on that four-entry
build and passed all 14 checks. The uncached plan samples were 2574.4870 and
2547.1308 ms per 240 frames. Cached samples were 1.5852 and 1.5346 ms per 240
frames, with 240 hits and zero rebuilds in each. Mean plan cost was therefore
10.6700 ms uncached / 0.0065 ms cached for that stable-graph workload. Graph
capture remained about 1.4 ms/frame. These timings still do not establish an
overall FPS improvement or once-only native mesh preparation.

The miss path was then improved without changing any graph semantics:
reachability and cycle checking now use the ordered edge set's outgoing range
(`lower_bound({node,0})`) instead of scanning every edge for every visited node.
The fixture compares reachability against the full-scan reference for every pair
of fixture nodes, retaining the cycle, unknown-shape and lifetime tests. The
captured run-193 graph still produces the same 943-edge plan with 3 early nodes,
8 producer prerequisites, 42 barriers and 3 frontier nodes. A 100-build offline
test on that exact saved graph took 957.816 ms before / 71.0624 ms after
(9.5782 / 0.7106 ms per build). This is an offline builder measurement, not a
live frame measurement; it specifically reduces the cost of required rebuilds.

Run 196 (PID24088) passed all 19 checks with the range-based builder: 240/61
completed and admitted frames, no busy slots/fallbacks/references, `[2,0]` geometry
checks, and one target with 239/60 reuses. Normal plan work was 21.3359 ms across
240 frames (0.0889 ms/frame, 219 hits / 21 rebuilds). Cancellation had 7.0444 ms
across 61 frames (0.1155 ms/frame, 54 hits / 7 rebuilds). These averages include
necessary rebuilds. The full paired snapshot still costs roughly 1.5 ms/frame.

### Late Shutdown Crash and DXC Lifetime

The final process inventory found CrashReporter PID22048, created at 01:57:51.
It belonged to run 192, not run 196. Its report directory is
`Cyberpunk2077-20261002-015750-21892-9388`. The exception is a null+8 read in
`dxcompiler.dll+0x300168`. The matching run-190 DLL/PDB stack scan includes the
`ModernReflection` static `utils` destructor at plugin+0x322E51 and CRT DLL
process-detach cleanup. This is a stack scan, not a complete unwind, but the
late static COM release is directly present in the corresponding source.
Run 192's summary now explicitly records this late audit and is marked failed
overall, preserving its successful rendering subtests and original harness result.
The leftover reporter was closed. This was not dismissed as random.

The reflection helper now caches only the loaded factory function. `IDxcUtils`
and the shader reflection object are local to each call, released in dependency
order under the reflection mutex while the compiler/runtime are still usable.
No DXC COM object is released by a plugin static destructor. The compiler module
remains loaded for the process lifetime. This follows Microsoft's
[DLL process-detach guidance](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain)
on avoiding cross-component cleanup during process termination.

The standalone policy test repeats reflection 16 times. Two new tests load the
policy in a separate DLL, confirm the requested real compiler was loaded, then
exercise process termination with that owner still loaded and three explicit
unload/reload cycles. Both pass. The launch suite now observes crashes/reporters
for five seconds after stopping the game, closes late reporters, and fails the
overall result for an exit-time crash. Python fixtures cover a delayed report
and reporter, and preservation of pre-existing unrelated reporters.

Run 197 (PID16808) passed all 19 checks after the lifetime fix, including the
240-frame interval and 61-frame cancelled interval. Its post-shutdown observation
reported no crash and no reporters. The game was closed by the existing timeout
termination path; this is not a claim that every natural game-exit path was tested.
The separate DLL process-exit fixture covers the relevant automatic detach path.
Final local results: 56 CTests, 168 single-pass Python tests, 23 launcher tests.

Installed archive: `build/single-pass-20260927/deploy-reflection-lifetime-197`.
DLL SHA256: `4717339ed065526d7bb8a93e69f76d037e3ff8fe0465123bda1a9cfde937a776`.
PDB SHA256: `e4965160593d2ef5b887ad51c5757bb52240c2fe9cc0c7900ec1222590c94420`.
No commit/push was made. Native mesh preparation still runs for both eyes; these
changes reduce prototype overhead and fix shutdown lifetime, not complete the
full single-pass objective or establish a net overall FPS improvement.

## Scoped Draw Origins and Membership Planning (2026-10-02)

`CurrentDrawGeometry` previously recovered the native flush context by unwinding
the stack at every selected MAIN draw. A scoped alternative now observes native
`0x1F1FA8`, only from the verified `0x1F19DA` return site inside `0x1F191C`.
Both code bodies are checked before installing the paired-root-only hook:
0x1F1FA8/0xF0 bytes/FNV64 `3EFA042B17CB96AB`, and
0x1F191C/0x168 bytes/FNV64 `39E2FE24CD281FF6`.

The MASM wrapper retains input/output GPRs and XMM0-5, preserves the nonvolatile
register inputs, and copies the fifth stack argument. It reserves the sixth
stack slot as native scratch, as required by the real routine. Unknown callers,
disabled origin mode and inactive depth work take the original path. The wrapper
uses unwind metadata and a canonical ADD/RET epilogue following Microsoft's
[x64 prolog/epilog rules](https://learn.microsoft.com/en-us/cpp/build/prolog-and-epilog?view=msvc-170).
Flags are not a return contract here: the exact native caller immediately clears
its batch count with AND, and the native callee's prologue overwrites input flags.
The fixture intentionally clobbers volatile state in both callbacks, writes the
native scratch slot, and tests active, disabled, inactive and unrelated callers.

A bounded thread-local stack keeps nested native invocations separate. The
scope retains identities only, not GPU resource ownership. Every resolution still
reads the live flush, batch draw pointer, packet flags and complete 64-byte draw
record, applying the original renderer/instance/stride/ordinary-geometry checks.
The old unwind remains both a reference and a fallback.

`GeometryOriginMode`: 0 legacy, 1 compare (default), 2 scoped. The reference mode
requires identical success and draw bytes. `GeometryOriginStats[4]` counts equal
valid comparisons, mismatches, scoped resolutions and legacy-only resolutions.
Mode 2 requires `GeometryOriginVerified=1`, at least 128 reference comparisons
and zero mismatches. Continuous-run reports check actual usage and reject a
scoped interval with residual unwind calls. The mode is restored after draining.
`--origin-benchmark` adds a legacy interval before the scoped/cancellation ones.

Run 198 (PID18316) passed all 20 checks. The reference produced `[128,0,0,0]`.
Legacy and scoped intervals each completed/admitted 240 frames, followed by a
61-frame scoped cancellation. They used 30720 legacy calls, then 30720 scoped
calls, then 7808 scoped calls, with no mismatches or scoped fallbacks. MAIN plan
time was 1382.0670 ms legacy / 1316.7697 ms scoped per 240 frames, only about
0.27 ms/frame difference. This disproved the assumption that stack unwinding
accounted for most of the roughly 5.5 ms MAIN planning cost.

The peer-only candidate search then gained a geometry index. It preserves the
first occurrence order within each geometry, duplicate counts, 48/64-byte stride
separation and unknown-range rejection, but no longer scans unrelated geometry
for every draw. Run 199 (PID12908) passed the same 20 checks and 240/240/61 frames.
MAIN plan totals were 1150.4543 ms legacy / 1103.4381 ms scoped per 240 frames.

Finally, membership planning stopped allocating a temporary hash node/string for
every instance debit. It finds stable table entries, groups their pointers using
`std::less`, preserves original occurrence order within each key, validates every
group, and only then commits counts. Failed queries leave output/counts unchanged.
Tests include 3200 deterministic randomized queries against the previous hash-debit
algorithm, unread peer ranges, duplicate ordering, both strides, 64 interleaved
geometry groups and transactional oversized extras.

Run 200 (PID17852) again passed all 20 checks and 240/240/61 frames, with all origin
comparisons/mode checks intact. MAIN plan totals were 1086.1734 ms legacy /
1036.5461 ms scoped per 240 frames (4.5257 / 4.3189 ms/frame). Scoped draw handling
was 4469.9329 ms total (18.6247 ms/frame). These are nested CPU wall intervals,
not additive frame savings. Changes between builds are separate launches with
some scene variation, not a controlled overall-FPS comparison. The scoped-origin
legacy/scoped comparison within each launch is isolated by its mode counter.

Runs 198-200 closed their games and observed no new crash report or reporter in
the post-exit window. The code still prepares both native eyes and records the
conditional native fallbacks. These changes improve origin ownership and reduce
prototype matching overhead; broader native single-pass preparation and positive
end-to-end FPS improvement remain unproven.

Installed archive: `build/single-pass-20260927/deploy-membership-plan-200`.
DLL SHA256: `044e02dd1471aaad41c42cfe56f4ad457079a743fb63d73f7b6f1686f5e0a262`.
PDB SHA256: `70fb2f544a429dda1cf2e17de75265ab704adc6707a29d15bdbf123386e68f35`.

Run 201 (PID27588) passed the nine-check normal control without the paired-root
or binding-observer environment. HUD readiness was 1031 ms / 93 submissions /
81 source advances. The active run-200 and ordinary run-201 stereo images were
inspected. Both owned games are closed, post-exit checks found no reporter, and
settings hashes match the archive. Preview dimensions returned to 2193x16.
Final checks: 58 CTests, 169 single-pass Python tests, 23 launcher tests and
whitespace validation. No commit/push; the full single-pass goal remains active.

## Full Compatible Depth Cohort (2026-10-03)

The continuous depth route and its reference warmup no longer select only the
first 128 MAIN sources. `--early-depth-gpu all` requests the full compatible
cohort. `AllDraws=UINT32_MAX` is a request marker, not an allocation size. Source
publication is bounded at 1024 entries, instance rows at 4096, and instance bytes
at 8 MiB. Exported capacity values are checked by the reader. Full-cohort tests
reject reaching the source bound; they cannot silently bless a truncated pass.
Explicit numeric selections retain their exact-count validation.

The append-only CBV arena now supports 1024 draws, with 4 MiB per paired root
in this route. Root and GPU-fence lifetime checks are unchanged. The new GPU
`optimized-capacity` fixture fills the arena, rejects an extra draw, compares
actual depth/stencil, and reuses it across four fence-completed generations,
including forced fallback and mismatched initial depth. This is tested on the
real D3D12 device with validation enabled, not only a CPU capacity fixture.

The offline `depth_family_coverage.py` resolves the registry's **one-based**
asset IDs against the exact hashed manifest. It also accounts for an important
trace detail: `DrawBegin` is emitted by `DrawRecorded` after the first draw hook.
The analyzer includes that immediate same-list predecessor and requires the
exact `DrawEnd.flags0` source count. It does not misclassify the first draw as
outside the pass, or assume later uncaptured scopes have complete draw traces.
Regression fixtures cover asset numbering, the first draw, incomplete counts,
uncaptured scopes and selected-source ownership.

Run 202, PID17916, used DLL
`af576dbfbaacf8853d5fe27f649e6eb66ae3a9b07eb89f2ab261aae8598e1924`.
Its reference selected and matched 160 MAIN sources, versus the old cap of 128.
All 225 MAIN / 226 VRCAM native draws remained covered by the whole-target
comparison: depth and stencil differences were zero for both eyes. The exact
family census for these scopes is:

| Family | MAIN / VRCAM draws | Current resource category |
| --- | --- | --- |
| 42 | 204 / 205 | Camera b1 + material b5 only |
| 35 | 5 / 5 | Skinned VS, t10/t77 plus b7/b8 |
| 39 | 2 / 2 | Skinned VS + pixel shader |
| 43 | 8 / 8 | Pixel shader, including bindless resources |
| Unregistered | 6 / 6 | No linked family in this package |

160 of family 42's 204 MAIN draws were selected. The other 44 share that shader
but still fail current input eligibility; their DEFAULT-heap instance ranges
must be investigated, not admitted on PSO or GPU-address equality alone. The
earlier informal count of 43 omitted the first draw before DrawBegin. Shader
classification is not a proof of complete material, instance or resource-version
ownership. No shader/resource guard was relaxed by this change.

The continuous run completed/admitted all 240 frames, selecting 160-161 sources
per frame, with no references, mismatches, busy slots or fallbacks. It executed
70,561 GPU draw commands against 108,241 native source commands. The separate
61-frame cancellation also completed/admitted every started root and restored
its controls. The median sum of the two measured depth GPU ranges was 0.332672
ms, including copies and admission. This is not frame-critical-path timing or
evidence of an overall FPS improvement; no native A/B timing was run here.

Run 203, PID24708, passed the new `--depth-variants` workflow. The ten-degree
turned pose selected/matched 150 sources with zero depth/stencil differences in
both eyes; the returned pose selected/matched 160 with the same exact equality.
Forced rejection then produced CPU admission 0 and GPU decision 0, zero
reference draws, and all 227 native peer batches (66 unconditional + 161
fallback) executed. Both image halves moved and returned, and the simulator
pose was restored. These are bounded depth and motion checks, not proof of
scene-wide color/temporal correctness.

Run 204, PID12372, passed the nine-check ordinary control without paired-root
or binding-observer opt-ins. Startup HUD readiness was 1016 ms / 92 submissions /
66 source advances. All three games closed through the timeout termination
path, with no crash/reporters during the five-second post-exit observation.

Installed archive: `build/single-pass-20260927/deploy-full-depth-202`.
PDB SHA256: `8f9e35799bd518bfbee357d461ed2b245805aee74bb73f14a7234f067cd56dc5`.
The native engine still prepares both eyes, and the conditional fallback
commands are still recorded on the CPU. This change expands actual paired GPU
coverage; native once-only preparation, broader passes, retained downstream
arrays and a positive end-to-end performance result remain required.

Final checks: 59 CTests, 176 single-pass Python tests, 23 launcher tests and
whitespace validation passed. Installed DLL/PDB and all preserved settings
match the deployment manifest; preview settings and simulator pose were restored.
Active, turned and ordinary stereo images were inspected. No game, crash reporter
or capture replay remains running. No commit/push was made.

## GPU-Only Instance Admission (2026-10-03)

Run 205 expands the actual paired path from 160 to all 204 MAIN draws of family
42 in this scene. The additional 44 did not require another shader conversion:
their complete instance bytes were unavailable to the per-frame CPU upload
shadow. No persistent CPU cache was seeded from unverified GPU addresses.

Preuploaded packet ranges now populate a separate prospective membership domain
using the exact resource identity and per-instance GPU address. Host-generated
transforms remain in their existing byte-comparison domain. Native draws select
the GPU domain only when their actual bound instance resource is that same
preuploaded resource. Material, camera, IA geometry, instance masks and late
peer coverage checks remain in place. GPU-address identity alone is explicitly
insufficient for admission.

The optimized pass owns two lazily allocated instance snapshot buffers. Every
used MAIN range is copied at its actual draw position, using a fresh native IA
resource lease and verified read state. Before the first VRCAM draw, the same
ranges are copied from its freshly verified source binding. A compute shader
compares every DWORD of the packed snapshots and clears the existing admission
predicate on any difference. This runs before the conditional array import and
before any optimized peer draw. Initial depth/stencil comparison and late CPU
admission are still required. No CPU readback wait was added. Both snapshots are
retained with their command recordings and reused only after the owning pass's
MAIN and peer fences have completed.

Copy/discard operations and write-state/alias barriers on the watched source
within the active peer group abort that experimental pass instead of allowing a
change after the snapshot. The current route requires one common preuploaded
source allocation, exact known read states, non-UAV input and bounded ranges.
GPU-only peer extras still use native draws; they are not uploaded as if their
identity tokens were transform data. The binary diagnostic row domain is 3 for
MAIN GPU identities and 4 for peer GPU identities (0/1 remain CPU data, 2 extras).

The gate's GPU test detects a change in the final byte of a comparison buffer,
rejects invalid ranges and proves that complementary predicated copies choose
the original path. The new `optimized-instances` pass test copies disjoint input
ranges, validates actual depth/stencil output, tests a GPU-side one-byte mutation
between the two eye recordings, and reuses the buffers through four completed
generations with CPU rejection and initial-depth rejection as well. Tests run
with the real D3D12 debug layer. The fence discipline is consistent with
Microsoft's [readback synchronization documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/readback-data-using-heaps):
Map alone is not a completion proof. Production admission here stays on the GPU;
only the existing final diagnostic decision is read after the pass fence.

Run 205 (PID1900) passed all 20 planned checks. Its exact reference matched all
204 selected sources and all 225/226 native depth draws, with zero depth/stencil
differences in both eyes. 147 selected draws used the GPU-only identity domain.
The continuous 240-frame interval selected/matched 204 sources every frame and
admitted every completed frame, with zero deferred predictions, fallback frames,
reference draws or origin mismatches. It verified 35,280 instance ranges,
13,939,200 bytes in total (147 ranges / 58,080 bytes per frame). It executed
60,718 draw commands against 108,238 native source commands. Cancellation stopped
after 61 completed/admitted roots, with 8,967 ranges / 3,542,880 bytes verified.

The ten-degree pose reference selected 193 sources and the returned reference
selected 204; both had exact depth/stencil equality. Forced CPU rejection copied
and checked 147 ranges / 58,080 bytes, produced GPU decision 0, and executed all
227 peer source batches (23 unconditional + 204 fallback). Both scene images
moved and returned; the simulator pose was restored. The game closed and the
five-second post-exit observation found no crash or reporter.

Important negative result: median summed depth GPU time was **1.153104 ms** in
the 240-frame interval, versus 0.332672 ms in the preceding smaller-cohort run.
These are different launches, not a controlled overall-FPS comparison, but the
per-draw instance snapshot copies/state transitions are plainly too costly for
the final path. Next work must batch or otherwise reduce those operations while
preserving the byte/version proof. The new result is wider verified rendering,
not a demonstrated optimization in total GPU time. Native once-only CPU scene
preparation, the remaining shader/resource families, downstream array retention
and positive end-to-end performance remain unfinished.

Installed archive: `build/single-pass-20260927/deploy-gpu-instances-205`.
DLL SHA256: `8a5f5508de5d55472e94d525eaa204493852132cd9e6ea4fba559f32eae882dd`.
PDB SHA256: `39d98060cd22e18c37218a4ad0fcb44bf6ff0045d58102080e778fe0f1b66945`.
Local checks at this point: 60 CTests and 177 single-pass Python tests passed.
No commit/push was made; the full single-pass goal remains active.

### Combined Instance Read Scope

The snapshot path above initially changed the same native source from vertex
read to copy read and back for every MAIN draw. The retained implementation
instead adds COPY_SOURCE to the verified read-only state once, allowing the
native vertex fetches and our snapshots to share that read state. It restores
the exact original state before the game's transition/alias barriers, at MAIN
finish/abort, and before command-list Close. A reused pass cannot restart with
an outstanding augmented state. No write flag is combined with read flags.
Microsoft explicitly permits [multiple read-only resource-state bits](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12#readwrite-resource-state-restrictions).
The GPU fixture also exercises releasing/re-entering the scope and idempotent
release. All per-draw snapshots, the complete byte comparison, native fallback
and fence lifetime requirements are preserved.

`--instance-benchmark` isolates the two state-management modes within one owned
game/process/build. `EarlyDepthInstanceReadScope` defaults to 1; mode 0 retains
the old per-draw transitions solely as a baseline. Each Work snapshots the
mode at its root boundary, and the test restores mode 1 after draining. Run 208
(PID6276) passed all 21 planned checks on the same scene:

| Mode | Completed / admitted | Selected per frame | Instance proofs | Median summed depth GPU time |
| --- | --- | --- | --- | --- |
| Per-draw transitions | 240 / 240 | 204 | 35,280 ranges / 13,950,720 bytes | 1.145728 ms |
| Combined read scope | 240 / 240 | 204 | 35,280 ranges / 13,950,720 bytes | 0.374160 ms |

Both intervals had exactly 54,000 MAIN + 54,240 peer source draws and 60,720
executing draw commands, zero references, zero fallback frames and zero deferred
predictions. Thus the measured median difference is 0.771568 ms within this
depth route, not a draw-count substitution or a smaller validation scope. This
is a sequential A/B interval, not a randomized whole-frame benchmark. The new
route still has more GPU overhead than the earlier native-only depth baseline;
no total-FPS increase is claimed.

The 61-frame combined-mode cancellation also completed/admitted every root.
Initial, turned and returned native depth/stencil comparisons were exact, and
forced CPU rejection selected the native peer path. The game closed with no
crash/reporters during the post-exit window. An earlier ordinary control 206
(PID2600) failed before tests because Windows rejected the focus change, not
because of a game crash; it was closed. Retry 207 (PID18268, run-205 DLL) passed
all nine ordinary checks, including 1063 ms / 95 HUD submissions / 81 source
advances, and also closed without a report.

Current installed archive: `build/single-pass-20260927/deploy-instance-read-scope-208`.
DLL SHA256: `bb2f919d46042002c22126a41aac0378909e35ea0d8401d491118e419228461f`.
PDB SHA256: `8873eef5d6fb95e74b0abb912d573ddba90f91f1339a3b2fc10a6da4cc594bd1`.
The remaining 21 MAIN draws are still native (skinned/extra resources,
pixel-shader families and unregistered families). This remains a depth-only
experimental route, not once-only native scene preparation or complete
single-pass rendering.

Run 209 (PID21036) passed all nine ordinary checks on the final run-208 DLL,
without binding/family opt-ins. HUD readiness was 1047 ms / 94 submissions /
75 source advances. It closed with no late crash/reporters. Active run-205,
active run-208 and ordinary run-209 images were inspected. Installed DLL/PDB,
both UserSettings files, launcher/mod INIs and absent calibration file match
the deployment manifest. Preview settings and head pose were restored; no game
or replay remains running. Final checks: 60 CTests, 177 single-pass Python tests,
23 launcher tests and whitespace validation. No commit/push.

## Dense Descriptor Metadata (2026-10-03)

The next CPU target was chosen from the existing observer profile: pipeline/root
binding accounted for much less time than CBV creation and descriptor copying.
No family-binding rewrite was made. Instead, Store now keeps value IDs in dense
arrays for known heap slots and retains the existing sparse map for unknown,
unaligned or over-budget addresses. CPU handle bases remain opaque; indices are
calculated using the registered heap's actual stride. A small heap-index cache
is invalidated by heap creation/removal and representation changes.

The dense allocation budget is four million slots (16 MiB of IDs), shared across
heaps. Canonical 160-byte descriptor descriptions remain interned with their
existing lifetime/refcount rules. VersionPage stamping, whole/ranged invalidation,
heap cookies, overlap handling and captured descriptor values are unchanged.
Switching storage first builds a new representation and then commits it under
the same metadata lock. It neither changes refcounts nor stamps a descriptor
mutation merely because the storage layout changed. Heap destruction and address
reuse still remove prior values. No GPU descriptor or rendering API is rewritten.

The Store fixture adds 20,000 differential operations against a sparse Store:
three non-aligned heap bases, different strides, writes, copies, unknown sources,
heap removal/recreation, repeated live representation switches, exact range
versions and descriptor values. The API-level test also changes modes while a
captured descriptor table awaits submission and checks that its version and
validation result remain intact. Existing million-slot, collision, lifetime,
multithreaded heap-retirement and byte-range oracle tests remain enabled.

Run 210 (PID18032) passed all 21 planned checks. Its sparse/dense/dense/sparse
intervals each completed/admitted 240 frames with no rendering or ownership
failures. CBV callback mean inclusive time changed from 9.053 / 9.191 us in the
sparse intervals to 5.127 / 5.136 us in dense intervals. Descriptor-copy callbacks
changed from 9.781 / 9.902 us to 5.304 / 5.323 us. There were roughly two million
CBV and 3.8 million copy callbacks per interval. These times include contention
across threads and must not be summed into total frame-time savings.

To measure the actual experimental frame cadence, each Work now records a QPC
timestamp at MAIN depth-group entry. It is published with the GPU-completed row
under the same sequence lock. The reader sorts by frame identity and requires
consecutive roots and strictly increasing timestamps; it does not infer cadence
from draw counts. Three new constraints are checked together: native paired-root
completion, GPU pass completion/admission and the timestamp sequence.

Run 211 (PID13788) repeated the balanced test on the final build:

| Interval | Mode | Frames admitted | Median period | Mean period | Pairs/s |
| --- | --- | --- | --- | --- | --- |
| 0 | Sparse | 240 | 88.4685 ms | 88.8066 ms | 11.2604 |
| 1 | Dense | 240 | 79.3194 ms | 79.5037 ms | 12.5780 |
| 2 | Dense | 240 | 79.3634 ms | 79.5437 ms | 12.5717 |
| 3 | Sparse | 240 | 88.1966 ms | 88.4026 ms | 11.3119 |

Each interval selected 48,960 sources (204 per frame), compared 35,280 GPU
instance ranges / 13,881,600 bytes, and used the same resolution and admission
rules. Executed draws were 61,201 / 61,200 / 61,200 / 61,200; no fallback frames
or reference draws occurred. The inclusive draw handler averaged 19.9722 /
17.3364 / 17.2715 / 19.9555 ms per frame. Those handler timings are nested CPU
wall intervals, separate from the approximately 9 ms cadence reduction above.

The additional unprofiled dense interval completed/admitted 240 frames with a
79.3702 ms median period; cancellation completed 61 roots cleanly. Full initial,
turned (193 selected draws) and returned (204) depth/stencil references matched
both native eyes exactly. Forced rejection retained the native peer path. The
head pose and preview settings were restored, and shutdown observation found no
crash or reporter. The 12.57 pairs/s result still describes the much slower
experimental observer-heavy path, not ordinary gameplay or a completed
single-pass implementation. Native per-eye scene preparation remains duplicated.

Installed archive: `build/single-pass-20260927/deploy-descriptor-cadence-211`.
DLL SHA256: `f9595be55aa07484ad93cf1f5b84d16f69bcc7b1ea22777035c26b3dfc17c65e`.
PDB SHA256: `ee149d4fb0ef52850efa474ad1068f1297162a200c4bc7cc039c2c9962599cdf`.
Run 212 (PID4752) passed the ordinary nine-check control without binding/family
opt-ins, with 1031 ms / 92 HUD submissions / 66 source advances at readiness.
The game closed through the normal timeout termination path and no late crash
or reporter was observed. Local checks: 60 CTests, 179 single-pass Python tests
and 23 launcher tests passed. No commit/push; the full goal remains active.

Final verification: active dense-mode and ordinary control images were inspected.
Installed DLL/PDB and all preserved settings match the deployment manifest;
preview and pose were restored, and no game/reporter/capture replay remains.
Whitespace validation also passed.

## Original-Root Camera Bindings (2026-10-03)

The existing paired resource bank replaces descriptor heaps and root bindings
for each selected draw. Extending it directly to texture-heavy passes would
require private copies of their shared/bindless tables. A second path now keeps
the original root signature and native descriptor heaps, changing only the
camera buffer through the engine's existing uploader/binder. This is compatible
with the documented [root-signature command-list semantics](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-a-root-signature):
switching to a compatible PSO does not itself change the bound root signature.

`patch_camera.py --main-first` keeps the native camera handle/register but adds
the other eye at a 1024-byte stride. ViewID is XORed with 1, so layer 1 reads the
unchanged MAIN prefix and layer 0 reads the predicted peer camera. Only camera
load indices/metadata change. The initial runtime whitelist remains native VS
`17f7e646a02eeaa6ea496dd75a21a494588b29dd926a545fb52bee45dec68117`, with no PS.
Material bytes still require exact late equality, and all instance/depth/stencil
admission checks remain enabled. Other resource families are not admitted by
this change.

The package builder verified original and existing shader hashes, assembled
the rewritten DXIL, regenerated ViewID metadata and ran the DXC validator.
Candidate shader SHA256:
`723d33be139cb213e14cb92e3a345727d8e726d85c0417e5e5a9e037818095b9`
(6415 bytes). Package:
`build/single-pass-20261003/native-camera-families/families.json`, SHA256
`29d4d1373fe91bb152811cffd59a05c4fdd98c8a0976386b17b50330a198f5bc`.
The runtime also creates an original-root view-instanced PSO for that variant.
The captured-pipeline batch linked 98 PSOs, with two excluded and zero failures.

At MAIN group entry the native allocator uploads the two cameras into one
2048-byte buffer and writes an owned CPU descriptor. The engine copies that
descriptor into its normal heap during recording; its upload allocator owns
the GPU bytes. The CPU descriptor heap is retained by the fence-managed Work.
Each selected draw verifies the actual bound CBV address and size; the first
use double-reads all 2048 bytes against the two prepared camera blocks. The
draw changes PSO/targets/view mask but no longer switches heaps or root
signature. The original native camera handle is rebound at group exit even
after a probe failure. Scoped capture of the uploader result is RAII-managed.

Native helper code fingerprints checked before this route:

| RVA | Bytes | FNV64 |
| --- | --- | --- |
| 0x1F0114 | 0xAD | 76EEBB3582ED71F9 |
| 0x1F3978 | 0xF1 | D77BA750BFB6EFC7 |
| 0x1F405C | 0x33 | 96D1A06A9C2CBA7F |

The standalone GPU fixture uses two cameras with the original root and verifies
actual depth/stencil and fallback output. Python tests check MAIN-prefix eye
selection and reject missing upload, byte validation or draw coverage. The
ordinary CPU snapshot test links the camera module with its tracing control
disabled; no native engine call is made there.

Run 213 (PID12836) correctly stopped before any replacement draw, reason 45,
because the CreateCBV observation hook was still gated only by the legacy twin
probe. No game crash occurred. The hook now also observes the scoped native
camera request. Run 214 (PID21636) then passed all 21 checks, including exact
whole-target depth/stencil comparison and a same-process comparison:

| Path | Frames admitted | Camera uploads / paired draws | Private arenas | MAIN emit CPU total | Median pair period |
| --- | --- | --- | --- | --- | --- |
| Private CBV banks | 240 | 0 / 0 native-camera | 1, reused 239 times | 612.0800 ms | 80.6624 ms |
| Original-root camera | 240 | 240 / 48,960 | 0 | 372.2403 ms | 79.3221 ms |

Both paths selected 204 draws per frame and executed 60,720 GPU draws against
108,240 native source draws, with all admissions, no reference draws and no
fallback frames. GPU depth sums were effectively unchanged (0.368976 vs
0.367776 ms median). MAIN emission saved about 1 ms/frame in this prototype;
these nested CPU intervals and the sequential cadence test are not a claim of
ordinary-game FPS improvement.

After tightening temporary-capture cleanup and validating the CPU-only heap,
run 215 (PID17528) passed all 20 checks on the retained build. The scene selected
205 draws per frame: 240 uploads / 49,200 native-camera draws, then 61 uploads /
12,505 draws during cancellation, with zero private arenas and every frame
admitted. Initial, turned (194 draws), and returned (205) references matched
native depth/stencil exactly. Forced rejection kept the native peer path. The
game closed without a late crash/reporter and the pose/preview were restored.
The separate-run median pair period was 81.0305 ms; this variation reinforces
that the main result is the binding architecture, not a new overall-FPS claim.

Installed archive: `build/single-pass-20260927/deploy-native-camera-final-215`.
DLL SHA256: `79ec2e0c33b4514efb0289931c4acd568cebd522a8d482f70167b8ae9d648a1e`.
PDB SHA256: `c0c9d2667cd9dc690d08d7b493d90d82a9eaba7d08d97724ab42771f1c37a641`.
The new route remains opt-in (`--native-camera`, or `--camera-benchmark`) and
requires its explicit shader package. No native CPU scene-preparation work has
been removed, no texture-dependent pass is claimed complete, and the full
single-pass goal remains active. Local checks: 62 CTests, 182 single-pass Python
tests and 23 launcher tests passed. No commit/push.

Run 216 (PID16616) passed all nine ordinary control checks on the final DLL,
without the family/binding opt-ins. HUD readiness was 1031 ms / 93 submissions /
71 source advances. The game closed without a late report/reporter. Active
run-215 and ordinary run-216 images were inspected. Installed DLL/PDB and all
preserved settings match the deployment manifest; preview settings and pose
were restored. No game or capture replay remains running. The dedicated GPU
native-camera rejection fixture also passed with the original fallback branch.

## Live Descriptor Metadata (2026-10-03)

The continuous binding route still maintained the full capture's descriptor
range-write history and interned every transient CBV's 160-byte API description.
Neither is required by its current-value CBV checks. `DescriptorLiveMetadata`
defaults to 1 inside the existing opt-in observer, and is active only during
binding-only intervals with no recording or pending captured command lists.

Live mode retains every current descriptor value, heap lifetime, copy and
invalidation. It suspends range history and stores streaming CBVs without
hash interning. Copied slots retain their shared value; subsequent overwrites
use copy-on-write unless the destination is the sole owner of a non-interned
value. Other descriptor types still use the existing interner. This follows
the immediate CPU-copy semantics of
[D3D12 descriptor copies](https://learn.microsoft.com/en-us/windows/win32/direct3d12/copying-descriptors).
It does not remove material/instance checks or authorize new shader families.

Entering a full capture always restores history. A new monotonic generation
invalidates old range versions; pending captures prevent entering live mode
until submission or reset. The native-camera bindings, GPU admission gate and
all depth/stencil comparisons are unchanged. The normal launch has no binding
observer, so this optimization does not change its rendering path.

Store tests cover copied-value independence, sole-owner updates, 20,000 mixed
operations against the original value oracle, dense/sparse representation
changes, history suspension/resumption and exact post-resumption versions.
The WARP API test switches full/live modes, overwrites copied CBVs, and checks
that pending full captures still detect later descriptor mutation.

Run 217 (PID5244) passed all 21 checks on one installed DLL. The new
`--metadata-benchmark` executes full/live/live/full 240-frame intervals with
identical native-camera, depth-routing and dense-storage settings:

| Mode | Median pair period | Mean pair period | CBV callback mean | Copy callback mean |
| --- | ---: | ---: | ---: | ---: |
| Full before | 79.7146 ms | 79.8406 ms | 5.1746 us | 5.3714 us |
| Live first | 73.1100 ms | 73.2666 ms | 1.6275 us | 1.9667 us |
| Live second | 73.2317 ms | 73.3536 ms | 1.6074 us | 1.9457 us |
| Full after | 80.1728 ms | 80.2408 ms | 5.5223 us | 5.6539 us |

All 960 frames were admitted with no fallback/reference draws. Three intervals
selected 48,960 source draws; live-first had one additional selected/native
MAIN draw (48,961), so the scene was not literally byte-identical throughout.
GPU depth sums remained about 0.368-0.369 ms. Callback intervals are inclusive
wall times across threads and cannot be added as critical-path savings.
The observed pair-period reduction is about 6.8 ms within the experimental
path, not a native-game FPS improvement or shared native CPU preparation.

The unprofiled live interval also passed 240 frames (73.1689 ms median), followed
by cancellation after 61 fully admitted frames. Initial (204 draws), head-turn
(193) and returned (204) depth/stencil references matched exactly for both
eyes; forced rejection retained the native peer path. Full capture worked
again after the live intervals. Every interval sampled the actual metadata
mode and confirmed capture mode restoration after completion.

The remaining upload registry observer is a separate measured cost:
`TouchUpload` takes its global mutex and updates the LRU clock for each CBV.
Its callback averaged 5.87-5.96 us during the live intervals, across roughly
2.03 million calls each. A subsequent optimization must preserve resource
lifetime and fresh lookup on actual reads, not cache raw resource pointers or
infer CBV ownership merely from a recycled GPU address.

Run 218 (PID20004) passed all nine ordinary controls without binding/family
opt-ins. HUD readiness covered 1031 ms, 93 submissions and 76 source advances.
Both owned games were closed via the existing close-timeout termination path;
the five-second shutdown watches found no crash/reporters. Active and ordinary
images were inspected. Preview/pose and all saved settings were restored and
hash-verified; no game or capture replay remains.

Installed archive: `build/single-pass-20260927/deploy-live-metadata-217`.
DLL SHA256: `67ed43e16cb2a08ce3d345dbd03cac396d9a77b11e883ba7de2875110aeb3ee7`.
PDB SHA256: `2dfa211869937ec9d9cda8aa0c7fc0f9809911836eea2039516d8bd3ab872bab`.
Local verification: 62 CTests, 183 single-pass Python tests and 23 launcher
tests passed. No commit/push. Once-only native preparation, broader passes,
retained arrays and positive end-to-end performance remain unfinished.

## Upload Registry Fast Path (2026-10-03)

`GetGPUVirtualAddress` already exposes a live native uploader resource and its
actual base address at the two audited caller sites. The old observer queried
the resource/heap again and then took another global registry lock when the
following CBV was created. The new `UploadAddressObserved` path first matches
that API argument against the retained registry owner. A one-use
`UploadTouchReceipt` contains only base/extent/generation, never a pointer or
constant payload. A changed generation or mismatched range takes the old path.

Run 219 (PID15536) verified this initial receipt path and all 21 launch checks,
but only about 48% of CBV touches consumed a receipt. The first baseline median
was 74.5905 ms versus 74.2247/73.7868 ms in the receipt intervals. This small
result motivated covering the other already-known ranges, not calling the
initial implementation a meaningful final frame-time improvement.

The retained version adds one thread-local weak lookup hint. It validates the
registry generation, locks the existing owner, checks the complete range (and
exact resource/base pair for API observations), then rechecks the generation.
It holds that owner only while updating the advisory LRU timestamp. Weak hints
do not pin resources between calls. The registry still owns at most 256 entries
and 512 MiB; evictions change the generation. LRU updates use an atomic maximum
so delayed threads cannot move a resource's timestamp backward.

`ReadUpload` deliberately does not use either fast path: it still locks the
registry, resolves the current retained resource, checks bounds and reads fresh
mapped bytes. Nothing here treats a GPU address as proof of resource identity
or caches mutable constant data. API/lifetime references:
[GetGPUVirtualAddress](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-getgpuvirtualaddress)
and [Map](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map).

Tests cover one-use receipts, invalid/overflowing ranges, generation changes,
foreign resource/base pairs, disabled mode, 8,000 operations on eight threads,
changed mapped bytes, native-reference release, and concurrent reads while
400 other upload buffers enter the bounded registry. A missing current entry
is a miss, never permission to read an expired hint. Existing hot-resource
retention and GPU constant-copy tests still pass.

`--upload-benchmark` runs legacy/fast/fast/legacy in one native-camera launch
with live descriptor metadata and all other rendering settings fixed.
`ConstantUploadFastPath` defaults to 1 within the opt-in family observer. The
six counters separately report known API sources, registration attempts,
one-use receipts, locked CBV lookups, CBV hint hits and API hint hits. The reader
checks their exported ABI count, actual mode/coverage and cleanup.

Run 220 (PID16584) passed all 21 checks on the retained DLL:

| Mode | Median pair period | Mean pair period | Locked CBV lookups | Touch callback mean |
| --- | ---: | ---: | ---: | ---: |
| Legacy before | 74.5493 ms | 75.1062 ms | 2,058,352 | 5.5674 us |
| Fast first | 70.2879 ms | 70.4803 ms | 17,563 | 0.1805 us |
| Fast second | 70.8222 ms | 71.7957 ms | 17,507 | 0.1850 us |
| Legacy after | 74.4597 ms | 74.8926 ms | 2,063,507 | 5.5748 us |

The two fast intervals consumed 996,384/995,104 one-use receipts and
1,049,910/1,048,318 weak range hints. Their 996,386/995,106 known API sources
also used weak hints. Both baseline intervals had zero fast-path hits.
All 960 frames were admitted without fallback/reference rendering. First-fast
selected 48,962 draws and the other intervals 48,960; executed commands varied
from 60,960 to 60,965 with live scene changes, so this is not an immutable-scene
benchmark. GPU depth medians stayed within 0.3667-0.3683 ms. The roughly 4 ms
pair-period improvement is experimental observer overhead, not native-game FPS
gain. Inclusive callback times cannot be summed as frame-time savings.

The unprofiled 240-frame interval measured 70.5305 ms median, and cancellation
drained after 61 admitted frames. Initial (204), turned (193) and returned (204)
depth/stencil comparisons were exact for both eyes. Forced rejection preserved
native peer rendering. Run 221 (PID20752) passed all nine ordinary controls
without family/binding opt-ins; stable HUD covered 1047 ms, 93 submissions and
80 source advances. Owned games closed through the existing close-timeout
termination path; five-second shutdown checks found no crash or reporter.
Active/ordinary images were inspected, pose/preview restored, and every saved
setting and installed binary hash matched the deployment manifest. No game or
capture replay remains running.

Installed archive: `build/single-pass-20260927/deploy-upload-lookup-220`.
DLL SHA256: `a49148c194b9145da481645ff0026ba72f48aaf2c71352dd96c58c1160348c32`.
PDB SHA256: `331f75ce328bd7c0d71609b2ea0505f61eab31a9799d935081b1679514fc27f0`.
Final local checks: 62 CTests, 184 single-pass Python tests and 23 launcher tests.
No commit/push. Shared native CPU preparation and broader shader/pass coverage
are still required; this step does not claim either is implemented.

## Original-Root VS and PS Families (2026-10-03)

The native-camera package builder now handles complete VS/PS families with
`--all-families`. It validates every original resource binding against the
family contract, checks native hashes/sizes, converts camera loads at b1 and
keeps all other register/space assignments. Camera-free stages are upgraded
to SM6.1 metadata without claiming they are eye-independent. MAIN remains the
first 1024-byte block; `ViewID ^ 1` selects the appropriate camera in each
converted stage. The manifest explicitly records VS/PS camera stage bits,
2048-byte binding size, stride and hashes. The old single-VS manifest still
loads and passed the captured-PSO batch again.

The inventory contains 94 families and 137 unique shaders: 44 families use the
camera in VS only, seven in PS only, 25 in both, and 18 have no typed camera.
All 137 shader outputs passed the DXIL assembler/validator; 76 families received
native-root camera variants. Pixel-stage ViewID is supported directly by the
[D3D12 View Instancing specification](https://microsoft.github.io/DirectX-Specs/d3d/ViewInstancing.html#sv_viewid),
so no new application varying is needed solely to carry the eye index.

The first real PSO batch linked 67 and rejected 31 with D3D12 message 1105,
`validatingPS stage` in view-instanced pipelines. The camera-only builder now
uses the existing semantic-preserving VS/PS interface linker as well. It
normalizes 42 family interfaces: 41 add the unused upstream clip-distance
declaration, one adds an unused TEXCOORD, and 19 relocate generated front-face
inputs away from occupied registers. Input loads and metadata move together;
the linker checks that all non-interface instructions remain unchanged.
Clipping is retained, not suppressed to make validation pass.

The corrected batch linked all 98 supported captured PSOs, excluded the same
two unsupported stage combinations and reported zero runtime/binding failures.
Native-camera pipeline stage counts were 47 VS-only, seven PS-only and 26
VS+PS, with 18 records without a camera variant. These counts are PSOs, not
unique shader families or automatically merged game draws.

New GPU fixture `native_camera_textured_pass` compiles original SM6.0 HLSL,
runs the actual camera converter and interface linker, then renders with the
same original root signature, material CBV, shared texture and sampler. It
compares both layers of two MRT arrays plus depth/stencil against separate
native eye renders, exercising masks 3/1/2, instancing, dynamic PS camera
indexing, clip distance and front-face inputs. Every compared byte matches.
A separately linked PS that still reads only the MAIN camera produces the
expected peer color mismatch while leaving MAIN and geometry correct. This
negative control ensures the test really exercises pixel-stage eye selection.
This is a GPU fixture, not an in-game G-buffer replacement.

Family registration stores the native-camera PSO and stage bits alongside the
existing 72-byte records under the same publication sequence. The new 16-byte
side record has its own ABI guard. The live reader checks every successful
record against its exact manifest asset, not merely the requested mode.

Development package: `build/single-pass-20261003/native-camera-linked`.
Manifest SHA256: `951ccdb11b463f41b6d79869b223cabec388d64d5d3423e210ed29722d18820a`.
`pipelines-final.json` contains the successful widened batch and
`pipelines-legacy.json` the successful old-manifest compatibility batch.
The initial rejected batch remains in `native-camera-batch/pipelines.json`.

Run 222 (PID4220) passed all 20 owned-launch checks. All 76 camera assets loaded;
the live registry had 193 successful families/PSOs, including 155 native-camera
variants (89 VS-only, 12 PS-only, 54 VS+PS), with zero creation, binding or root
mismatch failures. These are created variants; only the existing depth
admission contract is used to replace actual game draws.

Continuous depth admitted all 240 frames with 48,961 selected draws, 240 native
camera uploads and zero private arenas. Cancellation drained after 61 admitted
frames with 12,446 selected draws. The initial/turned/returned depth/stencil
references matched exactly and forced rejection retained native rendering.
The separate-launch cadence median was 70.6032 ms, not evidence of a new FPS
gain. Neither native CPU preparation nor texture-dependent game passes were
removed by this shader-library change.

Run 223 (PID11456) passed all nine ordinary controls without observation/family
opt-ins. Stable HUD covered 1047 ms, 94 submissions and 81 source advances.
Both games closed via the existing close-timeout termination path, with no
late crash/reporters. Active/ordinary images were inspected. Saved settings,
DLL/PDB hashes, pose and preview restoration were verified; no game or capture
replay remains running.

Installed archive: `build/single-pass-20260927/deploy-native-camera-stages-222`.
DLL SHA256: `454c1a6c2b20ef1ad8da4431f54b239c54b578f65ae45096a444c52d72d3379b`.
PDB SHA256: `41bc6fd477f559b1d7441456f7ba9ea5754932a5a48126507773f01843b62265`.
Local checks: 63 CTests, 189 single-pass Python tests and 23 launcher tests.
No commit/push. The native game uploader still binds the tested VS-only depth
contract; wider passes need stage-aware camera binding, proof for their other
resources, complete per-eye depth and retained arrays before replacement.
Once-only CPU preparation and positive overall performance also remain open.

## Scoped Native VS/PS Camera Binding (2026-10-03)

The native b1 shadow layout was read from the exact `0x1F3978` implementation,
already covered by the full-function fingerprint. State+0x68 supplies the
layout, state+0x28 its descriptor array. The b1 map entry is at layout+0x455
plus stage*0x1C; its parameter selects a 16-byte range record at layout+0x38.
The native formula is `range.offset + 1 - range.firstRegister`. The resolver
bounds the parameter/index and repeats the entire lookup to reject torn state.
No native memory is written directly by this decoder.

`BindCameraProbe` now remembers each graphics stage's own native handle and CPU
descriptor, keyed by the current native context, binding state and command list.
A PS-only bind no longer clears the VS camera record. A fresh matched camera
upload invalidates the previous bound camera until its matching VS bind occurs.
`BeginNativeStages` supports stage masks 1/2/3; the depth runner exposes mask 3
for VS+PS and leaves its previous VS-only mask 1 as the default.

Camera allocation is shared with the existing native uploader. The scoped API
captures both original stage bindings before changing either, rechecks them
after allocation, binds one MAIN-prefix 2048-byte descriptor through the native
binder, and verifies the resulting shadow slots. Cleanup restores each stage
through its original native handle, checks its exact original CPU descriptor,
and preserves a later foreign descriptor instead of overwriting it. Context
changes fail rather than writing another recorder's state. Partial restoration
is reported and can be retried; it is never counted as complete.

Runs 224 (PID20444) and 225 (PID7212) stopped before replacing any draw, failure
45 / stage preflight 6. They did not crash. The detailed second run found a
valid 1024-byte VS CBV for native handle 32570, but its GPU address was absent
from the CPU upload registry. Requiring `ReadUpload` for this native registered
camera path was incorrect. The fixed check uses the caller's fresh native
upload/bind handle, already associated with the captured MAIN payload. Stages
sharing that handle must also agree on GPU address and CBV size. A different
handle still requires two matching CPU-accessible reads of the full camera.
This specific camera identity proof is not permission to treat other mutable
resources as equal or to assume arbitrary recorded GPU copies have completed.

`CameraStageMasks` publishes requested/applied/restored/preserved masks. The
continuous counters accumulate them only with GPU-completed work. Python
validation requires complete stage coverage and cleanup, and restores the
stage control to 1 afterward. Error reports now retain the original failure
and camera diagnostics instead of obscuring them with a coverage error.

New tests exercise native layout decoding, invalid/overflowing/torn reads,
different VS and PS handles, masks 1/2/3, context mismatch, foreign bindings,
partial restoration/retry and idempotent cleanup. CPU descriptor lookup is
also checked against heap retirement. Existing GPU comparisons remain active.

Run 226 (PID4012), with `--native-camera-stages 3`, passed all 20 launch checks:

| Interval | Completed/admitted | Native camera draws | Stage mask totals |
| --- | ---: | ---: | --- |
| Continuous | 240 / 240 | 48,960 | 720 / 720 / 720 / 0 |
| Cancellation | 61 / 61 | 12,444 | 183 / 183 / 183 / 0 |

Thus every tested scope applied and restored both stages, with no preserved
foreign changes in these live intervals. Initial and head-turn/return
depth/stencil references were exact, and forced rejection used native peer
rendering. These runs still replace the existing depth cohort; they do not
execute a merged in-game G-buffer or prove all pixel resources equivalent.

Run 227 (PID14444) passed 19 checks with the unchanged VS-only setting: 240
admitted frames / 49,200 camera draws, then 61 / 12,505 on cancellation, with
stage totals 240/240/240/0 and 61/61/61/0. Separate-launch medians were 70.2849
ms for the scoped dual-stage run and 69.7531 ms for VS-only; different scenes
and launches mean this is not a performance A/B or a new FPS claim.

Run 228 (PID19556) passed all nine ordinary controls with no family/binding
opt-ins. Stable HUD covered 1015 ms, 92 submissions and 74 source advances.
All owned games closed through the existing close-timeout termination path;
five-second shutdown checks found no crash/reporters. Active and ordinary
images were inspected. Installed DLL/PDB and saved settings hashes matched,
and preview/pose were restored. No game or capture replay remains running.

The workspace entered managed sandbox mode during this work. Read-only binary
analysis, Visual Studio builds (FileTracker E_ACCESSDENIED), installed Python,
game deployment/testing and the AppData settings hash used approved escalations.
No permission restriction was bypassed or relaxed.

Installed archive: `build/single-pass-20260927/deploy-native-camera-scope-226`.
DLL SHA256: `f937662a81f578e9fdc9c56a62572310763fbf03dff0e69cd8c6401ff7eab21c`.
PDB SHA256: `f12427e3480528625934b69130c3267eff1bdb2ba6f74e6a7d58290a4281e15e`.
Verification: 64 CTests, 190 single-pass Python tests and 23 launcher tests.
No commit/push. Non-camera resource proof, complete retained depth/G-buffer
arrays, shared native CPU preparation and positive total FPS remain unfinished.

## Native Target Order and Retained MRT Chain (2026-10-03)

The explicit `--pass-targets` capture now records every native geometry scope,
not just argument plane 3. A 464-byte side record ties each captured indexed
draw to its preparation scope and up to eight RTVs plus DSV. Descriptions include
dimensions, resource/view formats, flags and a COM-lifetime identity cookie.
Target metadata remains weak and does not pin the game's transient resources.
Readonly DSV descriptions are observable, while the older strict replacement
capture still rejects readonly or incompatible targets.

The all-stage flag defaults to zero. During this bounded trace, queue submission
observation and the corresponding ExecuteCommandLists call share the existing
ordering mutex. Submission records explicitly mark that serialization. The
offline analyzer tracks command-list resets, complete scope boundaries, target
identity consistency and unique submissions. It rejects partial data as a
handoff proof and distinguishes cross-queue, same-submit and separate-submit
relations. A same-submit array of lists is not a completion fence, per the
[ExecuteCommandLists contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists).
This diagnostic adds no GPU wait or replacement of additional game passes.

Two runner defects were corrected before accepting the new result:

- Run 229 (PID21628) stopped at warmup because a local `analyze` import shadowed
  the existing preparation analyzer. It was renamed; no game crash occurred.
- Run 230 (PID22048) collected usable target evidence and completed its checks,
  but the harness's planned/completed order differed for the new check. The
  run remains failed in its saved report; the plan order was fixed and rerun.

Run 231 (PID11600) then passed all 17 checks. It captured 2,820 indexed draws
in 32 native groups, with no unscoped draws or missing scopes in the captured
frame. Two depth scopes and 18 complete G-buffer-related scopes were identified.
Tail-frame scopes outside the indexed-draw capture are not mislabeled missing.
The same-frame native depth/stencil reference remained exact.

Important measured layout and ordering:

- Argument planes 4/5 bind four color targets: R10G10B10A2_UNORM,
  R10G10B10A2_UNORM, R8G8B8A8_UNORM and R16G16_FLOAT, plus the depth target.
  Other geometry stages bind the first three of that set. The fourth format is
  consistent with a motion-vector attachment; target metadata alone does not
  decode its pixel meaning.
- Both eyes use distinct target resource objects. This is not proof that their
  placed heap allocations do not alias.
- All nine MAIN G-buffer-related scopes are submitted before the captured
  VRCAM depth scope. All nine VRCAM counterparts see MAIN depth from an earlier
  submission. A completed VRCAM depth result is therefore unavailable when the
  early MAIN color passes run in the current native schedule.

Consequently, simply retaining the current partial predicted depth array cannot
enable a correct whole G-buffer pass. Complete peer depth must be produced early,
or the native producer/consumer schedule must be changed with resource/alias
dependencies preserved. The four-target layout must also survive the following
three-target subpasses; a three-MRT-only design would omit an attachment.

The hardware fixture now has a retained-chain variant using the production
StereoTargetArray allocator and original-root camera conversion. It performs a
depth-only prepass, a four-MRT color pass with depth EQUAL/no depth writes, then a
three-MRT pass into the same arrays. There are no copies or target reallocations
between these stages. Formats match the observed native four-target layout.
The fourth attachment remains intact when the final pass does not bind it.
Both eyes' four color planes, depth and stencil match separate native eye
renders byte-for-byte. A MAIN-only PS camera negative still produces the
expected peer discrepancy without changing geometry. The earlier two-MRT
fixture remains enabled. This is a GPU chain proof, not live G-buffer removal.

Run 232 (PID23012) passed the nine ordinary controls without observation flags.
Stable HUD covered 1047 ms, 94 submissions and 74 source advances. All owned
games closed via the existing close-timeout termination path, and five-second
shutdown checks found no crash/reporters. Captured/ordinary images were inspected;
all settings and installed binary hashes matched. Preview/pose were restored,
and no game, replay or standalone GPU test remains running.

Installed archive: `build/single-pass-20260927/deploy-pass-target-order-229`.
DLL SHA256: `9d13dd3ec0ad7ea612d66954ead923523ed7ca2057679ff50022e8bcb4e44aae`.
PDB SHA256: `40ab52b4e20ac740d408fbe0dbc56721287696b329f8115bf2c8404cb6dcb52f`.
Evidence: `pass-target-order-231/preparation.json` and its pass-target/native-draw
binary sidecars. Local checks: 65 CTests, 198 single-pass Python tests and 23
launcher tests. No commit/push, no new FPS claim. The full goal remains open.

## Skinning Read Contracts and Multiple Resource Proofs (2026-10-03)

SRV metadata now includes immutable resource dimensions, format, mip count,
sample layout, flags, heap properties when available, and buffer GPU address.
It is collected while the native CreateSRV argument is live, using the unused
tail of the existing 160-byte descriptor record. No texture is retained by
this metadata. Null/default views remain distinguishable, and a resource
description is not a content or lifetime proof.

Run 233 (PID19068) passed all 17 checks and established the missing resource
facts for the five draws of depth asset 35 in each eye:

- VS `906dcb2a6f034360c3b1d00067dbf12b9f6ee79a9789e73fae258688dee00d0f`
  reads a 60 MiB raw bone buffer at t10. Its actual heap type is UPLOAD, not
  DEFAULT; this permits a small CPU-accessible snapshot path once ownership
  and capture timing are implemented, rather than copying the whole buffer.
- The five t77 deformation resources are single-mip R8_UNORM DEFAULT textures,
  sized 256x32, 256x11, 256x6, 256x12 and 256x2. Together they contain 16,128
  texel bytes. Matching descriptors alone do not prove matching texels.
- The saved shader loads bone addresses `base + index * stride + {0,16,32}`.
  Its BLENDINDICES are R8G8B8A8_UINT; the maximum index is 255. The observed
  stride is 48, making a conservative palette span 12,288 bytes per instance.

`SkinningReadPlan` recognizes that exact VS hash and all consumed native
input-layout elements, including both index/weight sets, EXTRA_DATA, three
instance matrix rows and the skinning uint4 at offset 48 in slot 7. Changed,
missing, duplicated, implicit-offset or differently stepped inputs are rejected.
The family registry publishes the resulting contract under its existing seqlock.
This is an input contract, not a new rendering admission rule.

The range planner takes complete 64-byte instance records, validates raw SRV
alignment and resource/view bounds, rejects uint32 shader-address wraparound,
and merges overlapping palette spans. The raw view's start requires 16-byte
alignment, independently of DWORD-addressed loads inside the view. The first
new WARP test used an invalid eight-byte raw-view offset and removed that test
device; changing the fixture to a valid 16-byte offset fixed it. This was not
a game crash. See the [DirectX view specification](https://microsoft.github.io/DirectX-Specs/d3d/D3D12RevisedCreateViews.html).

The existing `DepthCommitGate` now accepts multiple immutable buffer snapshots,
with optional row byte count, pitch and height for texture-copy footprints.
It compares every declared byte, masks a partial final DWORD, and ignores
uninitialized row padding. Every proof is validated before GPU work is recorded;
one mismatch in any resource keeps the native fallback selected. Proof count,
individual/aggregate bytes, dispatch limits, pitch alignment and final DWORD
bounds are checked. The old single contiguous instance-buffer API is preserved.
This does not authorize passing mutable live resources as immutable snapshots.

The GPU fixture exercises 20 cases, including multiple proofs with a mismatch
in the first/last resource, row widths 1/3/4/5/257, different padding, last-byte
changes, malformed layouts, frozen admission and complementary predicated copies.
All 66 CTests, 204 single-pass Python tests and 23 launcher tests passed.

Run 234 (PID21432), final DLL, passed all 21 checks. The live registry recognized
one exact skinning input contract. `analyze_skinning_reads.py` correlates it with
complete native instance records and typed SRV metadata; it found five draws
per eye and one merged bone span of 30,912 bytes in each eye in this capture.
Run 233's allocations instead needed two spans totaling 32,192 bytes. Thus the
ranges must be rebuilt from current instances, not hard-coded from a capture.
These are prospective validation footprints, not bytes already eliminated from
the game's work or measured CPU/GPU savings.

The existing merged depth cohort completed 240/240 admitted frames with 48,961
native-camera draws, and 61/61 admitted cancellation frames with 12,444 draws.
VS/PS applied/restored masks were 720/720 and 183/183, with zero foreign changes.
Initial, head-turned and returned depth/stencil references remained exact;
forced rejection selected native peer rendering. Active/turned images were
inspected. Preview and pose were restored, the owned game was terminated after
its close timeout, and the five-second shutdown check found no crash/reporter.

The new skinning family is NOT merged yet. Its texture contents and mutable
non-camera constants still need an owned same-frame proof. Remaining alpha-test
and unregistered depth shaders, complete early peer depth, live retained G-buffer
passes, once-only native CPU preparation and positive total FPS also remain open.
No ordinary-game FPS gain is claimed; no commit/push was made.

Installed archive: `build/single-pass-20260927/deploy-skinning-read-contract-234`.
DLL SHA256: `ffb040b0714802c5192479b0f270797af661ade45541627cb4e2b57b9a3eac88`.
PDB SHA256: `369cebb04c2a22159cdacccfdc746d59f5046161852e91f9a5acd602a5447ebd`.
Evidence: `skinning-read-contract-234/skinning-reads.json`, `families.json`,
descriptor/native-draw binary sidecars and `summary.json`.

Ordinary control 235 (PID3288) passed all nine checks without family/binding
opt-ins. Stable HUD covered 1062 ms, 95 submissions and 78 source advances.
Its stereo image was inspected. The game was closed through the close-timeout
termination path; no late crash/reporter was found. Installed DLL/PDB and every
saved settings hash matched the deployment manifest; calibration stayed absent.
Preview/pose were restored. No game, replay, crash reporter or GPU test remains
running. The full single-pass goal remains active.

### 2026-10-03: native skinning reference and snapshot submission guards

The skinning contract now has live draw admission and owned resource capture.
Fixed upload b5/b7 reads compare only the shader-consumed words; zero cull count
is required before treating b8 as unreachable. Descriptor stamps and the actual
64-byte instance records are checked again at submission. Each eye gets its own
snapshot-copy predicate, finalized before submission and never modified while
that eye's commands are in flight. Rejected or changed resources select native
fallback. All texture leases are rechecked, including MAIN sources whose draw
does not subsequently match in PEER.

The 64-byte skinning batch takes native draw-origin path 0x1F020C, not the
48-byte path 0x1F1FA8. The original guard therefore rejected run 237. The new
decoder fingerprints the distinct caller/gather path and checks the packet
flags and actual batch count without weakening the 48-byte contract.

Run 239 (PID20908), reference-only, passed after that fix:

- Five skinning candidates, five emitted, five matched, no input/copy rejection.
- Submission binding checks: five MAIN, five PEER, zero rejected.
- 209 total matched depth pairs; native depth contained 225 MAIN/226 PEER draws.
- Full 1485x1485 depth and stencil were bit-identical for both eyes, with no
  invalid depth. No native draw was skipped in this reference-only run.
- Stable HUD: 1079 ms, 98 submissions, 42 source advances. Game/Codex focus
  transitions occurred at +5.001 s/+1.111 s. The stereo image was inspected.
- The owned game closed by timeout termination; the shutdown observation found
  no crash/reporter, no remaining process, and restored the preview.

Seventy-five CTests, 213 Python single-pass tests and 23 launcher tests passed
before run 239. The two new GPU marker tests invalidate a texture receipt before
MAIN or PEER submission, prove the corresponding copies were not executed,
check exact native fallback, and recover on the next generation. The PEER test
holds submitted MAIN work behind a fence to check in-flight permit isolation.

Fixed R8 texture handling remains under review before optimized live testing:
native initial COPY upload provenance, submission order and replay must be
proved, not inferred from a resource class. The snapshot predicates alone do
not establish that provenance. COPY command-list observation was added; the
next source revision is replacing the single global ExecuteCommandLists
original with a per-vtable original so all native queue types can be observed.

Installed archive for run 239: `deploy-skinning-origin-239` under
`build/single-pass-20260927`.
DLL: `fa3fb910293ef0b6a10cb8e70ac5a27faf14edbd9980edc24e173124865bf43a`.
PDB: `d23398eb5fe3afefd3c1f05c8959c6bd914e95220e7d30dbacb5230dd2b7bf11`.
Evidence: `build/single-pass-20261003/skinning-origin-239/preparation.json`
and `summary.json`, with binary sidecars. Settings hashes were unchanged at
deployment. This is correctness progress, not a positive total-FPS result.
Alpha-tested materials, complete early depth, retained live G-buffer,
once-only CPU preparation and mutable geometry content proofs remain open.

### Verified upload provenance and optimized skinning (run 240)

The fixed-texture path now requires an observed initial upload from the native
COPY context and queue, a unique command-list recording generation, and the
BeforeSubmit/Execute/Submitted sequence. Abandoned recordings, changed producers,
wrong queues, replay and later writes invalidate the receipt. Final validation
also checks the actual native DIRECT queue. Queue execution hooks now retain
an original per vtable; COPY Reset/Close avoids graphics-only bookkeeping.
Missing observer installation is fail-closed in the following source revision.

Run 240 (PID13140) passed all 21 checks with FG disabled:

- 240/240 optimized frames admitted, no fallback/failure; 50,162 paired draws.
- 1,200 skinning candidates emitted and matched. All 1,200 MAIN and 1,200 PEER
  submission checks passed. The GPU compared 2,400 skinning snapshot copies.
- Snapshot payload totals were 14,745,600 bone bytes and 3,870,720 texture bytes.
- The cancellation interval completed 61/61 admitted frames and restored state.
- Reference depth/stencil remained exact after turning and returning the head;
  forced rejection selected GPU decision zero and native fallback.
- Active and head-turned stereo images were inspected; shutdown found no crash
  or reporter and restored preview/pose. The owned process was closed.

This remains a validation-heavy prototype: median paired-depth-entry cadence
was 74.076 ms, not an FPS improvement. The draw reduction does not prove
once-only CPU scene preparation or elimination of all second-eye scene work.

NativeMaterialBinding now observes the separate PS b4 binder at 0x1F3790 and
retains its source during the native call. At an actual draw it revalidates the
global record, CPU shadow and GPU descriptor. Run 240 resolved all 20 observed
alpha-depth b4 bindings as registered sources. No b4 GPU payload was captured
and no alpha draw was merged yet. NativeMaterialRead supplies a guarded 292-byte
READBACK copy, but it is not connected to the live capture yet.

GeometryWriteReceipt adds CPU-tested range/alias and queue-order tracking with
no owned GPU objects. Distinct lists inside one ExecuteCommandLists are treated
as incomparable; ordered cases need separate submissions or fence evidence.
Its live writer hooks and admission integration remain pending. The distinction
follows the [ExecuteCommandLists contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists).

All 78 CTests, 213 Python single-pass tests and 23 launcher tests passed.
Run 240 DLL: `384cef72a51e8e3a333f8ef0f60b2f25cf0c5b1201cdfeb60ddceecdb57ce0a7`.
Evidence: `build/single-pass-20261003/skinning-upload-240/summary.json`.

Final revision 241 also rejects texture admission after any required queue/list
hook installation failure. Successful command-list Close retires material
observations so closed, abandoned lists cannot exhaust their bounded storage.
The D3D12CreateDevice hook installs device observation before returning the
new device to the game; no throwaway COPY bootstrap objects were necessary.

Run 241 (PID25864) passed all 21 checks again: 240/240 admitted frames,
50,160 paired draws, 1,200/1,200/1,200 skinning candidates/emitted/matched,
zero skinning or submission-check rejects. The cancellation interval completed
61 frames. All 20 alpha b4 sources still resolved after Close cleanup. Turned
and returned references were exact; forced fallback returned GPU decision zero.
Active stereo output was inspected. Median paired-depth cadence was 73.5755 ms;
this remains a slow experimental path, not a demonstrated total-FPS gain.

Ordinary control 242 (PID1768), without family/binding opt-ins, passed 9/9 checks.
Stable HUD covered 1062 ms, 96 submissions and 82 source advances. Its stereo
image was inspected. Both owned runs closed after the close timeout and found
no late crash/reporter. Final installed DLL/PDB and every saved settings hash
matched deployment 241, including absent calibration. Preview was restored;
no game, replay, crash reporter or GPU test process remained active.

Final archive: `build/single-pass-20260927/deploy-skinning-verified-241`.
DLL: `67162c76ae7d9c4040109ba9ab63d5e02d094001cdcbfb9dffcbf095f425f84e`.
PDB: `fa33ae5753e270dbba30d69cbdac161fd8699662bedfe87e8ef922db0e8cdbfe`.
All 78 CTests pass on the final source. No commit/push was made.

Next implementation: reuse the reference-mode NativeEarlyDepth Work and its
completion fences for bounded 292-byte b4 readbacks. Submission validation must
not reuse render-worker TLS or the current material CPU slot, which may already
belong to another draw. Capture fresh b4 and a hinted bindless texture together
on a subsequent draw, and accept their association only if that fresh GPU b4
confirms the index. Do not resolve a later descriptor as historical evidence.
The b4 observer cannot simply be gated to Drawing(): the native material binder
can inherit an earlier binding when its token is <=1. A separate research opt-in
can remove it entirely from skinning-only runs. Full single-pass remains active.

### 2026-10-04: alpha material capture and descriptor-index correction

Reference-mode Work now records per-draw 292-byte PS b4 snapshots into bounded
READBACK slots. Each copy has its own initially-zero permit and retained source.
Submission checks use fresh global native records and saved descriptors, never
the render worker's current TLS/material slot. Completed permits are permanently
disarmed before CPU publication. Replayed or duplicate original recordings reject
the capture independently of whether their material permit was enabled; both
source sides must be covered before pass readback can be mapped. Reset/reused
list objects are distinguished by current recording ownership. Rejected rows
publish diagnostics, not invented or zero-filled payloads.

NativeMaterialRead's read-only descriptor checks originally rejected private
probe scope. MatchesDescriptor and ResolveCpuCbvAddress now permit these queries
while observation is enabled; mutation callbacks still ignore private commands.
A descriptor-audit regression proves this separation.

Runs 243-245 all closed without crashes and retained exact reference depth:

- 243: all 20 b4 bindings resolved, but read-only descriptor gating prevented the
  copy. All sources were DEFAULT class-1 buffer-pool suballocations (status 9).
- Native pool construction prefers ID3D12Device4::CreateCommittedResource1,
  slot 53. Observing only base slot 27 missed creation receipts. A slot-53 hook
  now shares the committed-resource observation path.
- 244: all 20 sources gained observed creation receipts (status 8). Capture
  still rejected at the pool contract; no material bytes were published.
- 245: finer diagnostics identified ReadPool stage 1. The initial exact-292-byte
  native allocation requirement conflated logical shader usage and pool slot
  size. The audited 512-byte size class now accepts native sizes 292..512,
  still requiring exact live record identity, slot/occupancy ownership and
  physical resource bounds. The copy remains exactly 292 bytes.

NativeBufferRead tracks committed DEFAULT COMMON/GENERIC_READ creation, observed
COPY uploader recordings and submissions, per-write epochs, and exact native
pool allocation identity. Explicit transitions, relevant aliasing, unsupported
writers, abandoned/replayed uploads or incomplete observer installation prevent
copy admission. It does not claim class-1 contents are immutable. Actual DIRECT
submission must be the native queue that receives upload waits.

Writer observation also covers ResolveQueryData, WriteBufferImmediate, stream
output, AtomicCopyBuffer and CopyTiles destinations, and enhanced barriers.
Optional interface vtable slots and ABI were verified against installed SDK
10.0.26100.0. Unknown/global enhanced-barrier cases fail closed.

The paired-resource DXIL transformer now propagates ViewID dependencies into
dynamic bindless indices. The alpha PS previously paired b4 but left its dependent
createHandle nonUniformIndex=false. It now marks that handle true without changing
shader arithmetic or bank ordering. All 137 saved graphics shaders passed DXC
assembly/validation in `build/single-pass-20261004/nonuniform-resource-pairs-v2`;
224 Python tests passed. No newly generated package was installed yet.

The standalone material_readback_gpu test additionally validates real WARP
predicated copies, delayed completion, invalid metadata, late rejection and
replay after sealing with changed source bytes. It passed with the D3D12 debug
layer. This supplements, not replaces, native-game source-state validation.
Alpha draws remain unmerged until their current GPU payload and texture contract
are established. No total-FPS improvement is claimed.

Run 246 confirms the first real material payloads: four family-39 rows (two per
eye) sealed successfully, with exact matching per-eye SHA256. Both used bindless
index 29652 in this run, cutoff bits 1052938076 and UV scale bits 1065353216.
The two material payload hashes differ from each other but each matches its peer.
These are current-run values only, not constants to force into later frames.
Sixteen family-43 rows in the other shared pool still rejected because their
observed upload history was invalid before capture. All depth references stayed
exact and the game closed without crash. The next diagnostic preserves the first
invalidation event instead of conflating every invalid history with one status.

Archive 246 DLL: `9c05383ed7396df353677511f8dc2a012c40577a4b1ad0f8b9088137e53a4a08`.
PDB: `127e7c56b66731676d620924423f6869d82f6acacef224b20c6a9ac5aa02efcf`.
Evidence: `build/single-pass-20261004/alpha-range-246/preparation.json` and its
materialrows/materialblob binary sidecars. The launch suite intentionally failed
the complete-material-coverage requirement; partial capture is not a full pass.

Run 247 (PID25764) captured all 20 b4 payloads (5,840 bytes), passed the full
reference launch suite and retained exact depth. PS b7 was read and revalidated
for all rows: alpha gate 1.0, dither multiplier 1.0, bias zero. b0 was not available
through the fixed-upload reader, so controlMask is 1, not 3; its published zero
selector is unavailable data, not a proven selector value. Nonzero alpha cutoffs
mean these actual materials cannot use a blanket no-discard PS specialization.

Run 248 (PID11184) reproduced the static-pool rejection with exact first cause:
WriteHistory saw recording 40286 while recording 40285 was still unsubmitted
(history bits 1). The uploader legitimately records multiple COPY lists for a
shared pool before submission; the single-producer history model rejected this.
This explains why all 20 captures passed in some startup schedules but only four
passed in others. The next revision tracks a bounded cohort of producers and
requires all uploads submitted on the audited native queue before capture.
No state/barrier guard is bypassed. Reference depth remained exact; no crash.

Typed Texture2DArray/sampler resolution and root-layout binding lookup now have
tests. The same-launch second-capture path snapshots fresh descriptor metadata
using first-capture indices only as hints, and confirms association against the
second capture's own sealed GPU b4. It does not claim texture-content equality.
Run 248 stopped at its first failed complete-material check, so this second
capture is not yet game-verified. The local suite has 81 CTests and 225 Python
tests; the last completed full CTest run passed all 81.

### Multi-upload fix and same-draw texture association (249-251)

NativeBufferRead::History now holds up to 128 producer recordings. Distinct
verified COPY producers may be recorded before earlier ones submit; every write
advances the content epoch and the source becomes readable only after all cohort
members submit on the verified upload queue. Resetting a completed member does
not abandon another unfinished member. Unknown/duplicate/replayed submissions,
wrong queues, writes to frozen recordings and overflow still reject admission.
All six submission orders for three producers and cohort reuse have CPU tests.

Run 249 (PID28820) passed both captures in one owned launch:

- First capture: 20 sealed material rows, 5,840 bytes.
- Second capture: another 20 sealed rows, all 20 descriptor associations
  confirmed against their own newly read GPU index. Seven distinct material
  handles used six distinct alpha textures; each material matched its peer's
  full b4 payload hash. Initial and second reference depth stayed exact.
- Alpha views are DEFAULT, flags 0, BC7_UNORM_SRGB (99) or BC1_UNORM_SRGB (72).
  Observed dimensions/mip counts: 512x256/10, 1024x1024/11, 2048x128/12,
  128x128/8, and two more 1024x1024/11 textures. These are not top-mip-only reads.
- Dither is the 64-layer R16G16B16A16_UNORM view with default component mapping;
  sampler is the native anisotropic filter 85, wrap XYZ, anisotropy 16 and
  negative LOD bias. Metadata and sampler identity are proved, texture content
  equality across the MAIN/PEER interval is not yet proved.
- The stereo image was inspected. Controls/preview restored; no crash/reporter.

The same final DLL passed regression run 250 (PID21680): all 21 checks,
240/240 admitted optimized frames, 50,160 paired draws and 1,200 successful
skinning draws per side. Cancellation completed 61 frames; head-turn/return
reference depth/stencil remained exact and forced fallback worked. Its active
stereo image was inspected. Median paired-depth-entry cadence was 77.0398 ms,
so this remains a slow prototype, not an FPS gain.

Ordinary control 251 (PID3544) passed with diagnostics disabled. Stable HUD:
1047 ms, 95 submissions and 72 source advances. Image inspected; game closed,
no late crash/reporter. Installed DLL/PDB and all saved settings hashes match
the deployment manifest; calibration remains absent. No game/replay/GPU test
process remained. Final local checks: 81 CTests, 225 single-pass Python tests,
23 launcher tests, and 137/137 offline paired-resource DXC validations.

Installed archive: `build/single-pass-20260927/deploy-alpha-multi-upload-249`.
DLL: `27b3750fd142c380749cc9a3da1352d8d955de3b9a95a2b54eb9996aa9f674dc`.
PDB: `0af54d9a3588d6fabf4cac68824d134498e492ac21cb5333c01c97138ef72857`.
Evidence: `build/single-pass-20261004/alpha-multi-upload-249/material-textures.json`,
its binary sidecars, and run 250/251 summary files. No commit or push performed.

Next actual renderer change: explicit reference-only family-43 admission with
the existing native-camera VS and unchanged native PS, original native bindings,
paired depth target and nonzero paired/covered counters. Do not enable mode 2
for this family until b4, all sampled alpha mips, sampler/descriptor stability,
geometry and instance coverage have the required same-frame proof. Dither may
use the narrowly proved scale=1/bias=0 UNORM no-discard case; unavailable b0 is
not implicitly zero. Family 39 needs its own exact skinning contract as well.
Complete early depth, retained live G-buffer, once-only CPU preparation and
positive total FPS remain unfinished; the full single-pass goal is active.

### First PS-preserving alpha reference rendering (252-253)

AlphaDepthContract recognizes only VS
`7fa0e076ff234929afda3679349d06353d4936245785b547b339432e3b1fd9ae`
and PS `dc69ea3ee2d8d906ebcbba4bd8f8d750add2b80946b5de9c9d94360cb18339e5`
with the five exact consumed POSITION, TEXCOORD0 and instance-transform inputs.
Unconsumed layout elements are allowed, missing/duplicate/changed consumed
elements are not. The family exports the contract without changing record ABI.

An explicit AlphaReference control admits this static family only in whole-target
reference mode 1, with its original root, native-camera VS and unchanged native
PS. Both NativeEarlyDepth and EarlyDepthPass reject enabling it in mode 2.
Original native MAIN/PEER draws are still executed; alpha content is genuinely
drawn into the paired candidate, not inferred from a fallback-only pass. Item
matching also checks alpha material identity, modifiers, dither and sampler.
Submission metadata/control changes prevent a successful comparison report.
The exact neutral dither controls are guarded; unproven b0 is not treated as zero.

The new GPU fixture renders an actual texture-discard PS and compares depth and
stencil, including kept/discarded pixels against an independent opaque control.
Negative tests exercise wrong flag, camera mode, family contract, stage mask,
opaque PS and non-reference modes. All 84 CTests and 227 Python tests passed.

Run 252 crashed during startup before world readiness or AlphaReference enable.
The owned launcher closed CrashReporter. Dump:
`Cyberpunk2077-20261004-121341-7600-23048`.
EXE RVA E6ABAD read a null entry while iterating GameInstance::systemInstances
(array +0x38/count +0x44) and preparing IUpdatableSystem::OnRegisterUpdates
(virtual slot +0x108). The same E6ABAD/null-RCX signature exists in September
22, 24 and 26 dumps. No direct connection to the DLL-owned family flag was found;
this is evidence for a pre-existing startup failure, not proof that all possible
timing/corruption contributors are excluded. Startup log saved in run 252 folder.

Unchanged-build repeat 253 (PID17464) passed all 18 planned checks:

- Initial view: 8 alpha candidates emitted and matched, zero alpha rejections;
  total paired depth cohort increased from 209 to 217 draws.
- Turned view: 6/6 alpha draws; returned view: 8/8. Reference depth/stencil and
  native round-trip outputs remained bit-identical in both eyes in all views.
- Forced optimized fallback kept AlphaReference disabled (all six counters zero)
  and used the existing 209-draw cohort. No optimized alpha admission occurred.
- The head-turned image was inspected; controls, pose and preview restored.
  Owned game closed after timeout; no new crash or reporter in this repeat.

Archive: `build/single-pass-20260927/deploy-alpha-reference-252`.
DLL: `9bf5c83d0472981eecd9a8ce402c675f2ffd15c18dc293d77fa7b372e6efe18e`.
PDB: `ca5d88fdc2ed0126a1625bfe02cc580c709a0c73b7d3233a6fd3d02a1e03039c`.
Evidence: `build/single-pass-20261004/alpha-reference-repeat-253/preparation.json`
and `depth-variant-turned.json`, `depth-variant-returned.json`, `summary.json`.
This establishes shared alpha rendering correctness in these sampled references,
not stable texture contents or performance of an optimized alpha path. Family39,
all-mip alpha lifetime/content proof, full early depth/G-buffer and once-only CPU
preparation remain open. No total-FPS gain or full single-pass completion claimed.

Ordinary control 254 (PID22556), same DLL with no diagnostic opt-ins, passed.
Stable HUD covered 1063 ms, 95 submissions and 69 source advances. Stereo image
was inspected. The process closed after timeout; no new crash/reporter. Final
DLL/PDB and all settings hashes matched archive 252, with calibration still
absent. All diagnostic controls returned to zero; no game/replay/GPU test process
remained. The 23 quickboot tests also passed. No commit/push performed.

### Optimized alpha implementation and recording ownership

The next experimental mode captures the 292-byte GPU material and every viewed
BC1/BC7 sRGB mip, compares MAIN/PEER snapshots on the GPU, and verifies the
hinted bindless index against the actual GPU material word. Texture footprints
use block rows and exclude row padding. Invalid contents, index, descriptors,
upload receipts or submission validation select the existing native fallback.
Synthetic tests cover changed material, last mip, wrong index and rejected
MAIN/PEER copy permits. These fixtures are not an in-game performance result.

Review found that arbitrary replay of an already submitted native recording is
not supported by the private snapshot/gate resource transitions. Changing an
UPLOAD permit on the CPU is not a fix while its first execution may be in flight.
EarlyDepthPass now retains distinct recording markers per eye and pass generation,
requires matching markers for submission, detects repeated submissions, and
retires a marker on successful Reset. This is detection, not GPU replay support.
The same COM list can Reset and record PEER while MAIN's resources remain retained.
This follows the documented distinction between list and allocator Reset:
https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles

The production alpha mode additionally requires the audited native context
ownership path: pool acquire resets the list, both single and bulk submissions
consume context owners, and recursive split submissions use disjoint halves.
The helper fingerprints those functions, records the exact Reset origin and
generation, and validates the native DIRECT queue and Execute call origin before
enabling snapshot permits. Foreign/manual replay is explicitly outside this
contract. Alpha reports require recording counters [1,1,1,1,0,1], not just draws.

The family-39 skinned-alpha pair is now classified separately as contract 2:
VS 02f81c75e38964877d832f7378a39251132ab93580dfe89017e65d01fc22bc4c,
the same alpha PS, all 13 consumed IA inputs and 64-byte instance stride.
Bone range calculations reuse the bounded eight-index checks. This metadata
does not yet enable family 39 in live alpha rendering; combined skin/alpha
admission still needs integration and live verification.

Current local checkpoint: 92 CTests passed, including same-list MAIN/PEER reuse
and replay detection; 232 Python tests and 23 quickboot tests passed. The native
recording helper/integration still needs its final build and live run. Installed
DLL remains archive 252 at this checkpoint. No positive total-FPS gain claimed.

### First live optimized alpha admission (255-258)

Run 255 reached a stable world/HUD but stopped in the family census because the
Python decoder still accepted only skinning contract 1. The decoder now accepts
the separately audited contract 2, exports alpha contracts, rejects incompatible
combinations and has regression tests. This was not a game crash.

Runs 256 and 257 recorded 217 paired draws with eight alpha candidates and 78
sampled mip snapshots, but correctly refused commit because native recording
submission provenance failed. The diagnostic repeat established matching native
DIRECT queues and valid Reset origins for both eyes; only the immediate caller
was outside the game module, in an intermediate graphics call. The fix uses
CaptureStackBackTrace, not raw stack scanning, only for captured fresh submissions:
the nearest unwound game frame must be the exact fingerprinted Execute return.
Wrong nearest frames are rejected even if the expected address appears deeper.
https://learn.microsoft.com/en-us/windows/win32/debug/capturestackbacktrace

Run 258 (PID20936) passed all 17 planned checks on the resulting DLL:

- Fresh same-process material and texture-index captures completed before tests.
- Optimized sample: 218 MAIN/PEER draw pairs, including 8/8/8 alpha candidates,
  emitted and matched. No alpha input/submission failures.
- Alpha proofs: 8 MAIN captures, 8 PEER material copies, 78 PEER mip copies,
  2336 logical material bytes and 3702928 logical texture bytes, no failure.
- Recording receipts were exactly [1,1,1,1,0,1]. GPU decision 1 and CPU admission
  1 selected the optimized path; no native reference draws were added.
- The same-launch forced rejection sample retained all the input proofs and
  switched GPU decision and CPU admission to 0, exercising native fallback.
- Both sampled stereo images were inspected. Controls returned to zero and the
  owned process was closed; no crash or reporter was found. Preview restored.

The depth-group timestamps include private copies and validation. They are not
an overall FPS comparison. This is one-shot static alpha replacement, not
continuous alpha mode, full-scene single pass, shared CPU preparation or proven
total speedup. Family 39 remains classification-only pending combined admission.

Archive: `build/single-pass-20260927/deploy-alpha-native-origin-258`.
DLL: `b4ff36314ae15d8ec016825ec996cbe1f15754e6bf2c11d4ccf55389040b45cb`.
PDB: `1eef930a8949f30c736ead8b38a12ff990a820a8e26092bad5411c912bba3440`.
Evidence: `build/single-pass-20261004/alpha-native-origin-258/alpha-optimized.json`,
`alpha-native-fallback.json`, `summary.json` and the corresponding BMP images.
All 93 CTests and 234 Python tests passed. The same-list alpha test now covers
three pass generations with fresh allocators and retained old resource batches
until actual list Reset. No commit/push performed.

Ordinary control 259 (PID21696), with diagnostic opt-ins disabled, passed all
nine planned checks. HUD readiness covered 1078 ms, 96 submissions and 75 source
advances; the stereo image was inspected. Game closed, no crash/reporter, preview
restored. Installed DLL/PDB and all five settings/absence checks matched archive
258. No game, RenderDoc/replay or standalone GPU-test process remained.

Independent CPU review of run 256 found 234 MAIN and 235 peer mode-0 depth
packets, with only 37 identical packets and 66 shared first-word sort keys.
Whole-list or MAIN-visibility reuse is therefore not justified. The current
eager cache sorts peer packets early and still runs MAIN preparation: this
reschedules work rather than eliminating both sorts. A shared native-key
ordering/permutation is a possible next bounded experiment, retaining both
live resource-dependent filters and eye-owned outputs. Equal-key ordering is
not yet proved (one duplicate peer key exists), so ambiguous ties must fall
back. Any implementation must measure actual CPU cost versus caching/merging
overhead; this limited sort reuse is not equivalent to once-only scene traversal,
visibility or full geometry preparation. No CPU change was made from this review.

### Combined skinning and alpha replacement (260-261)

The explicit SkinnedAlpha control now composes contract-2 VS skinning and the
audited alpha PS. It requires Skinning plus exactly one reference/optimized alpha
mode, and is still one-shot only. VS b7 and PS b7 remain independent descriptor
and content checks. Exact family-39 IR confirms the existing six b5 words and
three VS b7 words cover all relevant VS constants under ZeroCull; UV.xy passes
directly to this PS, and its normal/tangent outputs are unused by the alpha PS.
Submission validation uses the same consumed b5 subset for skinned inputs.

Optimized contract 2 requires CaptureSkinning(sequence), CaptureAlpha(sequence)
and Main(sequence) in that order. Both one-use tags are consumed on admission.
Missing, stale or failed captures cannot borrow the previous draw's evidence;
native fallback clears the abandoned skin tag and submission rejects pending
unconsumed tags. The shared gate allows at most 256 resource proofs, with the
existing 8 MiB snapshot limit per eye and per-proof/aggregate byte checks intact.
New GPU fixtures exercise combined proofs, each content mutation, missing and
stale tags, failed retries and capacity. These use the simple fixture VS, not
the native skinning shader; native shader correctness is checked in game below.

Reference run 260 (PID25472) passed 18/18 checks:

- Initial/returned view: 219 paired draws; alpha 10/10/10, including skinned
  alpha 2/2/2; skinning 7/7/7. No input or submission errors.
- Turned view: 206 paired draws; alpha 8/8/8, skinned alpha still 2/2/2.
- Whole-target depth/stencil and native round trip were bit-identical for both
  eyes in all three reference views. Turned stereo image inspected.
- Forced fallback correctly disabled both alpha modes and the skinned-alpha
  option, restoring the existing 209-draw cohort with GPU decision 0.

Optimized run 261 (PID21900) passed 17/17 checks:

- 219 paired/matched draws; alpha 10/10/10, skinned alpha 2/2/2 and skinning
  7/7/7, with no input or submission failures and no native reference draws.
- Alpha snapshots covered 10 materials (2920 bytes) and 102 mip copies
  (4402352 logical bytes); combined shader snapshots covered 126 source copies.
- Exact native recording receipts [1,1,1,1,0,1]; CPU/GPU admission both 1.
  The second sample kept the same proofs and forced both decisions to 0.
- Stereo image inspected; both owned runs closed without a crash/reporter,
  restored preview dimensions and returned all diagnostic controls to zero.

Archive: `build/single-pass-20260927/deploy-skinned-alpha-260`.
DLL: `d60bb9a034853e3f80aefb09054ca2843224a4e19416185544f79bd775206c15`.
PDB: `2569b7a8929ed61f1cfcddcbc7579be5f36c52cbe170443aefec80bbb911ceae`.
Evidence: `build/single-pass-20261004/skinned-alpha-reference-260` and
`build/single-pass-20261004/skinned-alpha-optimized-261` JSON/BMP reports.
All 106 CTests, 241 Python tests and 23 quickboot tests passed. No commit/push.
No total-FPS gain, continuous alpha operation or complete single pass claimed.

Coverage review of 260 identifies six remaining MAIN fallback draws in three
PSOs, with complete native draw/instance data: two draws totaling 45375 indices
with vertex stride 24/instance stride 64; one draw with 16842 indices and vertex
stride 16/instance stride 64; three draws totaling 9852 indices with extra slot 4
and instance stride 48. Their exact shader identities are absent from the earlier
family census. Pointer reuse was also observed elsewhere in that census, so
shader identities must be captured with the actual draw/PSO lifetime before
admitting another contract. Separate peer-only visibility draws remain valid.

Independent retained G-buffer mapping found stage 6 is first in submission
order: three 1485x1485 MRTs (formats 24,24,28) and depth 19/DSV20; stages 4/5
add format34. Family61 concentrates 104 MAIN/106 PEER draws in the first scope,
but b4(212 bytes), b7/b12, raw/structured buffers and two bindless texture tables
remain unproved. The synthetic retained-chain test is not live integration.
Next target-lifetime work needs frame-owned depth/four-MRT arrays, per-layer
barriers, preserved native clears/PSO state, and export before unmodified native
consumers. Binding/submission reports alone do not establish consumer barriers.

Ordinary control 262 (PID2160) passed all nine checks with experimental rendering
disabled. HUD readiness covered 1094 ms, 98 submissions and 79 source advances.
Stereo image inspected; the game closed without a crash/reporter. Preview and
all settings/absence hashes matched archive 260, as did the installed DLL/PDB.
No game, RenderDoc/replay or standalone GPU-test process remained.

### Retained target ownership infrastructure

After the 260-262 milestone, the two delegated target-API tasks stopped with a
service usage-limit error after writing the APIs but before tests. Root verified
the partial files, completed them and added the GPU tests. No further agent
writer remained active and no live-game deployment was performed for this work.

StereoTargetArray now has explicit per-eye TransitionEye, ImportEyeLayer and
CopyEyeLayer operations. Array barriers address only `eye + plane * 2`; the
other eye's state/content is untouched, and depth and stencil are both copied.
The new methods check DIRECT list/device/native shape before recording. Existing
whole-resource import/export methods keep their old behavior. Mixed read/write
eye states are tested over formats 24/28/34 and depth/stencil, padded dimensions
and repeated cycles with a clean D3D12 debug layer.

CaptureGeometryTargets is a separate strong capture API for 0/3/4 MRTs plus
depth in slot 4. It preserves exact typed view formats and read-only DSV flags,
resolves contiguous RTV handles with checked arithmetic, checks dimensions,
sample/mip counts, device identity and COM resource cookies before/after AddRef,
and rejects duplicate attachments, stale cookies, unsupported shapes and formats.
It must still be called at an actual native draw while native ownership is live;
the weak descriptor registry does not independently make a stale pointer safe.
Legacy CaptureTargets remains unchanged.

StereoGeometryChain now owns frame-local depth/four-MRT two-layer images and
single-layer/both-layer views. PrepareEye imports each attachment once as it is
first needed; depth -> 3 MRT -> 4 MRT -> 3 MRT retains prior private contents.
Bind accepts single-eye or both-eye masks, without changing shaders, root tables,
viewports or view-instancing masks. ExportEye copies every attached image back
once, seals that eye in private COPY_SOURCE state, and leaves the opposite eye
available for writable rendering. CommandResources retains the chain, all native
images and views through recorded and submitted command lifetimes.

The chain is an ownership component, NOT live rendering admission. Its caller
must prove native RT/DEPTH_WRITE states, same-eye command ordering, no intervening
unredirected clears/writers/aliases/readers, and export before native consumers.
Read-only native DSV input is explicitly rejected by this first writable chain,
although the capture API preserves those flags. No ordinary game hook invokes
the chain yet; cross-command native consumer/barrier integration is still open.

New geometry_retained_targets tests run on WARP and hardware, with separate
ordered native-style depth/3-MRT/4-MRT/3-MRT lists, GREATER_EQUAL/depth-write and
stencil REPLACE. Each eye imports/exports exactly five attachments and performs
four retained binds. The owner is destroyed before submission; outputs remain
byte-exact against independent native 2D reference targets and include nonempty
depth coverage. The initial fixture winding was culled; the nonblank assertion
caught it and the fixture winding was corrected without relaxing that check.

The pre-existing native_camera_retained_chain GPU test now uses this same
production ownership component for paired arrays and its both-eye binding API.
It verifies masks 3/1/2, textured VS/PS camera conversion, depth/stencil and four
native MRT formats, with no intermediate imports/exports and an unconverted-PS
negative control. This remains a synthetic fixture, not live G-buffer proof.

Local verification: 108/108 CTests, 241 Python tests and 23 quickboot tests passed.
The installed DLL remains archive 260; there was no new game launch or setting
change. Full retained live G-buffer, CPU work sharing and total FPS improvement
are unverified and remain required for the complete single-pass goal.

Resource-state reference:
https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12

### First live retained G-buffer routing (263-268)

NativeRetainedGeometry now uses StereoGeometryChain in actual game recordings.
The one-shot reference keeps every original native draw and additionally draws
into its retained eye layer. It compares all three G-buffer images plus depth
and stencil before the first consumer/Close, after GPU fences. The replacement
request in the same process uses only the verified native PSOs and routes each
original direct/eligible indirect command once into its array layer. Native
shaders, root arguments, geometry, viewport, clear contents and eye visibility
are preserved. The output is exported before unmodified native consumers.

Native targets are identified while owned at the real draw. Their writable state
is read twice from the engine's current native context, with exact registry,
resource handle, context/list/kind, pending-barrier and uniform-state checks.
Creation-time PSO metadata checks exact G-buffer formats, depth/sample/raster
state, shader stage, read-only resources, and absence of ViewID/array/viewport
index outputs. This policy is attached to each native PSO's COM lifetime,
including PSOs absent from the stereo conversion package. Existing stereo family
record ABI and shader assets are unchanged.

Several native writers share the same Execute batch and targets. During this
explicit test, a batch containing a retained owner is submitted as separate
Execute calls in the original list order, each exactly once. This supplies GPU
ordering for the full-target imports/exports without a CPU wait. It is a
diagnostic scheduling change, not a claimed performance optimization. Async
compute stays enabled. Full optimized scheduling still needs integration.

The live coordinator handles boundaries before native barriers, aliasing,
copies/resolves, clears, discard, unknown draws, dispatch, bundles, render passes,
nonzero predication and Close. Every indexed draw not delivered by the native
snapshot path flushes pending outputs before its original draw, including the
capture-capacity transition. A successful Reset abandons an unsealed old owner
without adding commands to the new recording. Native render-pass admission is
excluded by Begin/End observation. Plain OM bind/unbind does not access image
contents; live views are checked at the next draw, so a temporary unbind does
not prematurely finish retention.

The execution contract is the previously audited engine one-shot ownership
chain: exact native reset origin/context generation, consumed context owners,
and mandatory Reset on reacquisition. Foreign/manual replay of injected resource
transitions is unsupported. Submission validation reports violations; it does
not retroactively remove or guard already recorded native commands.

Run 263 reached the world but correctly stopped before private drawing because
the first stage-7 PSO was absent from the converted-family registry. The policy
was moved to source PSO metadata instead of requiring a converted family. Runs
264-267 then passed all 19 planned checks, with 51 MAIN / 52 PEER direct commands,
one retained scope per eye, and 37488825 compared bytes per eye with zero color,
depth or stencil differences. Replacement imported/exported four attachments
per eye. All these launches closed without a crash/reporter and restored controls.

Boundary diagnostics found that ExecuteIndirect ended the chain between stages
7 and 9. NativeIndirectPolicy now records immutable signature metadata in COM
private data through the existing slot-41 creation hook. It admits only a single
DRAW_INDEXED or DRAW argument, with its actual DWORD-aligned 20/16..1024-byte
stride and supported node mask. Root/VB/IB-changing, dispatch, unobserved and
malformed signatures remain excluded. Argument/count buffers and offsets are
passed unchanged, bounds are checked, and resources are retained. Indirect
epilogues outside the CPU DrawScope can use the same proven recording, live
targets and native PSO; no stage is invented for the missing scope.

Run 268 (PID17224) passed 19/19 checks:

- Reference and replacement each covered 176 MAIN / 178 PEER commands across
  two native scopes per eye, including 42 indirect API calls per eye. These are
  command counts, not an asserted count of GPU draws inside indirect buffers.
- Stage masks 640 establish stages 7 and 9. End reason 11 is an actual resource
  barrier/consumer boundary. No rejected indirect command was encountered.
- Each eye imported four attachments once across both scopes. The reference
  compared 37488825 bytes per eye, with all twelve difference counters zero.
  Replacement exported four attachments once and executed each command once.
- Two native batches, fourteen lists, were explicitly ordered on the GPU.
- Stereo replacement image was inspected; stable HUD, post-test capture and
  cleanup checks passed. The process closed without a crash/reporter, preview
  was restored, and all experimental controls returned to zero.

Archive: `build/single-pass-20260927/deploy-retained-pure-indirect-268`.
DLL: `7d5a95aa4471f0a8983dc6c86d2fb9544a143ecefb33292214cef4f2875bf9ad`.
PDB: `d92c3b7c932696c99d63e3f1fa8f8b655f12a50667c2dee0b989d3cfd7c0a5b8`.
Evidence: `build/single-pass-20261004/retained-pure-indirect-268/preparation.json`,
`retained-replace.json`, `summary.json`, target/draw binaries and BMPs.

All 109 CTests and 252 Python tests passed; the retained GPU fixture now also
uses indexed/non-indexed indirect commands with padded strides. No commit/push.
This proves actual retained native G-buffer outputs across these two sampled
passes. Both eyes still prepare and draw their native command streams; this is
not yet paired G-buffer drawing, once-only CPU preparation, a permanent mode or
a measured total-FPS gain. Those remain required by the full single-pass goal.

Ordinary control 269 stopped at the launcher because Windows rejected a
foreground-window request during Continue input. Focus-at-5s/Codex-at-1s had
already been verified; no game crash/reporter occurred, and the owned process
closed with preview restored. No renderer fix was inferred from that failure.
Unchanged-build repeat 270 (PID1076) passed all nine checks. HUD readiness covered
1110 ms, 99 submissions and 82 source advances; its stereo image was inspected.
The game closed without a crash/reporter, all preview/settings/absence hashes
matched archive 268, as did the installed DLL/PDB. No game, capture/replay or GPU
test process remained. No commit/push was made.

## Creation Sources And Delayed Paired G-buffer Reference (2026-10-04)

NativeShaderSnapshot now retains exact creation-time VS/PS/root bytes and input
layout through immutable COM-private source identities, not a raw pointer map.
Run 271 (PID1744) exported 14 native PSOs from the retained stages and passed
19/19 checks with the same 176/178 command counts, two scopes and 42 indirect
API calls per eye, and byte-exact retained results. Ordinary run 272 (PID1848)
passed 9/9, with 1078 ms/97 fresh HUD submissions; no crash/reporter occurred.
Both processes closed and restored the preview. DLL/PDB matched archive
`deploy-native-shader-sources-271`.

The new batch consumes only these strong source snapshots: 14/14 pairs and
23/23 unique shaders passed CPU conversion, DXIL validation and VS/PS signature
normalization. Seven pairs are VS-camera-only, seven use both VS and PS camera.
Warm execution verified 23+23 stage cache hits and rehashed all 28 final shader
references. See `single-pass-native-shaders-20261004.md`. Candidate metadata is
not a production family manifest, native PSO success or output equivalence.

GeometryTargetCheckpoint records single-layer private before/after targets at
the actual draw, including depth/stencil, and restores the native writable
states. IUnknown storage and command-resource leases keep images and views
alive until submission completion. The GPU fixture covers 0/3/4 MRT, WARP and
hardware, one-shot capture, deliberate native post-capture overwrites, and
caller ownership release before Execute.

The hardware `native_camera_delayed_checkpoints` fixture imports the two private
before images only after both original eye draws have completed, then performs
the shared depth -> four MRT -> three MRT chain. Native outputs are subsequently
overwritten; preserved after images still match the two array layers exactly,
including stencil. The unconverted pixel-camera negative remains detected.

NativePairedGeometry integrates this scheduling into the real native draw path,
initially for exact VSfe53e834 / PS8c5a09de, one matched six-index quad. Each
captured native draw is executed exactly once, even on a later diagnostic error.
The candidate is an additional shared draw, never native suppression/admission.
Actual completed camera uploads are retained separately for MAIN and PEER.
The validated native-camera package uses ViewID xor1 and a [MAIN,PEER] bank,
with array layers [PEER,MAIN]. MAIN must have returned from Execute; PEER uses
the same pinned native DIRECT queue and later individually ordered submissions.
Open render passes, predication and incompatible native PSOs remain excluded.

Runs 273 (PID10136) and 274 (PID14124) reached both strong checkpoints and both
native one-shot submissions (queue proof3). They stopped before the paired draw
at the camera flush preflight, with no crash/reporter and restored preview.
274 established the exact cause: selected resource group mask1/count4, segment base98604,
capacity446/cursor387, stride32, and dirty mask0x00000FF000000000. Those dirty
tables are invisible to the current graphics flush, not active camera tables.

The audited descriptor flush is EXE+0x1F22E4 (0x4DC bytes, FNV64
20371793713C127F) plus cold block1E3D3AE (0xD3, FDEFCB6798D5CC8B). Its actual
graphics stage visibility masks now determine which ordinary dirty bits can
emit. Unrelated non-emitting bits must survive both flushes unchanged; rebind
bits remain globally forbidden. Full selected CPU table spans are double-read,
with exact group/root-parameter distinction and a budget for two fresh tables.
Valid native heap caches must match the retained typed heap handles. CPU camera
slot changes alone are insufficient: the actual bound GPU CBV is verified after
replacement and again after original-camera restoration. No old GPU table is
rebound behind the engine's updated cursor/cache.

The live comparison also records original before-vs-after changed pixels. A
whole-target zero difference cannot pass validation unless the selected native
draw actually contributes G-buffer pixels in both eyes. No total-FPS improvement,
once-only CPU preparation or full-scene permanent single-pass is claimed here.
These remain requirements of the active goal. No commit/push was requested.

The CBV result observer initially ran only for legacy depth/twin controls.
It now observes our scoped 2048-byte camera descriptors regardless of which
experimental pass owns the upload. No ordinary camera descriptor is replaced.
Run276 executed its first actual paired GPU draw and proved all output bytes
equal, but the selected quad contributed no pixels. The launcher rejected that
vacuous result instead of claiming correctness from unchanged images.

Run277 (PID29120) searched ordinal0..5 automatically inside one owned launch.
The sixth MAIN candidate, sequences402/1893 with strong family owner638,
contributed828 MAIN and766 PEER pixels in each of the three MRTs and depth.
The single shared candidate draw matched all37488825 meaningful output bytes
per eye, with all twelve difference counters zero. Stencil also matched.
Both one-shot recordings were submitted on the same native DIRECT queue,
in two ordered batches; actual GPU b1 was2048bytes. All18 launch checks passed,
including post-pair HUD, repeated captures, controls reset and late-crash check.
The inspected stereo image was nonblank and the process closed without a
crash/reporter, with the preview restored.

DLL: `94391b20fc580d78867ae3cae8757a2360d961cf017abb148aa3d20a5f17c56a`.
PDB: `7871ba1473308be2784e93d9152098cf6095508f9762e98d2f600b4d065393e6`.
Archive: `deploy-paired-visible-candidate-277`.
Evidence: `paired-visible-candidate-277/paired-ordinal-5.json`,
`preparation.json`, `summary.json`, binary captures and `paired-gbuffer.bmp`.

Ordinary278 (PID32324, unchanged DLL) passed9/9; HUD readiness1110ms,
99 submissions/80 source advances. Experimental paired request/state/ordinal,
observers and family assets were zero. Image inspected; process closed with
no crash/reporter and restored preview. Screenshot FPS is not a benchmark.

The new snapshot package preserves all94 original family rows/order/blobs and
adds seven exact source pairs, giving101 families and83 native-camera families.
All342 blob references/4680949 loader-counted bytes were verified. It invents
no source PSO fixed state; the real creation callback supplies that state.
`package_snapshot_families.py` is content-addressed and has nine focused tests.
Manifest SHA256: `34a58c4bdac6a6b62420ff4567b4acef4cea8fd20165a05427501bd8ff93f5af`.

Runtime279 (PID29872, unchanged DLL) loaded this package:200 linked PSOs,
39 used families, zero creation/binding failures or overflow. All18 launch
checks passed. Native retained reference still covered176/178 calls, two scopes
and42 indirect API calls per eye, stage masks640, with zero differences over
37488825 bytes/eye. Image/controls/HUD/shutdown gates passed; no crash/reporter,
and preview restored. Offline runtimeAdmitted=false remains correct: successful
PSO creation/reference does not authorize wholesale native draw suppression.

Latest verification:115 CTests,269 Python tests,23 QuickBoot tests. New native
pair work still prepares and executes both original eyes, adds a reference
candidate and copies diagnostic snapshots. It is not the permanent optimized
mode and has not demonstrated an overall FPS improvement.

## Both-stage Camera And Whole-pass Frontier

The next audited pair is exact VS84b48dd0 / PS0cf4b32c (old asset70), not a
general permission for arbitrary G-buffer shaders. The VS reads only b1/b5;
the PS has no buffer/UAV/wave/quad operations. Texture indices are direct b4
words, and its sole camera XY read happens after all texture samples. Changing
the camera cannot select a new resource family for this exact shader pair.
Current PEER inputs are legal for the delayed reference, but MAIN/PEER contents
equality and production scheduling still require further proof.

Run280 (PID10568) selected native sequences595/2075, each28224indices/one
48-byte instance. Actual GPU camera stages3 and binding size2048 were verified.
The one paired draw matched all37488825 bytes per eye, zero differences in all
twelve counters. Native draw changes were72 MAIN and100 PEER pixels in each
MRT and depth, so this is not an occluded/blank equivalence. All18 launch gates
passed; image inspected, controls restored, no crash/reporter, preview restored.
The module still executes the two originals and one additional shared reference.

DLL: `1d9dd74d5c8cb884d830aca6cd6e40b39c9a3d17c9ef5115f7d27cfca2f568f2`.
PDB: `0108cfcca7d606822a985d833d91bda7b2caca39f5d23912768381a8a67cc983`.
Archive: `deploy-paired-radial-family-280`.
Evidence: `paired-radial-family-280/paired-ordinal-0.json`, raw indexed draw
records, `preparation.json`, `summary.json` and `paired-gbuffer.bmp`.

The saved277 graph frontier audit establishes an actual dependency cycle if
MAIN G-buffer simply waits for PEER. MAIN Lighting45, TXAA53 and Final2D57 lead
to PEER88..108; adding108->MAIN26 without changing that path cycles. PEER stage6
recordings occur in batch51, after MAIN stage7/9 batch29. EagerDepth only moves
stage3 sorting and cannot generate this stage6 producer.

The topology-only candidate cuts25->26,57->88,108->109 and adds25->88,108->26,
57->109, leaving a DAG. It is NOT a safe runtime patch yet: CPU frontier149..151,
native resource/alias/order graph compilation and exact graph-parent receipts
must be handled before allocation. The current phase0-only shadow JobGraph
must not be passed to the native executor. Full evidence and cycle tests:
`build/single-pass-20261004/paired-pass-frontier-audit/REPORT.txt` and `evidence.json`.

Once-only CPU geometry preparation, paired whole-pass output ownership and a
measured total-FPS improvement remain incomplete. The goal stays active.

Ordinary281 (PID2008, unchanged280 DLL) passed9/9. Its HUD remained fresh for
1109ms/100 submissions/81 source advances, with native menu0. Paired request,
state, ordinal and family selectors were all zero at startup and final cleanup;
no experimental family assets/binding observers leaked into this ordinary run.
The stereo image was inspected. Process closed, late crash/reporter check clear,
preview restored. Post-test DLL/PDB and all five settings hashes plus calibration
absence exactly matched the280 deployment archive. Latest Python suite270 tests
and QuickBoot suite23 tests passed; no commit/push.

## Exact Native Parent Receipts And Live Frontier (2026-10-04)

PairedPassFrontier is now a tested immutable, source-bound planner. It classifies
the actual native pass definitions by verified CName hashes and work RVAs,
checks both original DAGs, derives the producer prefix with arbitrary node
handles, and emits only three phase1 cuts/additions. Phase0 remains unchanged.
Its state is CandidateNeedsCpuAndResourceCompile and ExecutionReady is always
false; it is not an executor graph or resource allocation permission.

The parent receipt does not infer a native pass from the draw stage number.
Native1EC9B8 resolves the graph node handle through nodeOrder, passes its object
to1EC404 and metadata+24 to1ECF2C. The latter copies metadata+5 (the phase1 order
slot) into work-context+38 and resolves metadata.viewIndex through manager+48.
Native1EC7EC copies this order/view context into asynchronous children, including
the verified A98414 path. Full body fingerprints gate these reads:

- 1ECF2C:0x82 bytes / FNV177EF1EE2846E667.
- 1EC7EC:0x98 bytes / FNV6B7C1293E9814833.
- 1EC9B8:0xD8 bytes / FNVBBF5A342038F09A4.
- A98414:0xAF bytes / FNV3D60EF291D8F6BC5.

NativeGraphParent double-reads the actual context/view/manager/slot, rejects
ambiguous view arrays and duplicate source order slots, and joins only the exact
current snapshot. Publication is bounded and carries the full source fingerprint,
native object/work/name, handle/ordinal/slot, context/manager and scope. It is
restricted to the captured preparation epoch and frame; ordinary tail frames
and later captures cannot reuse an earlier graph receipt.

Run282 (PID28944) captured the correct parents and byte-exact retained geometry,
but its Python reader accessed records before decoding. Run283 (PID26952) caught
a second Python issue: the frontier-array dictionary reused the decoded rows
variable. Both failures were reader-only; native originals were preserved,
no crash/reporter occurred, cleanup/preview restoration completed. Both reader
issues were fixed before run284.

Run284 (PID32220) passed all18 launch checks. Exact parents in the merged frame:

- MAIN stage3 -> node18 DepthPrepass; stage6 ->21/22/23/24 GBuffer_Solid;
  stages7 and9 ->26 GBuffer_Discard.
- PEER stage3 ->101 DepthPrepass; stage6 ->104/105/106/107 GBuffer_Solid;
  stages7 and9 ->109 GBuffer_Discard.

The source graph fingerprint was D21012F03C25B73B, with155 nodes,809 CPU edges,
119 ordering edges. The native planner reported an acyclic phase1 candidate:
cut25->26,57->88,108->109; add25->88,108->26,57->109. The CPU closure contains
32 nodes and60 incoming boundary edges. Native scheduling/resource compilation
still needs proof; no candidate edges were applied. Retained geometry stayed
176/178 commands, two scopes,42 indirect API calls per eye, zero output differences
over37488825 bytes/eye. All controls returned to zero; no crash/reporter,
process closed and preview restored.

`dump_native_graph_code.py` also records exact installed-PE exception-directory
function spans, verified direct caller sites, bytes/SHA/FNV and disassembly.
New compiler audit evidence establishes the declaration boundary at219730's
call21A063 ->A86438. Execution dispatch is queued earlier at219E10 through
A9B980/A9BA28 ->AC4A04 ->289C78 ->1441E0/1E6488; it can run before declaration
capture, while native prerequisites keep node work gated. Native985EB0 clones/rebases nodes and edge links;985730 and
9845A4 add consecutive phase1/phase0 ordering edges through986104. These are
audited facts, not yet authorized mutation APIs. All audit files remain in
`build/single-pass-20261004/{native-graph-compiler-*,graph-compiler-callers,
graph-build-modes,graph-sort-compiler,graph-context-copy}`.

117 CTests,276 Python tests and23 QuickBoot tests passed. A total FPS gain,
once-only CPU preparation and a persistent whole-scene paired pass are still
unproved and remain part of the active goal. No commit/push was made.

Final guarded run285 (PID13304) passed18/18 with both capture epoch and frame
checks. All32 merged-frame draw scopes received exact parent identities; all29
normal tail-frame scopes were explicitly rejected as outside the source capture,
instead of being classified from stale order slots. Both original graph phases
and native draw execution remained unchanged, and retained output comparisons
were still exact. HUD, screenshot, cleanup and late-crash checks passed; image
inspected, game closed, no crash/reporter and restored preview.

DLL: `9425795092c666360f71fc64b5171dce2a9dbc8a27c6c4c5d71dc54021bc5af5`.
PDB: `c7683db071c689c7b7ea4dc248560e99103c716f528be1320d71e95373ac8a45`.
Archive: `deploy-parent-epoch-receipt-285`.
Evidence: `parent-epoch-receipt-285/preparation.json`, graph-parent/source
binaries, `summary.json` and `retained-reference.bmp`.

Ordinary286 (PID10912, same285 DLL) passed9/9. Stable fresh HUD1078ms,
97 submissions/74 source advances, menu0; experimental graph/paired/family
controls remained disabled. Stereo image inspected. Game closed without a
crash/reporter; preview and all five settings hashes/calibration absence were
restored. Installed DLL/PDB matched the285 archive after all tests. No game,
RenderDoc/replay or standalone GPU test process remained.

## Owned Sort, Native Jobs And Resource Inputs (2026-10-04)

The full paired geometry candidate now has owned native graph header/order/
node/edge storage and both incoming/outgoing adjacency chains. The source has
155 nodes,809 CPU edges and119 emission-order edges. The candidate lifts33
PEER producer/ancestor nodes, preserves32 late barriers through their outgoing
frontier, and has1867 CPU edges and119 emission-order edges. Original source
storage is never edited. The native sorter985D0C accepted both owned graphs:
original phase ranks exactly matched native source ranks; candidate ranks
changed112 CPU positions and53 emission-order positions.

This is sorting/compilation validation only. Both A86438 and the native
execution payload still receive the original graph. Definition-owner adoption,
resource compilation for the candidate and once-only CPU scene preparation
remain incomplete. A raw union of phase0/phase1 edges is not the native event
model; use separate DAGs and lifted CPU-ready/ordered-emission events.

Runs287/288 initially failed the complete-job receipt check even though the
hook was installed and the original graph/retained outputs remained correct.
Execution queue setup can precede declaration GraphHook publication. Waiting
for GraphState2 in the queue hook missed its one valid native-array lifetime.
The corrected observer captures immediately after original1E6488 at289DDA,
retains each node's two actual groups(+1C), and separately retains frame owner
(+8). Later association uses owned immutable source bytes, graph address,
frame-owner identity, explicit request epoch and paired-root generation.
It never rereads expired queue payload/group-array headers after that callback.

Completed unselected receipts remain available until association/cancellation.
At most four receipts are admitted; obsolete completed receipts are retired
without waiting. Graph snapshot writes and resource snapshot reads are
serialized. Declaration owner has its own hold while association is pending.
Compiler completion carries its invocation's graph sequence/epoch/fingerprint,
so a previous callback cannot complete a newer resource capture. Native
originals are called once; nothing here redirects execution or suppresses draws.

Run289(PID24788) passed18/18 checks and verified all155 node group completions.
Run290(PID9904) passed19/19 including a second complete capture in the SAME
process: request epoch1->2, distinct frame owners and distinct full source
fingerprints despite the cached graph address staying the same. Both retained
captures remained exact over37488825 meaningful bytes/eye,12 comparison planes,
176/178 recorded commands,42 indirect calls/eye and two scopes/eye. No crash or
reporter; games closed and simulator preview/settings restored. Ordinary291
(PID3216) passed9/9 with all experimental controls disabled.

Before original1EDA84(mode2) at exact AF5A15, the observer now owns all four
native input arrays:3206 raw72-byte declarations/3206 use IDs,20 auxiliary keys
and12 boundary IDs in the tested scene. Original pool mode1 and post-compiler
mode2 are checked. Kind4/5 are lifetime scopes, NOT write/read access. Kind7
relates two keys; kind2 references a descriptor source and optional callback;
kind8 imports a DWORD native handle, not a second ordinary resource key.

The bounded offline analyzer validates original/candidate adjacency, identity,
phase ranks, complete input arrays and exact source fingerprint. It calculates
all3138 lost lifted-event dependencies with path/cut witnesses. Its initial
1023 potential resource conflicts all came from a shared marker-only key;
that deliberately conservative first classification is superseded by the
separate marker contract described below, not by an execution permission.

### Native Non-Allocation Marker Contract

Exact installed-image audit proves keyFD3DDE00/kind9 is emitted at1EC1D0 before
SceneDriver children; kind10 at1ECFB0 after them; kind11 at58DA9C carries the
definition+18 boundary mode. Native1F0ED0 copies only payloadBYTE8 forkind9,
NO payload forkind10, and DWORD8 forkind11. Unused payload bytes can be stale.
Native24CAC8 creates no physical resource interval for a marker-only group
withoutkind1 (except the separatekind12 allocation path). Kind11 payload0/1/2
is skipped by its allocation grouping;1/2 participates in boundary interval
extension through the separately captured boundary-ID array.

58DA9C execution mode invokes58DC98 over globalRender+4C50 phase slots;
58DC98 invokes1FAFA4 and clears/advances that command-context range. These are
real native command-boundary effects. Markers must NOT be counted as texture
alias conflicts, and must NOT be discarded as harmless logging either.

Marker-aware analysis requires verified original body fingerprints plus exact
source work methods, legal semantic payloads and definition boundary identity.
Mixed resource declarations orkind2/7 imports involving that key disable the
marker-only exception. MarkerOrderRequired stays true,MarkerOrderProved stays
false. No CPU independence, access-state or native candidate compilation claim
is made. The next full-pass work remains actual generation-safe definition
ownership and one consistent owned graph at BOTH execution and declaration
boundaries, followed by native resource/context compilation and image/FPS tests.

Current verification before the marker-contract launch:123 CTests,295 Python
tests and23 QuickBoot tests passed. No total FPS gain or persistent whole-scene
single-pass claim, and no commit/push.

Run292(PID30408) verified ResourceMarkerContract1 against all six original
function bodies, passed19/19 including both155-job receipts and request epoch
1->2, and retained exact output for both eyes. Marker-aware analysis of BOTH
captures found163 non-allocation markers(75 kind9,75 kind10,13 kind11),88
marker nodes and299 actual/conservative resource components. All3138 lost
event relations were retained:2454 opaque,399 marker-order unproved and285
without a represented resource conflict. No shared physical-resource component
was found for those lost relations. This is NOT proof of CPU independence or
candidate-native compilation; all execution/admission proofs remain false.

Ordinary293(PID31352), SAME292 DLL with the experiment disabled, passed9/9.
Stable HUD1109ms,98 submits and76 source advances; all experimental controls
zero. Both games closed, late crash/reporters clear, preview restored. Images
were inspected. Installed DLL/PDB:
`71a6c03eb8e4d4daf9c7ccffb624fdc788116b9621c870a731db8da1b844c768` /
`d8ecf71ac5892b951f84b55a4a7515b6094751687f277c66352af5697d591438`.
Archive:`deploy-native-resource-markers-292`.
Evidence:`native-resource-markers-292/{summary.json,preparation.json,
preparation-repeat.json,resource-order-report.json,resource-order-repeat-report.json}`
and`native-resource-markers-normal-293/summary.json`.

## Native Definition Sources And Consumer Identity (2026-10-04)

NativeDefinitionOwner MovePlan now accepts the native null owner slots created
for synthetic nodes, while rejecting duplicate non-null ownership. The bounded
NativeGraphDefinitionSource helper maps the cached composite(slot+10) through
composite+70 to per-view graph+70 unique owner vectors. It checks complete
headers, native allocator trailers, entries, disjoint allocation ranges and
every non-null graph node definition. Repeated MAIN/PEER nodes may legitimately
reference one unique entry. Per-view node counts are NOT ownership evidence:
native983EDC resets those counts after concatenation but leaves owners intact.

Observed generation hooks cover native983F1C(builder reset/destruction) and
986E74(owner destruction). The latter also covers the full-cache destructor
chain986E38->986EA8->986DB8->986E74 and the cold cache replacement path. Reset
and destruction originals execute unchanged, outside the registry mutex;
in-flight mutation counters prevent a read-only receipt being presented as an
exclusive native section. Builder append/reallocation after reset is covered
by fresh whole-source MatchesBefore validation, not inferred from reset epoch.
No owner vector is detached, no native destructor is delayed or called by CVR.

Source capture occurs before originalB56968 at its sole root return219E09,
before either execution queue or declaration captures the graph. Exact native
cache-slot/key, all owner entries and definition mappings are exported with
the same frame owner, source fingerprint, request epoch and pair generation.
The tested scene maps119 non-null nodes to65 unique definitions;54 definitions
are referenced twice across the paired graph. One real per-view owner contains
83 slots(65 non-null and18 synthetic null); the second owner is an unused
allocator sentinel with capacity/count0. All155 node groups are independently
checked for terminal completion. This is provenance, NOT definition ownership.

Run294 first capture passed this complete mapping and exact image comparisons;
its repeat was rejected by the existing queue receipt association. Run295 was
rejected by strict source validation(Failure::Source). Both games closed with
no crash/reporter; native originals and retained image comparisons were intact.
Bounded pending/selection/source-rejection diagnostics were added instead of
weakening source validation. Run296(PID8760) passed19/19, including complete
first/repeat ownership mappings, distinct frame owners, request epoch1->2 and
both155-job terminal receipts. These results do not establish all-native
generation consumer coverage; the earlier intermittent rejections must not be
relabelled as random crashes or proof that source arrays are immutable forever.

NativeGraphConsumerIdentity pins the exact freshly-created root prerequisite
group BEFOREB56968 moves its handle. Root234D4C->142B20 creates a fresh group
with positive references+1C. B56968 moves*arg5 to payload+18; A9BA28 moves the
same handle to its queued heap payload. Immediate VerifyConstruct checks
destination0/8/18 and consumed source handle0. MatchQueued at289DDA reads only
live heap payload0/8/18, after the old root stack addresses have expired. The
group is released natively atAC4A88, after289C78 and1E6488 return, not before.
Retaining+1C prevents allocator ABA. Pending0 is NEVER used as terminal proof.

Consumer IDs and epoch/generation labels are frozen at construction; selected
queue capture now derives them from the matched ticket rather than late global
counter values. Ticket frame/group reference rollback is balanced; native
release occurs outside the tracking mutex. The separate redundant frame hold
was removed to avoid an extra unchecked increment. All-node completion remains
the separate NativeGraphJobs lease. Tickets alone have no Complete API.

Scope remains explicitly selected-request observation: cancellation/rearm does
not register every prior/ordinary native consumer. allPriorConsumersAccounted,
DefinitionOwnershipProved, NativeExclusiveProved and ExecutionPermitted remain
false. The next required implementation is an all-native generation consumer
registry, retaining late consumers even after the selected experiment cancels,
followed by generation-owned native-vector retirement. Only then can an owned
compiled graph replace BOTHB56968/A86438 input paths and remove duplicate work.

Run297(PID17664) passed19/19 with the integrated ticket. Both captures map
119 nodes/65 definitions/54 shared definitions/18 null slots and complete all
155 job groups. First ticketconsumer1/group1602AEDAD70/request1/pair2; repeat
ticketconsumer2/group16028A5EEE8/request2/pair3. Constructor-transfer and exact
queued-payload checks both1. All12 retained comparison planes were zero-diff
over37488825 meaningful bytes/eye. Both-eye image inspected, game closed and
late crash/reporters clear. Preview restored, experimental controls zero.

125 CTests,300 Python tests and23 QuickBoot tests passed. Installed297 DLL/PDB:
`326dda42eb88c2a2334e3cf4ce2e34e0f91e2b5a826d7aa67d33ea6ff18caa85` /
`c8c389469d5e73c9d591540f553d3628ce50cb05c9fbc339bc76150350e9168b`.
Archive:`deploy-definition-consumer-ticket-297`.
Evidence:`definition-consumer-ticket-297/{summary.json,preparation.json,
preparation-repeat.json}` and source/mapping/owner binary exports.
No FPS gain, actual graph redirect, all-native consumer coverage or permanent
whole-scene single-pass is claimed. Goal stays active; no commit/push.

Ordinary298(PID32324), same297 DLL with the experiment disabled, passed9/9.
Fresh HUD1094ms/98 submits/77 source advances, all experimental controls zero.
Game closed, late crash/reporters clear and preview restored. Installed binary
and settings hashes were checked against the297 deployment manifest after tests.

## All-Native Consumers And Aggregate CPU Completion (2026-10-04)

The registry now observes every guarded B56968 root constructor, including
ordinary/menu roots outside a selected CVR request. Each consumer has its own
monotonic ID and pinned prerequisite/frame-owner identity; callback lifetime
does not depend on the current experiment epoch. Selected cancellation drops
only the selected report hold. The registry keeps admitted identity/completion
holds until actual callback return and terminal pending0. Native releases run
outside its mutex. It also polls at native root construction so completed menu
roots do not accumulate while VR frame-boundary observation is unavailable.

The AC4A04 callback's scheduler completion is separate from payload+18. It
transitively covers the native CPU DAG through289C78's all-leaf join and scoped
SceneDriver child propagation. The callback's own queued pending token cannot
be decremented until the original callback returns. The helper refuses null/
pending-zero fallback groups, prerequisite alias, bad refcounts/callers/layouts
or torn identities. Complete is unavailable before MarkOriginalReturned.
This is CPU completion, NOT GPU completion or definition ownership permission.

Run299 found independent all155-node completion but no valid aggregate receipt.
Runs300/301 narrowed this to a second scheduler execution path, not a crash.
Native143060 is the common worker loop: callback143248 returns14324C;
workerContext=RSP+50, H=&[RBP+A0]==workerContext+150, G=*H. Task completion G
comes from the copied task atRSP+40, is retained at1431FB, and its pending count
is decremented only AFTERcallback at14325A. The local reference is released at
143413. The existing14B164 path remains14B1E0/H=worker+68. Both body versions
are fingerprinted, including143060 size577/FNV91777795D38E54D9. No arbitrary
caller or inferred pending-zero completion was added.

The registry limit is256 live observers. Failed observation after normal
constructor/callback return drops only observer-owned pins outside the mutex,
increments sticky Unsupported and never marks Complete. This policy is valid
only because native definition adoption is still absent; future generation
retirement must preserve unresolved consumers of any adopted definitions.
Duplicate callback claims do not retire another callback's live receipt.

Run302(PID26960) passed19/19 with both scheduler layouts. At first snapshot:
7837 registered/entered/returned,7836 completed,1 current,0 unsupported,
0 overflow. At repeat:7927 registered/entered/returned,7926 completed,
1 current,0 unsupported,0 overflow. Consumer7828/request1/pair2 and consumer
7919/request2/pair3 each had a distinct terminal group; both aggregate CPU
receipts agreed with independent all155-node completion. Retained geometry
remained exact in all12 planes over37488825 meaningful bytes/eye; both-eye image
inspected, game closed, late crash/reporters clear, preview/controls restored.

126 CTests,300 Python tests and23 QuickBoot tests passed. Installed302 DLL/PDB:
`b60a92e51b8cb08f7f0ed9fe213819e929b7d261382a0d8e804f98248938f661` /
`12f9ca1904332cf477dd5c1d4fbfe9c7b1f7011690218674cd893f928ee62a4e`.
Archive:`deploy-both-scheduler-consumers-302`.
Evidence:`both-scheduler-consumers-302/{summary.json,preparation.json,
preparation-repeat.json}`; static ABI proof in root-terminal-completion-audit
and aggregate-caller-live-audit.

All-native callback accounting is now present. Binding ALL consumers to native
definition generations, proving prior-consumer coverage at generation birth,
moving/retiring native owner vectors and actual shared-graph execution remain
required. allPriorConsumersAccounted/DefinitionOwnershipProved remain false;
no native definitions are detached or destroyed by CVR and no FPS gain is claimed.

Ordinary303(PID19856), same302 DLL without the experimental startup environment,
passed9/9. HUD1063ms/96 submits/77 source advances; controls idle, both-eye image
nonblank, game closed and late crash/reporters clear. Preview restored and
DLL/PDB plus all settings/calibration absence matched the302 deployment manifest.

## All-native definition generation admission (304-306)

Added a bounded NativeDefinitionGenerations ledger: monotonic owner/consumer
IDs, immutable constructor identity, source/execution bindings, close-time
consumer high-water, sticky unsupported users, separate adoption/retirement
gates and observation-only reclamation. It retains owner epoch watermarks but
not an unbounded completed-consumer history. Fixtures exercise over12050 roots
and generations, pending adoption versus terminal retirement, provisional
consumers, source closure before constructor identity sealing, allocator replay,
capacity loss, unsupported removal and proved no-source cancellation.

Runtime admission now begins at219730 before983C80, not atB56968. A provisional
ID blocks retirement even before its source is known. Request/pair identity is
sealed once atB56968: PairGeneration can advance INSIDE the original root while
PreparePair runs. The guarded root's only normal non-constructor return is the
zero-sized-view branch before cache access; that removes an unbound observation
only, never counts native aggregate completion. Selection cancellation leaves
all-native accounting intact. Callback entry recaptures actual owners and
mutation stamps and binds Admission::Execution, excluding address-only ABA.

Poll is nonthrowing and reserves deferred-release storage before committing
ledger completion. Native observer pins are released outside its mutex.
Missing/overflowed admission and unknown callbacks poison coverage instead of
silently permitting generation ownership.

Run304(PID9436) exposed the early pair-identity timing error; selected definition
capture belonged to an auxiliary graph rather than the155-node merged root.
Run305(PID26180) fixed this selection and completed the selected155-node source
and aggregate receipt, but the all-native generation validator rejected53
changed-source observations. Run306(PID6596) reduced this to22 by separating
slot+0 last-use/LRU metadata from ownership mapping. On983C80 cache hit,983CD3
writes only that counter; key, topology, owner headers/entries/trailers and
mutation epochs remain required. Default selected provenance is still strict.
The exact983C80 body1C7/FNV2D15DBDC6AEC2159 is validated before ANY native detour
is enabled. These runs failed the full suite and are NOT successful ownership
validation; all had exact retained geometry in12 planes and no crash/reporter,
followed by owned shutdown and preview restoration.

The pre-WinMain RED4ext stack is not yet a strict pre-native-init certificate:
CRT initializers have already executed. Native manager drain is real, but
293BCE may enqueue AA3904 before293BF9 publishes its aggregate in manager+8.
Therefore a pre-hook callback's parent may still be private to its producer.
No prior-user coverage was inferred from a timer, null global or completed
selected callback. Renderer teardown and changed-source causes are under audit.

Native header adoption/destruction and actual owned-graph execution remain
disabled. allPriorConsumersAccounted and nativeExclusiveProved remain false;
the goal still includes those steps, shared CPU preparation, complete paired
passes and measured total FPS improvement, not just the generation observer.

Run307 isolated the remaining drift: execution-phase diagnostics reported ONLY
DefinitionPrefix(bit10), not raw topology, vtable/work, owner headers or entries.
Lifetime membership now has an explicit OwnerMembership policy which ignores
only the snapshot's32-byte mutable definition prefix. All object/owner mappings,
handles/order, raw topology, vtable/work, vector headers/entries/trailers and
mutation stamps remain required. The default ImmutablePrefix policy used by
selected semantic/source provenance remains unchanged. A lifetime receipt is
not evidence that those mutable contents can be shared or that a definition is
owned by CVR.

Run308(PID6452) passed19/19, including a same-process repeated request. First:
6190 early-admitted/source-bound/execution-bound roots,6189 completed,1 live,
0 unsupported/errors/coverage loss. Repeat:6247 bound roots,6246 completed,
1 live,0 errors. Two closed observation-only generations were forgotten;
native retirement/adoption remained0. Selected consumers6183/request1/pair2
and6240/request2/pair3 both bound65 unique definitions to generation6/born8;
the selected generation's completed-user count progressed2->3 with pending0.
Selected all155-node receipts, strict source mappings and distinct aggregate
terminal groups passed. All12 retained planes stayed identical over37488825
meaningful bytes/eye; both-eye bitmap inspected and nonblank. Game closed,
late crash/reporters clear, preview and all controls restored.

Ordinary309(PID23772), same DLL without diagnostic startup environment, passed
9/9. Stable HUD1031ms/93 submits/68 source advances, controls idle, game closed,
late crash/reporters clear and preview restored. DLL/PDB, all settings and
calibration absence match the308 manifest. This is startup/regression evidence,
not an FPS benchmark. The detailed all-native observer is diagnostic-only;
production shared execution still needs bounded cached/leased binding rather
than repeated deep graph snapshots.

127 CTests,307 Python tests and23 QuickBoot tests passed. Latest generation-aware
repeat validator also passed against the actual308 first/repeat JSON artifacts.
Installed308 DLL/PDB:
`628edf74c9f29f77bc92c009e9e70ebbb40ee36d114f64ee7b8580fac0f5a852` /
`bd28eb16197d36716e2a6bfbbf8a240f89759abf67d22cd76abe38ace4f37f5c`.
Archive:`deploy-generation-membership-308`.

Next executable-graph contract is audited in owned-root-stack-redirect-audit:
B56968 takes the original root stack graph pointer by address; its controlled
pre-constructor replacement also feeds laterA86438 andAB80EC. Source provenance
must remain separate from execution identity. After native escape, owned graph
storage cannot be freed or rolled back merely because observation failed.
Producer closure, declaration users and aggregate CPU completion must all be
covered, with fresh resource compilation from the owned declaration.

A CVR-only definition reader gate is a possible first reference-execution route
without taking native ownership. It still requires separate Closing/admission
and physical mutation states, no wait under the tracking mutex, and proof
against self-wait, paused producers and14AB30 nested helping(TLS183). The native
prequeue terminal handle at graphPointerAddress+F0 is pinned only while stack
identity is valid; pending0 before native queue/producer closure is not complete.
No such wait or source-stack redirect has been enabled by these results.

## Separate Completion And Native Retirement Intent (310-313)

NativeRootTicket now keeps immutable original source graph and expected execution
graph identities separately. An optional execution override never writes the
native slot or claims a storage lease. Wrong/unpublished payloads, expired stack
reads and source identity changes are covered by focused fixtures. The runtime
still uses the original graph; no stack pointer has been redirected yet.

Added OwnedGraphExecutionState with pre-publication rollback, post-construct
quarantine, producer-open/closed and separate declaration/execution gates.
Its12544-cycle fixture is a pure lifecycle test, not executable shared rendering.

The declaration completion contract is now audited and live-verified. D0 is
pinned from the original AA context before B56968; before originalA86438, its
job/context, frame, graph and actual completion handle must still match.
Complete requires normal originalA86438 return, normal219730 producer return
and the pinned D0 pending0. Context stack addresses are cleared at producer
closure and never rediscovered by polling. Native141B10 can rotate context+18,
so the execution aggregate E is still a separate receipt. AB80EC's guarded
B2AB60 callback does not dereference its copied graph pointer. AA's final142F88
adds only dependency links/releases, not new graph readers. Full relied-upon
dispatch, nested-scope, scheduler and cold bodies are fingerprinted before
detours. Contract:owned-declaration-completion-audit/CONTRACT.md.

Runs310/311 each passed19/19, with same-process repeated captures and exact
retained outputs. Run311 selected consumer6276 had distinct D0 and E groups;
both returned+completed, producer closed and no receipt rejection. They are CPU
receipts, not proof of GPU completion or native definition ownership.

NativeDefinitionTree captures the actual transitive shared-handle hierarchy and
owned payload layouts. It permits harmless positive shared-refcount changes,
but rejects physical handle/header/allocator drift, pairing conflicts, cycles,
overlaps and bounded-budget violations. Flattened binary ABI is Root16/Node64/
Handle48/Array56/GlobalAllocator32/String128. Unknown types remain observations,
not implicitly safe destructors. Selected source/capture identity stays frozen.

Run312(PID10304) passed19/19 and repeat. Actual65 top definitions and287
descendants form352 unique objects through287 shared handles. Full payload
inventory includes72 owned arrays, all using adapter985ABC, and7 heap CStrings
using direct1444B0/global32F67A0. String capacities imply280 total allocated
bytes; raw32-byte string metadata and concrete free-route receipts agree.
No top-level or reachable child destructor remains arbitrary in this scene:
134 vtable/work/destructor triples reduce to10 audited DL=0 destructor bodies.
Evidence:definition-tree-destruction-audit and definition-payload-retire-certificate.
This is a bounded actual-scene certificate; different types must fail admission
until classified, not silently inherit these predicates.

Prolong-only deferred native deletion can avoid the separate allPrior bootstrap
proof: intercept the exact original native deletion point and keep its original
objects/allocations alive only for extra CVR owned readers. This must NOT set
allPrior=true. Pre-reservation precedes extra-reader admission; native intent
closes further readers and transfers the exact original16B header into stable
native-format storage. Reset983F1C's audited fixed20E570/13B0690 prelude always
reaches deletion for valid nonnull inputs; Destroy986E74 interception must occur
before capacity branch. The985294 relocation call is not such an intent.

Final deferred deletion requires a verified LIVE nonempty native-intent scope
where original still must invoke985B40, matching allocator identities and a
sticky shutdown/unload closing gate. Pointer equality/readability alone is not
liveness. Service only original986E74(saved16B) and plain bookkeeping outside
locks, never attached unaudited FrameOwner/COM/RAII releases. Finish an already
started destructor exactly once if closing races it; stop subsequent attempts,
always continue original operation, and quarantine uncertain state without
native frees. No wait, header move or final deferred destructor is active yet.

Ordinary313(PID10620), same312 binary without diagnostic startup, passed9/9.
HUD1032ms/93 submits/67 source advances; game closed and late crash/reporters
clear, preview restored. Latest installed312 DLL/PDB:
`8013be7ed5c3433bc35b42519938db4e8d17e7ede4d2a6281fc7ba868d1d8226` /
`81696f4d1010aea157d3080ccba9751c710cfeb1e6229358c23a76d35b1c751c`.
130 CTests and312 Python tests passed. NativeRetirementDeferral pure-state
implementation is being finished separately; it is not runtime-adopted or
counted as successful execution here.

The full goal remains actual owned/shared graph execution, shared CPU geometry
preparation, full compatible texture-array passes and measured pure FPS gain.
These receipts close concrete lifetime gaps but are not a substitute for that
end state. No total FPS improvement is claimed by310-313.
