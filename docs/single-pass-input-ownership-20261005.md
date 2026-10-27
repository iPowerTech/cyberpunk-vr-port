# Single-pass input ownership, 2026-10-05

## Verified In Game

Run `build/single-pass-20261005/indirect-binding-metadata-349/summary.json`
passed all 21 launch checks and a second capture in the same process. Both
captures owned 354 commands in the selected retained geometry interval:
270 direct and 84 indexed-indirect. This is not whole-scene coverage.

All 84 indirect calls matched native return site `0x623CC1`, one 20-byte
argument, no count buffer, DEFAULT heap, native class 0, complete logical/COM
buffer equality, and tracked `INDIRECT_ARGUMENT` state. The full native B0
records were captured coherently and revalidated at the original call.

Run `build/single-pass-20261005/owned-indirect-snapshots-350/summary.json`
passed all 21 checks and a second capture. In each capture:

- 84/84 arguments were copied to independent private GPU buffers: 1680 bytes.
- All 84 array-reference draws used those private buffers, not the originals.
- All 84 source submission leases were validated before native submission.
- All 12 compared color/depth/stencil planes were byte-identical to native.
- No new crash or crash-reporter process was observed.
- Test controls returned to zero; preview settings were restored; game closed.

Installed DLL for run 350:
`67B850E700ECE976DFA79834412BA42AE854DA50CE64B4B46166370F0FB891BB`.
Settings hashes before and after deployment are in
`build/single-pass-20260927/deploy-owned-indirect-snapshots-350/manifest.json`.

Normal run `build/single-pass-20261005/owned-indirect-normal-351/summary.json`
passed 9/9 checks with all test controls disabled. The game was closed, no new
crash/reporter was observed, and the four settings files still matched their
deployment hashes.

## Current Source CBV Ownership

Run `build/single-pass-20261005/source-cbv-mask7-354-check/summary.json`
passed all 21 launch checks, including a second capture in the same process.
The first selected interval contained 354 commands and 2788 reflected CBV
bindings; all 2788 had current held backing owners. The second contained 353
commands and 2783 CBVs, all owned. This is complete CBV ownership for those
selected intervals, not a CBV content snapshot or whole-scene coverage.

The first capture classified 1920 registered bindings and 868 exact-created
bindings. Native `0xCAE7A8` uses stage mask 7 for b0/b12/b13. The observer now
admits that exact mask while tracking only VS/PS bits; unknown masks/stages
remain rejected. Pure regression tests cover this admission. Origin-bearing
descriptor values are interned so retiring one of two matching CPU slots
cannot remove the other slot's held origin.

All 84 indirect snapshots in both captures used private GPU arguments and
passed source-submission validation. All 12 color/depth/stencil comparison
planes were identical. The initial run-354 checker rejected a valid registered
setter origin without a CreateCBV return; the corrected checker requires an
origin for every creator, while permitting creator-zero registered origins.
That initial failed run is retained separately, not relabelled as passing.

Installed DLL for run 354:
`A98AA274BF453AE13D4E3B6F2BD57DE4A10C84AA3089F89B0B1A57CED4B45A6B`.
Normal run `build/single-pass-20261005/source-cbv-normal-355/summary.json`
passed 9/9 checks. Both runs closed the game, observed no new crash/reporter,
restored preview settings, and preserved the four configuration hashes.

## Source-Copy Contract

`NativeIaBinding` obtains only the exact current native indirect binding. It
uses OS-unwound callsite registers, current TLS context, recording generation,
actual API operands, signature, coherent record and logical/backing bounds.
Read-only revalidation repeats actual origin checks. Unknown state can be
observed but cannot authorize a copy.

`NativeIndirectCopy` retains the real native Allocation and its allocator
wrapper. Allocation must be released before its allocator/Pimpl. An explicit
`ActualCallScope` binds a receipt to one occurrence, recording and thread;
historical receipts cannot authorize another callback's copy.

The copy inherits the unchanged valid ORIGINAL native read epoch. It is
inserted on the same native list, after existing producer/alias/readiness
barriers and before the original read, for exactly the same 20-byte range.
No later command reads the original source on behalf of the snapshot. Parent
identity, allocation retention and zero pending barriers are not substitutes
for that original ordering obligation.

The audited native registration/flush path adds `COPY_SOURCE` to the current
read-only state before copying. It does not edit a map directly or restore an
old map snapshot. Private buffers transition from COPY_DEST to
INDIRECT_ARGUMENT. Typed owners remain through actual source GPU completion.

The opt-in test flag is `--retained-indirect-snapshots`, requiring
`--retained-source-stream` and the unchanged-native reference path. All
controls default to zero. Original native commands remain unchanged; only
the diagnostic array reference consumes private arguments.

## Actual Finite Descriptor Tables

Run `build/single-pass-20261005/source-finite-tables-356/summary.json`
passed all 21 launch checks and a second capture in the same game process.
Both captures preserved all 354 selected source commands and independently
owned their supported finite descriptor tables:

- 354/354 source occurrences succeeded; no table/provider failures.
- 3972 table occurrences, preserving original parameters and full table extents.
- 31406 descriptor positions represented by 827 unique immutable CPU heaps
  containing 2484 descriptor positions; canonical versions reuse their owners.
- 6275 declared used-slot occurrences; all 2788 reflected CBVs remained owned.
- 440 unbounded-root uses remained explicitly deferred, not filled with nulls.
- All 84 private indirect snapshots and all 12 image comparison planes passed.
- No new crash/reporter; controls reset, preview restored, game closed.

Installed DLL for run 356:
`946C35AE7E95351CD81B164507DC0E098184C669B565D60073A9E5C9933ADA3A`.
Normal run `build/single-pass-20261005/source-finite-tables-normal-357/summary.json`
passed 9/9 checks, with all controls disabled, no new crash/reporter, restored
preview and closed game. All four configuration hashes still matched the
deployment manifest after both runs.
The opt-in flag is `--retained-source-tables`, requiring the original retained
source stream and current CBV ownership. It defaults to zero. `SourceTablePlan`
maps the exact family uses onto the original serialized root, retains offsets,
and separates unbounded-root uses. Capture recreates independent FLAG_NONE CPU
descriptors and retains their current resource/counter/origin owners.

This run validates actual descriptor metadata ownership and interning, not
resource contents. The array-reference geometry still uses the original native
bindings; a full source bank cannot be emitted while required bindless tables
remain deferred. No native command is removed or merged, and no FPS gain is
claimed from these metadata counts.

The pure selector evaluator independently validates original SSA/reflection
artifacts and evaluates all audited b4 DWORD sites for 13 families using supplied
immutable bytes. It rejects indices outside the declared 32768 domain without
masking. The multilayer t5-dependent family remains explicitly unsupported.
The caller must separately prove that bytes belong to the actual source read
epoch; synthetic evaluator inputs do not freeze native CBVs.

144 C++/GPU tests and 546 Python tests passed for the run-356 source tree.
Core selector tests are hermetic; optional original-artifact integration tests
are explicitly skipped when the local evidence folders are absent.

## Other Changes And Tests

The original-source binding bank now accepts one final unbounded descriptor
range with an explicit finite admitted extent. Original root bytes and
parameter indices remain unchanged. Unused tables may be omitted. Finite
aliases retain their previous behavior. Heap limits, tail bounds, register
arithmetic, immutable-version identity and strong leases are checked.

Source recording refuses foreign lists, repeated/out-of-order commands and
mid-prefix resource-batch reset. Native scene-update observations use actual
stack liveness and distinguish cancellation after unwinding from a normal
callback return. The unchanged thunk ABI has a real throw-through fixture.

140 C++/GPU tests and 511 Python tests passed for the run-350 sources.
The original-source bank tests include high parameter 33, dynamic SRVs,
unbounded declarations, descriptor overwrite, resource-content non-freezing,
Reset, fence lifetime and transactional rejection cases.

## Still Incomplete

Freezing indirect arguments is not freezing all scene inputs. IA contents,
CBV/SRV/UAV resources, counters and required dynamic source descriptor tables still need
complete ownership/content/order contracts for actual deferred whole-pass
execution. The bindless table cannot be duplicated per command; versioned
immutable capture and interning are required.

Both native eye preparations/draw streams still execute. The coupled
whole-discard topology remains a sort-only candidate. Full single-pass,
shared CPU preparation, native suppression and total FPS gain are not proved.
The direct allocation path was exercised; the alias-payload path was not.

## Next Concrete Input Step

Before implementing general CBV copies, capture a separate resource-detail row
at the current source callback: actual heap type/flags, native B0 allocation
form, read-state status/value, resource creation/placement receipt and typed
allocation owner. Current CBV ownership rows do not contain those certificates.
Do not query a historical GPU address or infer DEFAULT from native class3.

The current 2788 bindings comprise 613 class1 pooled b4 uses, 1307 class3
registered uses, and 868 exact-created upload-arena views. Class1 pool history
and occupancy alone do not pin its allocation; class3 padded whole-COM views
are not the indirect path's logical-size-equals-Width predicate. Arena class4
B0 allocation fields do not identify the arena's physical owner.

Reuse the existing native read registration and original source-read epoch,
without CPU GPU-completion waits. Preserve the old material-specific 292-byte
API while adding a separately proved range API. Snapshot into owned GPU slices
at the original occurrence, then validate source submission and real fence
lifetime. Only actual owned selector bytes may close the deferred bindless
domains. The detailed current-category report is
`build/single-pass-20261005/source-cbv-content-next/REPORT.md`.

## Actual Resource Details

Run `build/single-pass-20261005/source-cbv-details-358/summary.json` passed
21/21 checks and a second capture. Both observed all 2788 CBVs at their original
source callbacks, revalidating current source ownership before/after observation:

- 868 committed UPLOAD views with known creation/current GENERIC_READ.
- 613 committed DEFAULT class1 pool views, native state-status8, occupied
  512-byte slots and logical payloads16..368. Pool/MA/allocator headers matched.
- 1307 placed DEFAULT class3 views with tracked state-status2/value1 and
  known creation COMMON. Each actual API heap matched the MA-derived heap;
  the MA original resource matched the held source COM resource.
- All 1920 DEFAULT allocation/allocator headers had the expected observed ABI.
- All finite tables, indirect snapshots and 12 image comparisons still passed.

Class3 API/MA allocation spans are65536 bytes, while actual COM buffer widths
are512/1024/1792 and native logical payloads are smaller. Neither the allocation
span nor CBV padding is the copied shader payload. Header observations do not
pin native owners or reserve pool/upload slices, and no content copy is claimed.

Actual356 selector analysis found0 reflected/native-logical overruns. The13
b4-supported families account for187 source occurrences and271 required b4
selector snapshots;167 occurrences still require multilayer t5 input data.

`--retained-source-details` requires current source CBV ownership and defaults
to zero. Its independent128Q rows preserve exact occurrence association, known
versus unknown observations, actual creation/placement receipts and raw headers.
Legacy resource-creation hooks now retain their actual initial-state operand;
old callers remain explicitly unknown. Private interface retrieval retains the
heap using the documented COM reference semantics, not a borrowed pointer.
See [Microsoft GetPrivateData](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12object-getprivatedata).

Installed DLL for358:
`6659468D07D685E302513B259932851B1E6671B60F690C4902F9FA957D153963`.
Normal run `build/single-pass-20261005/source-details-normal-359/summary.json`
passed9/9, with no new crash/reporter, restored preview and closed game.
146 C++/GPU tests and554 Python tests passed for the358 checkpoint.

The next implementation is a transactional current-source CBV snapshot module:
read-only preflight of every request, typed MA+allocator pins for DEFAULT,
existing pool write-history/queue proof for class1, native combined read
registration for class3, and unchanged fixed-read UPLOAD sources. Copy only
the certified reflected payload into private aligned DEFAULT slices, preserve
the padded descriptor size, and finalize each private recording arena once.
All original native reads remain enabled until later whole-input admission.

## Actual DEFAULT Payload Snapshots

Run `build/single-pass-20261005/source-cbv-snapshots-360/summary.json` passed
21/21 checks and a second capture. Both selected intervals preserved354 source
commands and2788 CBV associations. At their original native callbacks:

- 1920 DEFAULT CBV payloads were copied into private GPU buffers.
- 1373748 exact reflected bytes were copied; padded private views occupy1664768 bytes.
- Both per-recording arenas finalized once; all354 source-submission receipts passed.
- After the real source GPU fences, private READBACK payloads were mapped and
  associated with their original CBV rows. No CPU GPU wait was inserted in recording.
- 868 UPLOAD views remained explicitly deferred; their proof gate stayed0.
- All12 original/array-reference image planes still matched; no crash/reporter.

The native source draws remain enabled, and the private CBVs are not yet emitted
through a full binding bank. Copied bytes/fence completion prove the snapshot
outputs, not whole-scene input freezing or private-bank consumption equivalence.
Both shader-table and UPLOAD gaps remain. No FPS gain is claimed.

Submission validation is transactional per eye: any failed receipt aborts the
whole eye's snapshot candidate and clears every same-eye submission marker.
Prepared-but-unrecorded batches cannot partially finalize an arena. DEFAULT
allocations retain both genuine native MA allocation and allocator through the
real command-resource flight; allocation is released before allocator.

The class1 pool API now has exact-range `PreflightRange`, `CaptureRange`, and
`ValidateRange`. Preflight carries no registered-read grant; capture rejects a
changed held epoch before native registration. Original material wrappers still
copy exactly292 bytes. The position-ledger resource-qualified resolver additionally
matches the held actual destination and has no address-cache fallback; its WARP
fixture checks original ordered payloads and rejects another resource at that address.

Installed DLL for360:
`23C67BE4392BC9D57A445868F10841B9832BD1EC957E254917BF95769096CE66`.
Normal run `build/single-pass-20261005/source-cbv-snapshots-normal-361/summary.json`
passed9/9, game closed, no new crash/reporter, preview restored.
147 C++/GPU tests and562 Python tests passed for the360 checkpoint.

## Actual Bindless Selector Values

`build/single-pass-20261005/cbv-selector-evidence-360/selector-evidence.json`
independently verifies original root/VS/PS hashes and exact ordinal/eye/list/
generation/context/binary/blob associations in both captures.187 supported
occurrences use271 private b4 snapshots containing44476 logical bytes. All1790
audited selector evaluations are below32768, with101 distinct offsets spanning
29614..32763.273 dynamic table occurrences have these finite candidate extents:

- VS parameter13, space1:84 occurrences, maximum extent32764.
- PS parameter31, space1:187 occurrences, maximum extent32757.
- PS parameter33, space3:2 occurrences, selected offset31498, extent31499.

These are actual values for the captured occurrences, not universal producer
invariants or historical descriptor-owner certificates. Original high offsets
must be retained.167 multilayer occurrences still require actual t5 inputs.

UPLOAD remains separately disabled. Static allocator analysis found a monotonic
absolute reservation/CAS and fence-gated tail protocol, but current origins
do not carry the absolute reservation/retirement fence/source-submit association.
Transient CreateCBV also precedes its CPU memcpy, so creation is not a writer
completion event. Exact audit and hook points are in
`build/single-pass-20261005/upload-slice-lifetime-360/REPORT.md`.

## Runtime Selector Ownership (2026-10-06)

Run `build/single-pass-20261005/source-selectors-364/summary.json` passed
21/21 checks and the repeated capture. Both original intervals contain354
commands. For the13 supported original shader families:

- All187 occurrences captured immutable compact dynamic SRV tables.
- All1790 audited selector sites evaluated within their original32768 extent.
- 976 selected-slot occurrences use43 interned CPU heaps with164 descriptors.
  Original high indices remain separate from compact storage indices; no
  original shader index is rewritten and no32768-entry heap is made per draw.
- All438 provisional b4 inputs matched their exact original-callback DEFAULT
  GPU snapshots:271 supported-family inputs plus167 multilayer inputs.
- All167 multilayer occurrences acquired their exact current PS t5 view at
  original root23/offset5, and their conservative record bounds matched GPU b4.
  No t5 content copy, native SRV-buffer state/pin, or texture-selector ownership
  is claimed for those167 commands yet.
- The12 color/depth/stencil comparison planes remain exactly equal. The84
  indirect snapshots and1920 DEFAULT CBV copies still validate;868 UPLOAD
  views remain deferred and their enable gate stays0.

The startup candidate cache is independent of the frozen position ledger.
It is qualified by the held current destination object/range, stores immutable
CPU-intended copy bytes, and never certifies a producer queue or GPU draw read.
Only an independently associated original-call GPU snapshot can validate the
candidate bytes used to enumerate these selected descriptors. Its bounded
resource-private owners do not retain their destination or form COM cycles.
Limits are32MiB globally,2MiB/512 ranges per resource, and65536 bytes per copy.
Clear generations plus the actual enable gate reject late publications.
Cleanup reports0 current bytes,0 ranges,0 registered owners and all controls0;
18960 captured payload bytes deliberately remain held by completed diagnostics.
The development-only first private owner pins its module until process exit,
so a retained resource cannot call an unloaded owner vtable. Normal mode never
attaches these owners.

The first363 launch completed the new receipts but its Python reader shadowed
the existing native `blocks` dictionary. The364 launch verifies the corrected
reader end to end; an AST regression test protects that dictionary.
152 C++/GPU tests and577 Python tests pass. Installed DLL:
`8FB59F8E0002E7F8E6EE987FC55956B071A98D3486A4C243A06CE68445951CD3`.
Normal run `build/single-pass-20261005/source-selectors-normal-365/summary.json`
passed9/9 with all controls0, no crash/reporter, restored preview and closed
game. Both owned runs used the bounded close timeout and then terminated the
process; shutdown observation found no crash. All four settings hashes still
match the deployment baseline.

The successful-CAS UPLOAD thunk now returns through the real CALL return on
both paths, with a relocated JNE preserving CET shadow-stack compatibility.
Its expanded ASM ABI fixture passes. The patch is still uninstalled: there is
no verified installation quiescence/lifecycle token, and reservation observation
alone would not close UPLOAD writers/retirement. See
`build/single-pass-20261005/upload-lifecycle-363/REPORT.md`.

This is current-source descriptor ownership progress, not a completed binding
bank or constant/texture input equivalence proof. Native draws/preparation for
both eyes remain enabled, private compact tables are not consumed, and no total
FPS gain or whole-scene single-pass is claimed.

## Original Structured Buffer Snapshots (2026-10-06)

Run `build/single-pass-20261006/source-t5-369/summary.json` passes21/21 and
the repeat. Both captures nominate all167 original PS t5 occurrences from the
actual native buffer setter, not the texture registry or a historical GPU VA.
The measured binding is handle-indexed low type8/native kind0, state-map status2
with actual shader-read mask `0xC0`, dedicated committed1MiB DEFAULT resource.
Individual and cached-group routes have separate guarded transparent before
observers; source-time layout/CPU/GPU descriptor equality validates their inputs.

All167 bounded2048-byte spans now copy at the original DEFAULT read callbacks:
342016 actual GPU bytes per capture. Each per-recording private arena finalizes
once, and all original source-queue receipts validate. All12 original/reference
color/depth/stencil planes remain equal. Private t5 ranges are not bound to a
consumer and no native draw is suppressed.

This dedicated committed adapter retains the actual resource/implicit heap;
it neither fabricates a CBV nor nominates an MA owner from unrelated fields.
It requires exact observed committed creation, full native backing, no shared
or protected form, known original tracked shader-read state, source-call scope,
native registration/flush/recovery and saved GPU-descriptor revalidation.
Initial COMMON is never used as the current state. The actual WARP protected
session getter returns `DXGI_ERROR_NOT_FOUND` for an unprotected resource;
the regression fixture verifies that precise result and rejects live sessions
and unknown failures.

The369 diagnostic confirms a legitimate recording handoff at finalization:
stored source thread3564, finalization thread30144, equal actual/owned list,
pending0 and private cursor172032. Source Prepare/Record retain their original
thread/scope checks; finalization instead validates the exact native recording
lease, like the existing CBV arena. The earlier368 attempt refused this handoff
before the second eye. No crash occurred, and that attempt was not reported as
a complete geometry or GPU snapshot pass.

CPU-before-draw t5 candidates are still absent, so these167 draws do not yet own
their dynamic texture selectors. The last exact-resource copy receipt is a
successful2560-byte update at1044224, from an actual UPLOAD source, with a fresh
full structured view marker. That does not cover the needed436736/2048 range.
The normal cache's whole-payload invalidation and bounded range eviction need
to be distinguished before changing its current-copy semantics; no old GPU
payload or late descriptor lookup is promoted to source ownership.

Multilayer selector evaluation now audits all10 original PS sites (VS has0):
field100 is raw, fields104/108 each supply low/high16-bit indices. Planning uses
the original b4 mask and checked view-relative base, at most32 records/4096 bytes.
Thus a16-record mask yields80 transformed candidate values and160 conservative
original-site evaluations, not48 raw indices. The exact audit is in
`build/single-pass-20261006/multilayer-plan-366/REPORT.md`.

156 C++/GPU tests and586 Python tests pass. Installed369 DLL:
`E92844EC5FAAF8B8E13D08D134AF6B1FCBB9192B51D2860E6CD046DD6011598E`.
Normal run `build/single-pass-20261006/source-t5-normal-370/summary.json` passes
9/9 with no crash/reporter, all controls0, restored preview and closed process
after the bounded close timeout. All four settings hashes match deployment.
UPLOAD remains disabled/deferred; shared CPU preparation, a consumed whole-input
binding bank and total FPS improvement remain unproved.

## Source Table Assembly And Coverage (2026-10-06)

The structured candidate cache now preserves untouched subranges of observed
UPLOAD copies independently of the generic single-copy API. Composed candidates
carry actual per-piece copy labels; the reader checks exact contiguous coverage
and independently compares the selected bytes with original-call GPU snapshots.
Limits and Clear-generation gates remain enforced. Clear releases structured
metadata capacity, and eviction removes a payload's fragments in one linear
filter pass. This does not certify a GPU producer, queue order or frozen content.

Run `build/single-pass-20261006/assembly-coverage-373/summary.json` and its repeat
pass. The original 354 commands produce 187 successful source binding plans and
167 dynamic-coverage refusals, with exactly 868 missing UPLOAD CBV domains.
The assembly input uses actual captured shader reflection, saved original
root/list/generation/occurrence/eye, finite and sparse descriptor owners, and
finalized private DEFAULT copies. Original-safe CBV certificates remain empty.
No plan is Ready; no assembled bank is consumed and no native draw is suppressed.
The 4245 table receipts preserve 8932070 original descriptor coordinates in total,
not that many compact selected descriptors. Shared VS/PS CBV requirements are
merged by original parameter/offset while preserving maximum reflected bytes.

All 167 t5 GPU snapshots still validate, but CPU candidates remain absent.
Exact-resource coverage queries show a current matching marker and existing owner
with zero structured bytes/ranges, and a hole at436736 for the2048-byte read.
Thus this run does not prove that partial-overwrite preservation solved the
runtime gap. The next investigation includes actual full CopyResource writes,
which currently invalidate the candidate without recording their source kind.
Initial copy-before-marker and eviction also remain possible until observed.

158 C++/GPU fixtures and608 Python tests pass. Installed373 DLL:
`0083DB705AE045214819611EC8BA2993FEE870A7F2D115A57BC834849AEE1CD7`.
The owned run closed the game, restored preview, reported no crash/reporter,
and left all controls and current candidate-cache counts zero. It does not
demonstrate a whole-scene single-pass path or total FPS improvement.

The offline UPLOAD study identified real lifecycle, block-refill, writer and
retirement functions in `build/single-pass-20261006/upload-proof-372/REPORT.md`.
Small CBVs suballocate from native per-thread64KiB blocks: latest-thread CAS is
not a valid view association. The current installer remains disabled. Safe
installation must also be implemented; a MinHook enable call followed by an
unprotected seven-byte patch is not a suspended-thread transaction.

Normal control run374 failed during QuickBoot loading, before test capture.
Dump `Cyberpunk2077-20261006-060348-3996-25860` faults in the game's
`0xB8084B` virtual call through a null object during handle release. Its bounded
stack scan contains CET frames but no VR DLL frame; this is a scan, not a proven
unwind or root-cause attribution. The requested diagnostic export spans were
not captured by this minidump, so they cannot establish control state at fault.
The unchanged373 DLL subsequently passes normal run375,9/9 with no crash or
reporter, restored preview and closed process. All four settings hashes match
the deployment baseline. The failed374 result remains preserved, not relabelled
as a passing run or a proven random crash.

## Multilayer Source Coverage Closed (2026-10-06)

The global32MiB candidate cache now evicts owners without current typed
structured coverage before falling back to all owners. Per-resource bounds,
generic512-range semantics and unknown-write invalidation are unchanged.
This policy is necessary in addition to preserving partial structured writes:
the actual t5 owner has547 current intervals, beyond the generic range limit.

Run `build/single-pass-20261006/structured-priority-380/summary.json` passes21/21
and the repeat. All167 original t5 ranges have complete current CPU coverage;
their342016 candidate bytes match independently captured original-call GPU
bytes exactly. All167 multilayer occurrences now own their shader-selected
texture descriptors. Compact slot occurrences increase from976 to11497 in the
primary capture (11493 in the repeat, with a different ordinary selector set).
Every one of the354 original source commands now passes binding-plan Build.
The12 color/depth/stencil comparison planes remain equal.

This is not bank execution: all354 plans remain nonready because of the868
UPLOAD CBVs, with no original-safe certificates, no assembled/consumed bank and
no native suppression. Total FPS improvement and shared CPU preparation remain
unproved. Installed380 DLL:
`E8C5CD8BB10C25F1F64DA16CEA9862ACC5CDE750DF97F31F0E52908B3EE1E05E`.

Native upload-observer preparation publishes a nonce/process-bound immutable
Plan368 and accepts only a detached breakpoint Receipt144 with exact current
patched-body/relay/protection checks. Prepare never patches native code; the
external helper commits only under an actual Windows debugger event, without
calling DLL code or allocating target storage while its threads are stopped.
Observer enable, PrepareRequest and AcceptRequest default0. The helper and
pure failure fixtures exist, but actual debugger attachment/installation and
the lifecycle/block/writer/retirement observer chain have not run yet.

159 C++/GPU tests,640 Python tests and23 QuickBoot tests pass at this checkpoint.
Normal run `build/single-pass-20261006/structured-priority-normal-381/summary.json`
passes9/9 with all controls0, no crash/reporter, restored preview and closed
game. Run378 had loaded the world but timed out on an unnecessarily large
viewport-size requirement, not a crash. The viewport guard now accepts a usable
320x240 or larger game window; world validity and stable native HUD remain
mandatory and separate from window size.

The external helper's additional failure fixtures reject foreign native code
without resuming/detaching an uncertain target, match the native protection
whitelist, keep confirmed termination in exit-event drain, and avoid a second
detach after successful recovery with failed receipt publication. These are
fake-backend/pure contract tests, not a real debugger installation certificate.
The final installed DLL and all four settings hashes were rechecked after381;
no game, CrashReporter or capture-replay process remains.

## External Observer Installation Verified (2026-10-06)

Run `build/single-pass-20261006/upload-install-live-382/summary.json` passes10/10.
The real external debugger transaction holds the attach breakpoint event,
checks every live RIP, commits the prepared CALL/JNE bytes, restores protection,
continues the event and detaches. The native DLL accepts the exact nonce-bound
receipt after rechecking its full patched177-byte body and relay. Observer
Enable stays0. This proves installation in the actual game, not a reservation
generation, writer completion or UPLOAD read permission. Ordinary rendering
checks pass afterward; the game closes with no crash/reporter and all controls0.

The unified launch runs the debugger transaction in its own Python process,
not a timeout-killed child. Uncertain held-event recovery cannot escape on
logging or observation failures; the verified owned-game termination path
drains debug exit events before allowing process exit.643 Python fixtures pass.

The next bounded UPLOAD route is direct VS b5 only: creator1F2960 follows the
completed memcpy at1F292F, with R13 logical bytes and R12 padded reservation.
Its immutable Origin now records those actual fields and an optional genuine
creation recording lease. A copy must fit the completed logical prefix, match
the exact original recording token/list/generation/context/registry and remain
inside the original queued read epoch. Native retirement/signal bodies have
separate original code guards. These metadata and policy changes have160 passing
C++ fixtures, but direct UPLOAD copies have not yet been verified in a live run.
Transient1F0BF7 origins precede their CPU writer and remain unqualified.

## Direct UPLOAD Copies Verified (2026-10-06)

Run `build/single-pass-20261006/direct-upload-385/summary.json` passes21/21
and the repeat. All354 direct VS b5 producers match actual112 logical bytes,
256 reserved bytes, completed memcpy1F292F and the identical source/creation
recording token/list/generation/context/registry. Each owning copy executes
inside the certified original DIRECT read epoch, before the original draw;
both retained recording leases validate against the actual source submission.
The inserted read does not extend the source slice's original lifetime.

GPU readback validates1920 DEFAULT copies plus354 UPLOAD copies,2274 total.
The remaining514 UPLOAD domains are transient1F0BF7 creations preceding their
writer and stay deferred. No plan becomes ready and no bank is consumed.
The12 color/depth/stencil comparison planes remain equal. No total FPS gain
or shared native CPU preparation is claimed. Installed385 DLL:
`0C2B9CEE70049563FF935F7E470385EF4994988E4D9DBA91DB4197C9E232E930`.

The385 scene has159 exactly matched t5 copies and8 refused multilayer commands
whose b4 CPU candidate is absent in both captures. No mismatched bytes were
accepted; missing b4 inputs remain explicit and require investigation rather
than historical nomination.160 C++/GPU and655 Python fixtures pass.

The optional arena lifecycle adapter is implemented with completed nested-reset
qualification and monotonic generation, but its native hooks are not installed.
The direct inline route does not require observing every CAS: it uses the
already valid original queued epoch, exact completed producer and same-source
lease. Wider replay or an unqualified transient writer would need a different
proof; enabling the upload gate alone cannot supply it.

Normal run `build/single-pass-20261006/direct-upload-normal-386/summary.json`
passes9/9 with no crash/reporter, all controls0, restored preview and closed
process. Installed385 DLL and all four settings hashes match deployment.
The narrow next transient writer observation can use the existing
`Grading.cpp::ForwardBufUpload` around its original1F088C call; a second hook
at that same function would be wrong. The alternate220068 class4 writer needs
its own guarded invocation. Neither transient completion route is implemented
or enabled by this checkpoint.

Offline385 review localizes the eight b4 refusals to one pooled DEFAULT material
view, main144..147 and peer322..325: COM offset117248, reflected228/native240/
descriptor256 bytes.425 other successful input occurrences use the same pool
resource. All eight original GPU snapshots agree, but there is no interval-
qualified CPU copy/cache receipt for this view. No historical GPU payload was
promoted into a candidate. The exact loss cause remains unknown until observed.

## All Source CBVs Captured (2026-10-06)

Run `build/single-pass-20261006/transient-writer-389/summary.json` passes21/21
and repeat with zero CBV inputs deferred.1920 DEFAULT,354 direct UPLOAD and514
completed common-writer UPLOAD inputs reach actual original-queue validation
and GPU readback. All2788 CBVs are private owning copies,1607380 logical bytes
in the primary capture. All354 binding plans are Ready with zero missing CBVs.
All438 selector inputs and167 t5 reads are covered/matched in this scene, with
no source candidates fabricated from earlier GPU captures. The12 color/depth/
stencil comparison planes remain equal; native draws are still not suppressed.

`ForwardBufUpload` observes the original1F088C invocation around its existing
hook. Its new1F0BF7 origin must match the actual handle, mappedR15 destination,
creation/writer/source recording and OS-unwound caller chain. Closure is
published only after normal return past the completed1F08C2 memcpy.1F0A28's
finish does not return the mapped pointer.220068 and ambiguous/nested/abnormal
writers remain unqualified. A review found an invalid nested-attempt early
return which did not poison the outer scope; that is repaired after389 and
covered by zero-size/oversize/invalid-context fixtures.

Only30 ready CPU assemblies fit the current1000000 descriptor-allocation cap.
The354 plans preserve14392467 original shader table coordinates. They are not
that many distinct selected resource slots; immutable table sharing is the next
step before consuming a private bank. No bank is created/bound, no whole-input
draw comparison has run, and no total FPS improvement is claimed.

The new exact-range generic cache diagnostic observes PRESENT coverage and
invalidation/eviction labels without reviving historical bytes.389's438 inputs
are fully covered; this does not prove the earlier eight-view cache-loss cause.
160 C++/GPU tests and660 Python tests pass before the nesting repair's rerun.

Run391 verifies the nesting repair:21/21 plus repeat,868 UPLOAD copies and no
CBV inputs deferred. Normal392 passes9/9 and closes without crash/reporter.

## Immutable CPU Table Reuse (2026-10-06)

Run `build/single-pass-20261006/assembly-cache-394/summary.json` passes21/21
with repeat. Exact immutable descriptor backing/layout/CBV views/physical owners
are interned independently of current source copy/epoch leases. Every hit still
returns that command's own leases; no readiness or source scope is borrowed.
The allocation cap remains1000000 descriptors and failed transactions publish
no cache/output/counter changes.

Primary/repeat both have334 Ready plans,288 CPU assemblies,984676 unique
descriptor positions,2001 unique CPU tables,1363 hits. All868 UPLOAD CBVs and
1920 DEFAULT CBVs still copy/read back with zero deferred inputs. Original
coordinate total13738392 is not actual allocation size. No bank is consumed.
Installed394 DLL:
`9A5540ED9C87E46CBA383471956853AA6AAE1E2AFE2EA4EC5C33A0D2D20C042E`.
Normal395 passes9/9, no crash/reporter, all controls0 and closed game.
160 C++/GPU and660 Python tests pass after the nesting repair and CPU cache.

The remaining large-table allocation cost is real:420 rows represent44 distinct
selected layouts, with1431064 descriptor extents; no exposed equal descriptor/
offset signature maps to different captured owners/heaps.30 built large heaps
use978610 positions,1971 small heaps use6066.14 unbuilt large keys need452454
additional positions. Union tables must be proved conflict-free before changing
the current original-coordinate/null-hole contract; raising the cap is not an
optimization.

Twenty b4 candidates in394 are currently wholly uncovered, on two228-byte
spans at offsets28160 and26624 of one262144-byte pooled DEFAULT resource.
The same resource provides413 successful inputs; all queries show490 present
ranges,118120 live payload bytes, no full invalidation,654 range invalidations
and61 generic evictions. Those counters do not identify an eviction of these
specific spans. Exact write/eviction-span evidence remains missing; no policy
change or historical candidate reuse is justified by these counters alone.

## Pass Union And Private Structured Views (2026-10-06)

`AssemblePass` now constructs conflict-partitioned sparse SRV unions only inside
the same held original root/heap/base/parameter domain. Equality includes every
canonical descriptor word, not just the visible register or capture label.
Each source retains its own admitted count, selected offsets and copy leases;
all union contributor owners survive. Allocation is transactional and the cap
stays1000000. CPU tables are now assembled in a second pass after all per-source
plans are built. This is descriptor preparation, not shared scene traversal.

`StructuredSnapshotView` maps each completed private dense t5 copy into a
disjoint padded span while preserving original shader record indices and the
8192-element/128-byte SRV domain. No original-source copy, shader-index rewrite
or historical byte reuse occurs. Copies wait for their actual source fences;
first consumption must follow copies on the exact private recording. A later
consumer needs an explicit completed destination fence and retained owners.
Native submission/finalization/B4/record-path contracts remain caller supplied.

`union-view-tests-397.log` passes math and WARP GPU tests2/2. Real shader reads
indices3412..3427 from two independent versions, including nonzero data and
selector fields; GetDimensions returns8192/128. This is an owned standalone
fixture, not evidence of native whole-input consumption or a total FPS gain.

`QueryCacheEvents` exports bounded64-event metadata per registered owner, with
the exact query span and separate publication/invalidation/eviction receipts.
Dropped history remains unknown. `cache-events-tests-397.log` passes its focused
C++ suite; `union-events-python-397.log` passes666 Python tests. Policy and
current-byte eligibility are unchanged. Native union/bank verification follows
after all contributing modules are frozen and rebuilt.

Run `union-bank-400/summary.json` passes21 existing harness checks and repeat,
without crash/reporter, then closes the game. All354 plans are Ready and CPU
assembled;2788 CBVs are owning copies (1920 DEFAULT,868 UPLOAD), zero deferred.
CPU storage is104687 unique descriptor positions/2523 tables rather than
14394861 original source-coordinate positions. All12 color/depth/stencil
comparison planes remain equal. Actual shader-visible bank creation returns
E_INVALIDARG, so400 does NOT prove bank readiness or a working whole-input draw.

The follow-up changes share ALL exact immutable CPU table storage, including
finite sampler tables, with original root/parameter included in cache identity.
Current source counts/access and copy leases remain separately validated. The
native400 sampler-like parameters29/30 total2832/2216 positions but have only4/3
unique CPU heaps; duplicating finite tables can exceed the2048 sampler cap even
after sparse resource unions fit. The next run must prove the actual cause and
successful bank creation; the cap is unchanged. A per-instance rejection record
now exposes the exact validation stage/source/parameter/reason.

`finite-consumer-tests-401.log` passes164 C++/GPU tests and
`finite-bank-python-401.log` passes666 Python tests. The new hardware fixture
uses Build/AssemblePass/CreateSources/RecordSource with direct indexed and pure
indexed-indirect draws. Completed private DEFAULT CBVs produce17/29 while fresh
original controls remain9; private-only mutation changes output, and slot7
firstInstance7/11 and actual fence-retained owners are verified. The separate t5
fixture changes only private views and checks a freshly assembled original
control after replacement. These are owned fixtures, not native replay permits.

`--retained-source-bank` now requires an actually created bank for the complete
current source occurrence set in both primary and repeat captures. Missing
plans, failed heap creation and native consumption cannot pass this criterion.

Run402 actually creates both primary/repeat354-source banks (104557 resource
descriptors plus72 samplers), but the overall harness result is false because
the new check was inserted at the wrong planned position. No crash, reporter
or render mismatch occurred. That failed result is preserved. The plan order
is repaired and covered by two pure regression tests.

Run `finite-bank-403/summary.json` then passes22/22 with the strict bank check
and repeat. Both captures have354 Ready/CPU-assembled/bank-created sources,
104610 actual resource descriptors and104682 unique CPU positions/2523 tables.
All12 comparison planes remain equal; bank consumption/execution/suppression
remain zero. Installed401 DLL:
`D6B8B1ED8D62D10622849DEE961D74AB8D80C13384C473A52F61CC096EB556EB`.
The game is closed with no crash/reporter and preview restored. Python suite
now passes668 tests. This proves descriptor-bank preparation for this source
set, not native whole-input equivalence, shared CPU traversal or an FPS gain.

The next real t5 integration requires an immutable finite slot5 association,
not a later comparison with the live descriptor: finite capture precedes SRV
resolution and may see another descriptor version. It also needs held successful
source queues/signaled values, an exact B4 private-resource/copy/blob join and
a one-shot asynchronous overlay copy stage before assembly. Later original
SRV/IA reads still require their own read/state/allocation/producer contracts.

Normal404 passes9/9 with all controls0. No crash/reporter, game closed, preview
restored. Installed401 DLL and GameUserSettings/PluginUserSettings/vrport.ini/
launcher settings hashes still match the deployment manifest; calibration is
absent as before. No framegen, quality reduction or async-compute change was made.

## Native Private T5 Overlay (2026-10-06)

`MatchesCapturedSourceSrv` now validates the actual immutable finite-capture
receipt, full canonical slot value and held cookie/resource association. It
does not consult later live descriptor metadata. Actual capture receipts retain
the original interned heap/owner but keep per-capture p/count/base/heap labels
separate, including when identical tables are reused in different domains.

Native overlay preparation joins the exact private PS b4(19/0) resource/view/
offset/owner/copy receipt to the completed GPU blob and original occurrence.
The source queue and successfully signaled value are now held explicitly;
failed source Signal cannot publish a submitted certificate. Original payload
readback happens once. The new destination list copies only completed private
dense t5 spans, then returns until its own fence completes. Readback of those
new spans is compared with each exact original owning snapshot before assembly.

An independent review found an ABBA risk in calling the game's hooked Execute
while holding the retained mutex. This path now uses the mirror's existing
original-queue submission route for private-only buffers, with explicit
CommandResources flight retention and fence signaling. It does not enter the
game image/framegen/native callbacks, reset/replay its copy list, or grant a
future original-resource read epoch.

Run `t5-overlay-406/summary.json` passes22/22 with repeat. Both captures contain
167 actual private t5 versions,342016 copied bytes in a1388544-byte backing,
167 destination-fence/GPU-byte matches and167 successfully assembled overrides.
All354 source banks are Ready with105643 actual resource descriptors; all12
color/depth/stencil reference planes remain equal. Native bank draw consumption,
execution and suppression remain zero. Installed405 DLL:
`F51AFEA19992E3096E17E7823543CD352E5DCA548FE2BDEBF39D42F14D2AB9F0`.
The test closes the game with no crash/reporter and restores preview settings.

`t5-overlay-tests-405.log` passes164 C++/GPU tests; Python suite passes677 tests.
These prove real native private-copy/binding preparation, not a whole-input
shader draw, shared CPU preparation, permanent single-pass or an FPS increase.

Normal407 passes9/9 with all controls0, no crash/reporter and closed game.
Installed405 DLL and all four settings hashes still match deployment; preview
is restored. The next consumer must cover its actual IA and remaining finite/
selected SRV/UAV inputs, not extend their original source epochs by assumption.

## Paired Shared Inputs And IA Admission (2026-10-06)

StereoGeometryPass now partitions shader uses between exact paired CBV/SRV
bindings, original shared descriptor domains and preserved static samplers.
Qualified unbounded SRV selections require both actual native-side scopes,
full family keys, selected original offsets and a mandatory owning caller
validator. CBC bytes, SRV views/references, tables, selectors and resource
contracts are copied into retained storage; no destination-list COM cycle.

The hardware fixture compares two original draws with both view-instanced
array layers, indices5000/6000, sampler13/static sampler, per-eye SRVs and the
byte844 tail of the848-byte CBV. It detects swapped layers, proof loss, malformed
coverage, state/view changes and blocked-flight lifetime errors. This is one
synthetic draw, not production shader coverage or native suppression/FPS proof.

Independent review caught and repaired paired-SRV state/contract mismatch and
same-COM distinct-handle IA guard asymmetry. The original IA coordinate fixture
had assumed SV_VertexID included baseVertex; it now uses actual POSITION fetch
and copies that buffer too, proving firstIndex2/baseVertex-4/firstInstance7 and
private immutability after the fixture's original upload mutation.
`ia-shared-tests-410.log` passes167 C++/GPU tests;680 Python tests pass.

NativeIaSnapshot observes full consumed IA views and prepares only whole
admitted sets at the exact original callback. Known committed fixed1/tracked0/3
copy paths pin/revalidate creation/allocation/allocator, native B0/context and
recording; mutation uses guarded register/flush with verified cumulative recovery.
Full original view coordinates and draw arguments remain unchanged. Unsupported
pool/placed/UPLOAD/protected/unknown views are deferred, not privately rendered.

Native411 collects1708 IA views over354 sources:300 individually admitted,
306 placed-deferred,264 UPLOAD-deferred and838 partial-backing/pool-deferred.
Every source has an unsupported view, so there are ZERO complete IA copies.
The strict whole-bank harness fails because8 dynamic selector inputs are missing
in this capture (346 Ready sources); that failure is preserved, not relaxed.
No crash/reporter, game closed and preview restored. Normal412 passes9/9 with
all controls0; DLL and all settings hashes match deployment. Installed410 DLL:
`F0892F8500A5164DBFF68A30AF67E6830A4FD2047CAD9C014C7ADF3A4EB35E4C`.

The large geometry pools are NOT the512-byte CBV pool: fixed1/lowType5 uses5
size classes at game+314AB00 and registry+13BC548+24*class. Actual packed class/
group/slot, entry resource/owner/occupancy and pinned owner association must be
read before admitting partial native ranges. Source14's geometry fits its native
logical range but VB7 is still UPLOAD. Pool support could cover82 whole sets;
their full6MiB instance views also require a measured capacity/span decision.
Kind5 UPLOAD VB7 has no currently proved writer/publication/no-overwrite contract.
Bounds or GENERIC_READ do not supply that proof.

## Geometry Pools, Array Inputs And Instance Writer (2026-10-06)

NativeIaSnapshot now admits the actual fixed1/lowType5 geometry-pool path.
It reads the native packed class/group/slot, five-class table, group header,
entry resource/allocation owner and occupied bit twice. Typed allocation and
allocator pins are revalidated before recording; the live slot association is
checked at the original callback, while submission checks the retained physical
allocation. The guarded native functions are AE7CD0/1F7EB8/220110/21CEC0.
Observation remains64Q: flag4096 denotes this path; word46 is its actual packed
identity. Full view extents and original draw coordinates are unchanged.

Native416 passes22/22 with a repeated capture. Both captures have354 Ready
source banks and105652 resource descriptors,838 actual pooled IA observations,
20 recorded/finalized/submitted complete IA sets,138 private views and126467464
copied bytes. Remaining observations:306 placed,264 UPLOAD and62 capacity
deferrals. Each eye still has a bounded64MiB arena; it was not enlarged to hide
the full6MiB-per-draw instance-copy cost. No native IA GPU-byte comparison,
private IA shader consumption, bank consumption or native suppression is claimed.

Small actually observed copies <=256B now have a separate8192-record retention
class; ordinary copies keep512. The shared2MiB-per-resource/32MiB-global/payload
budgets and overwrite/invalidate semantics remain. This preserves current228B
selector candidates against unrelated small-copy pressure; it does not classify
them as CBVs or substitute for private GPU-byte matching. The8 missing inputs
from411 are absent in416: all354 source banks genuinely exist. The old64-event
ring cannot establish the precise historical reason those8 inputs disappeared.

NativeTextureRead now supports finite2D array view ranges and selected typed
formats observed in the capture. It preserves mip+arraySlice*resourceMipLevels,
uses device copy footprints, rejects planes/typeless/MSAA/3D/write-capable shapes,
and keeps original creation/read/submit checks. Multi-subresource resources require
a uniform tracked state; a single tracked subresource is not treated as the whole
array. Typed PS-only shader reads are supported. Hardware/WARP fixtures verify
4 original-index subresources, exact copied GPU bytes,12 shader samples and source
mutation isolation. The hardware paired-pass fixture also includes finite t67
Texture2DArray with FirstArraySlice1 alongside the5000/6000 bindless selections;
both view-instanced layers exactly match2 original draws.
Index/layout references: [D3D12CalcSubresource](https://learn.microsoft.com/en-us/windows/win32/direct3d12/d3d12calcsubresource),
[GetCopyableFootprints](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getcopyablefootprints).

Kind5 VB7 is now identified as the real instance rings:87C0EC/87C250 create
renderer+40h with20000h*48=6MiB and renderer+C0h with8000h*64=2MiB. Writer1F3E40
finishes the mapped copy and returns firstInstance; normal/skin return sites are
1F2039/1F026B. Reservation95B308 may wrap without checking retirement.
209332->20A778->20A7DC publishes the16B {stamp,absoluteHead}; retiredTail advances
only after1FDCAC reports completion, using the stamp from1F7CF4. NativeInstanceRing
adds a bounded, tested CPU-version ledger with separate producer/source scopes,
immutable completed payloads and explicit reuse/publication handling. It does
not install native hooks or grant allocation, no-overwrite, GPU copy or submission
permission. Hooking the actual reservation and joining the source IA epoch remains.

First launch415 failed before any render tests on HUD readiness and is preserved.
Repeated polling of the same still-fresh HUD source no longer discards valid
progress: duplicate samples are not counted, and the existing300ms freshness,
350ms stall,1s duration,30 successful submissions and5 progressing-sample gates
remain. Frozen, stale, regressing, torn or unsubmitted HUD cannot pass.
Normal417 passes9/9 with controls0, no crash/reporter, closed game and restored
preview. Final C++/GPU suite418 passes170/170; Python suite passes684 tests.

This is preparation for whole-input native paired rendering, not a permanent
single-pass implementation. Common CPU traversal/sorting/preparation, elimination
of the second native pass, whole-scene shader/material coverage and total FPS
improvement remain unproven and required.

Final main build418 includes the unused, no-hook instance ledger and succeeds.
Normal419 passes9/9 with all controls0, no crash/reporter, closed game and restored
preview. Installed418 DLL and PDB plus all four settings hashes match deployment:
DLL `E050795063A2091C5E7CB267C06BF5F24F1E5FC4FF30E4163122B132F65CFD11`;
PDB `62AADE908D98E6BCD55DC8278BE34D111A702AF7C9D6E3E26646AF5C2B8ED0F4`.
The complete354-bank/IA-copy native416 evidence remains associated with414 DLL,
not relabeled as a418 capture. Subsequent native writer integration needs a fresh
instrumented launch with genuine reservation and source-epoch joins.

## Placed IA And Actual Instance Observer (2026-10-06)

NativeIaSnapshot now pins and verifies placed DEFAULT buffers through the actual
native GetOffset21FF3C and heap getter21FF18. The opaque allocation handle is
never used as the heap offset. Native metadata identity, two getter results,
held placement/heap/allocation span, protection and current source read state
are checked before copying and again at source submission.64Q stays unchanged:
flag8192 denotes placement; word46 is the actual getter offset. Pool4096 and
placed8192 are mutually exclusive. Native421/423 observe306 verified placed
bindings. Capacity remains64MiB/eye;20 complete IA sets are copied.

InstanceSnapshotView uses the genuine retained PSO creation snapshot and full
slot7 input layout with PER_INSTANCE/step1. Original firstInstance/count,48/64
stride and6MiB/2MiB view extents stay unchanged. Sparse private views retain
original index coordinates while only copying the selected instance records.
WARP/hardware tests cover two immutable versions, unchanged indexed draw
arguments, source mutation, view bounds, PSO identity, fence ownership and
later-consumer completion. This alone grants no native epoch or GPU-copy permit.

The real1F3E40 writer,95B308 reserver and20A7DC publisher entry hooks are guarded
and installed, with explicit installed-mask7 and default-disabled observation.
Actual source queries precede IA relocation and retain32Q producer observations,
owning CPU candidates and typed backing owners. Retained exports are48Q rows
plus a bounded16MiB CPU blob. Submission validation remains provisional and
does not infer future no-overwrite or original GPU-byte equality. A null failed
backing capture was fixed before the first deployment. Explicit idle reset
invalidates old observation receipts without changing native ring generation.

Native421 and423 pass22/22 with repeat, all354 banks Ready and no crash/reporter;
the game is closed and preview restored after each run. They do NOT prove a
native instance producer join:270 queries return Changed and ZERO CPU candidates.
Native423's first-loss export is5, i.e. Contention+1. The observer's global gate
around real native calls rejects parallel writers even after ignoring stride160
before the lock. This is a failed approach for concurrent native witnessing,
not a reason to serialize the renderer or relax admission.174 C++/GPU tests and
688 Python checks pass; these narrower results do not override the native failure.

The verified next replacement is a genuine successful-CAS edge witness at95B33E.
After `cmp r9,rax; jne95B312`, the5-byte instruction is
`48 8B 5C 24 08` (restore caller RBX). At this exact edge: R10=ring,R9=old absolute
head,R11=absolute reserved begin,RCX=absolute end,R8D=physical first,EBX=count,
RAX=CAS-observed old head. A preserving thunk must capture EBX before restoring
RBX from nativeRSP+8, preserve flags/full enabled XSTATE/GPRs, then resume95B343.
This supplies actual concurrent reservation identity without a later-head
inference or a native-call lock. The replacement is not yet installed or verified.

Normal424 passes9/9 with controls0, no crash/reporter, closed game and restored
preview. Installed422 DLL/PDB and all four settings hashes match deployment:
DLL `FD2C4A043F0B40A450215E7EBB93BF1EFE4F58E9A3DBEFD552E26D20B4EF8F04`;
PDB `2753952B8954E71BBC1670C72013721D07367DA6CBC5673A4ACE0055B1EBB58B`.
No commit, push, native suppression or FPS increase is claimed.

## Successful CAS Witness (2026-10-06)

The95B33E success-edge CALL is now implemented and installed by a separate
NativeInstanceWitness service. It guards the exact81-byte allocator body,
retains a near relay and thunk module, suspends/checks/resumes other threads
for the5-byte patch, restores page protection and verifies current code.
The ordinary95B308 entry detour was removed; installed-mask bit2 comes from
the actual verified witness. Native writer calls run outside the metadata lock.

The independent ABI fixture compares the displaced MOV against active/passive
thunks with all GPRs, FLAGS, x87/MXCSR/XMM/upper YMM and OS-enabled XSAVE state.
It tests wrapped/nonwrapped reservations, disabled/not-ready paths, the native
physical firstInstance return and Windows virtual unwind. It found an epilogue
dependency on callback-clobbered RBP, repaired before deployment. The active
frame is4120h and nativeStack=frameBase+4138h. The patch-plan fixture guards
the real bytes and signed CALL displacement limits. Deterministic two-thread
tests cover reversed events, disjoint writers, unknown pending, ABA, local
recovery and retained-byte budgets. Full suite425 passes177 C++/GPU tests.

Review also identified two live scheduling failures. Sampling now refreshes
the native head/tail after reacquiring the metadata lock. A returned writer can
discard a qualified provisional sample without quarantining unrelated pending
writers; the source range was invalidated before its actual native write.
Fresh reservations replace tickets removed by local recovery. A regression
models one writer returning before its peer has reached the reservation event.

Native426 on425 DLL obtains8 actual completed writer candidates/512 CPU bytes,
all stride64, with firstObserverLoss0. This is the first nonzero real producer
capture. All8 fail later source-submission validation, so no GPU-copy permission
is assigned. The strict whole-bank gate fails:342/354 are ready, with12 missing
VS B4 selector candidates. Failed426 is preserved. No crash/reporter; game closed.

Build427 removes an unnecessary pending-GPU-barrier restriction from the CPU
writer scope while retaining genuine native recording validation, and exports
bounded local-rejection counters. Native428 crashes before observer activation
during world startup: EXE+1DB1EC loads[RCX+58h] with RCX=70000h. Available stack
scan entries are engine resource lookup calls (a scan, not an unwind); the dump
does not contain the observer globals. This does not establish a causal link
or prove a random crash. CrashReporter was closed by the harness;429 repeats
the identical installed427 build to continue diagnosis.

Native429 on the identical427 build passes22/22 with repeat and no crash/reporter.
The repeated capture has354 Ready banks,134 real CPU writer candidates/7824B,
127 valid at original submission and firstObserverLoss0. The earlier capture
had353 Ready banks and62 candidates/20 submission validations; the successful
whole-bank check is specifically the repeated capture. This does not prove the
prior startup crash was random or that CPU samples match eventual GPU inputs.

Completed writer sampling now handles unknown cached cursor ranges explicitly.
At the guarded cached-write branch, a physical interval can be invalidated even
without an earlier absolute reservation receipt. A pending token blocks queries
during the actual write; its return invalidates the interval again, retaining
overlap conflicts against in-flight sampling, then discards only that token.
It never creates a candidate. Known disjoint versions and other writer tickets
survive. A regression tests an unknown cached write racing a known sample.
Publication while a writer is pending no longer quarantines the ring; that
publication is simply unavailable. Genuine physical-backing failures still reject.

Native431 obtains132 candidates/7792B,36 submission-valid,0 global loss; local
cache rejections drop to454. Its strict source-bank check fails350/354. Native433
obtains178 candidates/10096B,141 submission-valid,0 loss and188 cache rejections,
but banks fall to89 Ready during an experimental ordinary-cache cap increase
from512 to2048. Evidence shows substantial cache eviction; broader shared-budget
pressure is the working explanation, not a demonstrated cause for each missing
input. The cap increase was reverted completely in434; ordinary512/small8192 and
the original2MiB/32MiB/payload budgets remain. Failed431/433 are retained as such.
All177 C++/GPU tests passed432; focused affected tests and688 Python checks pass434.

Native435 on434 DLL restores354/354 Ready banks in both captures. Initial
capture:235 CPU writer candidates/12688B,191 valid at original submission,
firstObserverLoss0. Repeat:248 candidates/13264B,127 valid at submission,loss0.
The lower submission-valid counts are explicit and do not become permission for
the other candidates. These are CPU producer/source joins only. No native GPU
byte comparison, compact VB shader consumption, shared CPU scene preparation,
native second-pass suppression or measured total FPS improvement is established.

Native435 completes22/22, then normal436 completes9/9 with all observer/capture
controls0. Both runs close the game, restore preview and report no crash/reporter.
Installed434 DLL/PDB and all four settings hashes still match deployment:
DLL `94E291D371DA763EE6E474A02FA0FCB7AC3D2219AF7FF85ECDD15FDF0D0EFC5C`;
PDB `D62E1A163852FB5D7C5492DACB7DE35DC4A2578F780AEDC7451082B97485D343`.

## Native Instance GPU Comparison (2026-10-06)

RecordGpuProbe rejoins the opaque completed CPU candidate to the current original
direct draw, PSO/root, VB7/native backing, recording and graph parent. It records
a selected-range UPLOAD-to-READBACK copy with a zero-initialized per-source
predicate. Before original submission, only ValidateSource-approved candidates
enable their predicate. The caller's native queue signal/fence closes readback;
unsubmitted or rejected rows cannot publish GPU bytes. Predication is restored
after every copy. The official D3D12 predication contract explicitly includes
CopyBufferRegion and UPLOAD predicates in GENERIC_READ's predication read mask:
[Predication](https://learn.microsoft.com/en-us/windows/win32/direct3d12/predication).

The actual command block passes WARP and hardware tests for zero/one predicates,
late pre-submission finalization, exact selected source bytes and restoration
before the next unpredicated command. InstanceGpu24Q exports bind the original
CPU owner/blob, native list/generation/occurrence, selected range, predicate slot,
readback and actual source fence. Python independently compares captured bytes.
This remains a diagnostic original-position read, not future original-resource
replay permission or proof of whole-input equivalence.

Native438 did not close its first source stream. Recording D3D commands while
holding the observer mutex was removed; the predicate retains submission-time
validation. Native440 failed in QuickBoot's bounded Continue handling before
render tests. Native442 passes22/22: initial169/169 submitted GPU samples match
(9264B); repeat234/234 match(12640B),0 mismatches and354 Ready banks. All failed
launches remain recorded.179 C++/GPU tests and693 Python tests pass with the new
GPU/overlay report validation (the overlay itself was not native-verified yet).

PrepareInstanceOverlay now builds compact private VB versions only from matched,
fence-completed original GPU payloads and genuine PSO snapshots. Original draw
coordinates and full logical view domains are preserved; selected payloads are
uploaded once and independently read back from the private GPU buffer. Native
444/446 exposed an E_INVALIDARG caused by an outer internal-command scope:
shader_snapshot::Capture intentionally rejects lookup in that scope. Build447
restricts that scope to resource creation and actual copy commands, retaining
the normal PSO qualification path. Private instance views are stored for the
next geometry consumer; native draw consumption/suppression remains zero.

Native448 then identified a second internal-scope boundary: closing the private
list invoked the original-render BeforeClose hook while the retained mutex was
already held. Build449 scopes private Close/submission as internal, matching
the completed t5 copy route. Native450 passes22/22 including repeat:

- Initial:354 Ready banks;177 original GPU samples/9888B, all match;177 compact
  private VB versions,9888 actual uploaded bytes,7982592B backing. Private GPU
  readback also matches all original selected GPU bytes.
- Repeat:354 Ready banks;120 original GPU samples/6176B, all match;120 private
  VB versions,6176 uploaded bytes,6291456B backing; all private bytes match.

The full view domain is preserved despite sparse uploaded records. Both captures
have0 original/private byte mismatches, unchanged original reference planes,
no crash/reporter and restored preview after closing the game. Full suite443
passed179 C++/GPU tests; affected tests pass449 and693 Python tests pass. Neither
private view creation nor byte equality proves that the native paired shader
consumer has used these views; consumption remains explicitly false.

Normal451 passes9/9 with diagnostic controls0. Game closed, no crash/reporter,
preview restored; installed449 DLL/PDB and all four settings hashes match:
DLL `F9A270E812EBB29E0379E1CDB9E01174785EC6AAADD0FABEA2AFBD6C13F42945`;
PDB `D6F387BC78166743A1CF25435E651A6A69855BE54CB5330807A3FCE60787A3CA`.
The active objective still requires the native paired geometry consumer,
remaining inputs, common CPU preparation, second-pass elimination and a measured
whole-frame improvement. No commit/push or completion claim was made.

## Joined Private Geometry And Copy Coalescing (2026-10-06)

IA capture may now explicitly separate slot7 when its actual original-position
instance GPU probe was recorded. The full original PSO mask/current bindings
remain validated. Selection5Q preserves original/copied/separate masks; no other
slot may be omitted. After both source and private-copy fences complete, only a
GPU-matched instance overlay can join the exact mesh/index copies into a complete
private geometry input. PrivateGeometry16Q exports owner/scope/draw joins, with
native draw count and permission still zero. Indirect arguments are not inferred.

Native453 creates88 complete private geometry sets(80 compact-instance joins),
but the strict overall run fails with352/354 banks Ready. Full views within one
source draw often overlap in the same COM buffer. PlanCopies now unions only
overlapping/touching ranges for that actual occurrence, preserves every original
view displacement/alignment, and retains every original allocation/state proof.
It never bridges unread gaps or shares data across source occurrences. The GPU
fixture compares all relocated view bytes, including non16-byte-aligned offsets;
the report checks shared ranges map to the same original resource bytes.

Native455 records126 IA sets versus89 in453 within the unchanged64MiB/eye cap.
Logical view extents total182584242B; the actual copied union is134149126B,
removing48435116B of duplicate copies in that capture.81 sets form complete
private geometry(63 compact instance joins). Remaining large copies include
full6MiB slot7 from a different DEFAULT class; no unobserved tight range or
cross-draw epoch has been assumed. Run455 is preserved as failed because only
341/354 source banks are Ready. This is not an FPS measurement. Full C++/GPU
suite454 passes179/179;697 Python tests pass. Native shader draw consumption,
full material resources and common CPU preparation remain next requirements.

Normal456 passes9/9 with controls0; no crash/reporter, closed game and restored
preview. Installed454 DLL/PDB and all four settings hashes match deployment:
DLL `A41BB69478124732EEF602724F6706B331DCE68E7334336E743D668F18C58BFF`;
PDB `8F70B470A28BD4FDA891F1F3FE20E915E9806936094B2BACE242BA50E14BCF4C`.

## Bounded Instance Cache Liveness (2026-10-06)

The generic-copy fragment-retention experiment457 passed179 C++/GPU and697
Python tests, but live458 regressed to4/354 Ready source banks and no captured
instance candidates. The experiment is fully reverted, including its remaining
GenericFragmentBounds test. Failed458 artifacts are retained, not reclassified.

The instance observer had a separate liveness failure: its duplicate writer
inventory never followed ledger retirement, and reaching4096 completed ledger
ranges permanently exhausted the ledger until reset. Complete now evicts only
completed cache entries under range/byte pressure. A returned sample that cannot
fit because external immutable candidates retain the budget is dropped without
forgetting pending writers or permanently poisoning the ledger. Real overlapping
writes still invalidate old provenance; eviction never makes a future native
read legal. A weak completion-membership receipt expires independently of held
CPU payloads, so the observer prunes exactly removed ledger entries. The unused
append-only reservation inventory is removed; actual reservation receipts remain
in active calls, cached cursors and completed writers. Retired/exhausted cursors
are pruned. None of the configured cache capacities were increased.

Tests cover10000 sequential completions without tail advance/reset, bounded
membership, external payload ownership, byte-budget recovery, pending writers,
retirement and quarantine. Build459 passes179/179 C++/GPU tests and697 Python
tests. Live460 initial capture records12501 completed writers with FirstLoss0,
260/270 source CPU candidates,260/260 matching original GPU samples(13984B),
and260 matching private instance versions. It assembles354/354 descriptor banks
and152 complete private geometry inputs, including136 compact-instance joins.
This remains input preparation: native private shader draw consumption is zero.

Live460 is an overall failure: repeat capture stops after13 peer draws with
retained reason1060(source-CBV snapshot Allocation), before a complete source
stream. No crash occurred; the harness closed the game by its bounded shutdown
fallback and restored preview. A new failed-source evidence collector preserves
bounded CBV/detail/copy-command buffers before strict analysis rejects the partial
stream. It checks ABI, extents and stable repeated reads and makes no verified
execution claim. Python suite461 passes701 tests including that collector.

Live462 crashes during startup before any test completes:
`Cyberpunk2077-20261006-234017-13472-25504`, EXE+0xB8084B, null read after loading
the pointed-to object, called from EXE+0xFF3B8's release path. The archived459
DLL/PDB were used to parse the dump. No plugin frames appear in the stack scan;
observer globals are absent from captured dump memory. This does not establish
causality or prove the crash unrelated. IDA exports and dump analysis are saved
under build/single-pass-20261006/crash-462-* and instance-liveness-crash-462*.
CrashReporter was closed, the game is gone, and preview was restored.

Deployed459 DLL/PDB, with all four settings hashes unchanged at deployment:
DLL `50846B9A900AE929E1F09727E8ADAE9C51495EF8A6DF50E4E039043DE4A50A0F`;
PDB `22B3B674B2A7CF2B72086CE83BB01E6FC5B8EDC232BE6A060A8B9BF88A794471`.
Framegen remains off. Full native paired consumption, material lifetime coverage,
common CPU preparation, second-pass elimination and total-FPS improvement remain
unproven requirements of the active objective.

Live463 starts successfully on the same459 binary. Initial capture again has
354/354 Ready banks,13031 completed instance writers,FirstLoss0,252/270 CPU
candidates(13584B),252 original GPU matches and252 matching private versions.
145 complete private geometry inputs include129 compact-instance joins. Repeat
capture times out in the preparation recorder's8-second wait before detailed
source analysis. This is a separate overall failed run, not a successful repeat
or evidence of a crash. The harness restores controls/preview and closes the
game; no crash/reporter appears. The new failure collector was not exercised by
this timeout because the retained failed state was not reached/read.

Normal464 passes9/9 with diagnostic controls disabled: stable submitted HUD,
both-eye images, repeated packet capture and crash stability. No crash/reporter,
game closed using the bounded shutdown fallback, preview restored. The strict
repeat capture remains unresolved; the latest initial-capture successes do not
prove repeat stability or whole-scene single-pass. Next work must distinguish
the actual CBV allocation rejection1060 from the later recorder timeout, then
continue to native paired shader consumption of the owned geometry/materials.

## Material Cache And Allocation Sampling (2026-10-07)

Run466 stopped at337/354 Ready banks:17 missing inputs belonged to292-byte
material B4 copies. Their committed512-byte-slot resource had only117912B of
current payload but59 generic evictions. The64-event history had already dropped
the exact missing-slot event, so the specific historical eviction is not proved.
Copies of292B exceeded the old256B small-copy class and competed in the512-entry
ordinary queue. The small-copy threshold now includes512B native pool slots;
the2MiB/resource,32MiB/global and8192-small-record limits are unchanged. A1024-slot
292B regression verifies retention, exact overwrite invalidation and held bytes.
Ordinary-limit fixtures now use disjoint1024B spacing for their513B copies.

Threshold-only468 passed179 C++/GPU and701 Python tests. Run469 stopped at the
post-load Continue prompt, with no crash. Run470 reached rendering but had only
98/354 Ready banks; missing116B and292B records shared an owner with430 evictions
and31020B remaining. Global pressure previously evicted oldest generic copies
without distinguishing small constants from ordinary copies. Build471 first
evicts ordinary copies from owners without structured coverage, then small
copies of those owners, retaining the existing structured-data priority and all
hard bounds. A full32MiB pressure fixture verifies that old292B material bytes
survive while large-copy admission and actual overwrite invalidation still work.
All179 C++/GPU tests pass; no FPS benefit is claimed for this diagnostic cache.

Run472 timed out in the8-second preparation observation window. Captured state
was Request0/State1, Failure0,3130 records and64358 packets, while both native
eyes had sealed/submitted all176/178 draws with retained reason0. This was not
proof of process exit or renderer failure. The reader now waits on the same live
process with progress tracking, a20-second idle limit and60-second total limit.
After8 seconds it records a slow observation and may capture a thread minidump
without terminating the process. The live-dump fixture proves target survival,
MDMP/thread parsing, no invented exception and refusal to overwrite an artifact.
Thread output is explicitly a bounded stack scan, not an unwind. The dumper uses
the [external-process MiniDumpWriteDump API](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump)
with thread/context data, not full private-memory capture.708 Python tests pass.

Run474 initial capture passed in7.9095 seconds:354/354 banks,263 original GPU
instance matches,146 complete private geometry inputs. Repeat capture completed
its recorder in5.9378 seconds but hit actual CBV allocation failure1060 after21
peer draws. The failure collector saved1533 CBV/detail/snapshot rows and198 copy
commands. All five original detail observations for failing source197 had valid
allocation associations; the global allocator's sampled reference count was1518.
Failure artifacts stay unverified and never grant execution permission.

The shared allocation reader double-read whole allocation/allocator headers,
including their mutable reference counter. IDA exports allocation-addref.c and
allocation-release.c confirm native13-byte AddRef at+18E9720 increments only the
DWORD at object+8; Release at+8780A4 decrements that DWORD and destroys at zero.
RefCountedBytes now requires both counts to be live/in-range, compares every
other byte, and returns the actual second sample. Record/context/alias readers
remain unchanged. This removes a demonstrated false rejection under concurrent
AddRef/Release without ignoring vtable, Pimpl, resource, allocation or heap changes.
Tests include concurrent increments/decrements, final Release, overflow, changed
vtable/padding/Pimpl/resource and unchanged source ownership checks. Build475
passes179/179 C++/GPU and708 Python tests. Causality for the live1060 failure still
needs the next native repeat test, rather than being inferred from unit success.

Texture-consumer research: native+1F40DC calls+1F5A28 for residency tracking;
the latter records bitsets/list entries and is not by itself an allocation AddRef.
Native fixed texture creation+221930 stores the separate allocation owner at
record+10h. Future deferred shader consumption still needs an owned allocation
and unchanged content/read epoch. This is consistent with D3D12MA's separate
[resource and allocation reference counting](https://gpuopen-librariesandsdks.github.io/D3D12MemoryAllocator/html/quick_start.html),
not permission derived from descriptor equality. Relevant complete native exports
are texture-consumer-*.c and texture-fixed-creation.c under the dated build folder.

Installed475 DLL/PDB, settings unchanged at deployment:
DLL `0B27F4F0A12DA377C6140D4B8B08C13DAA9F2230453DECCE3BEFB7D64CABA37F`;
PDB `EE0C2E3B0A84662EC7E4CAFA823EFF125E7DCA162576F473F962A3299D1191A0`.

Run476 initial capture completes without allocation rejection, with258 GPU
matches and145 private geometry sets, but one uncaptured292B B4 at source205
leaves353/354 banks Ready. Its resource has509 current records and zero generic
evictions, so that remaining miss cannot be attributed to count/budget eviction.
The exact old copy event was no longer in the bounded history. This failed run
is retained. The launcher previously asserted the first bank before its planned
repeat test; strict first+repeat bank checks now occur together after both
independent reference captures, still before retirement/replacement. Tests reject
an incomplete first OR second bank.709 Python tests pass.

Run478 passes22/22 on475, including the strict full-bank and repeat checks:

- Initial354/354 banks,258 original GPU instance matches,151 complete private
  geometry inputs. Recorder completes in7.8053 seconds.
- Repeat354/354 banks,255/255 CPU/original-GPU matches,255 matching private
  versions(13776B),145 complete private geometry inputs including129 compact
  joins.16609 completed writers,FirstLoss0, no GPU mismatches. Recorder completes
  in8.1833 seconds; the old8-second limit would have interrupted this observation.
- Both reference images remain exact. No native private shader consumption,
  original draw suppression, common CPU preparation or FPS gain is claimed.

The slow repeat's3.3MiB live minidump parsed with the exact475 PDB places thread
32024's current RIP in NativeRetainedGeometry.cpp:1673, the per-pixel CPU reference
comparison in Poll. This is a direct sampled instruction pointer, while the
additional stack entries remain a scan. The same process subsequently finishes;
there is no exception stream. The evidence supports optimizing bulk equality
checks while preserving exact component/pixel mismatch counts, not attributing
this delay to a deadlock or a measured production GPU bottleneck.

Normal479 passes9/9, all diagnostic controls zero, no crash/reporter. Both478 and
479 close the game with the bounded shutdown fallback and restore the preview.
The single-pass goal remains active: native paired shader consumption of the
complete owned input set, texture allocation/content lifetime, common CPU scene
preparation and elimination of the redundant native pass remain required.

## Owned Material Inputs And Direct GPU Banks (2026-10-07)

Source-table and selected-slot capture can now return exact held SRV/UAV resource
dependencies, with original parameter/offset/stamp, typed nulls and UAV counters.
Outputs are transactional and independent of descriptor-storage interning. Dense
and high-offset sparse tests verify identity, source coordinates, nulls, counters
and nonempty-output rejection; no historical resource lookup is exposed.

NativeTextureRead::AcquireImmutable retains the typed native allocation and its
allocator, separately from the D3D12 resource. It requires a fixed native asset
record, unchanged initial-upload history, live typed headers, matching allocator
Pimpl and creation information. Committed and placed storage are supported;
placed storage reuses the guarded allocation-offset reader and must match the
actual creation heap/offset/extent. Physical/content validation repeats at the
original submission and after the original GPU completion. Capture history now
includes other shader-readable immutable texture formats, without broadening the
old texture-copy layout/format contract. The native creation provider+222EB4 was
exported as assembly: it obtains the allocator from registry+13BC540, matching
the already-guarded typed reader. The allocation pins do not themselves establish
a future destination read epoch or native residency registration.

Retained material24Q rows and per-source5Q summaries distinguish null SRVs,
deferred buffers/UAVs, rejected assets, pinned immutable textures and completed
private T5 overrides. Work owns these leases and records materialInputsReady for
the future consumer. The Python report cross-checks source scopes, owners,
submission/completion and T5 view ownership, without claiming execution.

Build482 passes179 C++/GPU and714 Python tests. Live483 completes both reference
captures with identical material coverage in each:13740 dependencies,12373 pinned
texture bindings(all valid after source completion),524 deferred buffers,654
rejected assets,22 null SRVs and167 completed T5 overrides.80 source commands have
complete material inputs. Rejections are380 eligibility failures(85) and274
non-fixed native assets(87). These are binding counts, not unique textures.
The strict overall run fails because353/354 descriptor banks are Ready in each
capture; no crash/reporter, preview restored and game closed. No shader draw was
yet recorded using the new native material ownership route.

Saved483 data confirms equal GPU instance bytes for pairs0/176,12/188,13/189 and
14/190. Source0/176's original mesh IB/VB domains match, including the overlapping
76932B vertex views and12144B index view. Independent private mesh versions still
need actual byte-equivalence proof before replacing one with the other. CBVs are
not camera-only:0/176 and12/188 differ in VS b7/b1 and PS b7;13/189,14/190 and
39/216 differ in VS b1 and PS b7. A general consumer must use the full converted
paired pipeline, not assume the older camera-only native pipeline is sufficient.

StereoBindingBank now accepts owned GPU-resident CBVs and borrowed owned SRVs,
including default texture views. It retains the supplied resources/version
owners without CPU constant upload or new SRV snapshots. Alignment, bounds,
device and exclusive input-mode checks remain enforced; the caller still owns
contents, state and destination-epoch admission. The existing CPU-only arena
rejects these new input modes. The direct-input fixture uploads distinct6/7
constants on GPU after bank creation, reads them with explicit/default shared
texture SRVs, checks exact RGBA output and delayed-owner release behind a real
queue fence. It passes on WARP and the hardware adapter with D3D12 validation.
Build485 passes180/180 C++/GPU tests and715 Python tests.

Next native consumer work must prove shared private mesh contents, construct the
full paired bank from both GPU CBV versions and compatible shared descriptor
domains, and register material reads on the actual consuming native recording.
Original-shader references and the converted view-instanced draw must then be
compared on both layers with complete state restoration. Existing source fences,
bank construction and materialInputsReady alone do not authorize that draw.

Installed485 DLL/PDB, settings unchanged at deployment:
DLL `3ADC25A0BFE0CE1E52FDB56E263DAB3A063041CA9C7D5898BA1756DFE391EE9C`;
PDB `78F62BBAF6E4893106ADA846F9725927E95B0A2BFCA820E23EDEF2C9FD983F8D`.

Live486 passes22/22 with485. Both captures have354/354 Ready descriptor banks,
13744 material dependencies and12377 pinned texture bindings, all still valid
after original completion.81 commands have complete material inputs. Both retain
524 deferred buffer bindings,654 non-eligible assets,22 null SRVs and167 private
T5 overrides. Initial/repeat recorder durations are9.7466/9.5743 seconds. These
are diagnostic durations, not production frame times or performance gains.
The direct-GPU paired-bank extension is hardware-fixture verified but not yet
connected to a native paired shader draw. No second native draw is suppressed.

Normal487 passes9/9 with no crash/reporter, all test controls cleared and preview
restored. Both486 and487 close the game after their checks. All settings remain
unchanged; installed binaries are the archived485 build. No commit or push made.

## Native Prepared Pair Consumer (2026-10-07)

NativePreparedPair now selects completed owned sources, compares the original
mesh domains/arguments and exact GPU-matched instance bytes, copies both private
mesh versions to bounded readback on an actual native recording, and requires
byte equality before sharing one mesh. It builds original-source descriptor
banks and the full converted paired bank with GPU-resident CBVs/owned SRVs.
The consumer records two original-shader references and one view-instanced draw
into separate private two-layer targets. Native context/use registration extends
fixed texture residency to this actual consumer. Allocation/content and native
recording validation repeat before submission. Extra shader draws share a
zero-initialized predicate enabled only after those checks; original game draws
are never suppressed. Root/heaps, compute tables, IA, targets, viewport/scissor,
stencil/blend, shading rate/image and view mask are restored before returning.

Private targets have a separate initialization mode that cannot import/export
native attachments accidentally. Source-shape inspection returns held metadata
of finalized copies, not a fresh original-resource read permission. The launcher
has an explicit --prepared-pair check before the whole-bank gate. Its report
requires actual recorded2+1 draws, mesh equality, zero per-eye differences and
nonzero contribution in both eyes. Candidate failure and mismatched state are
exported separately. Test491 exposed a Python Reader constructor mismatch before
any native probe request; the API call and a CLI-construction regression test
were fixed.493/495 rejected every candidate;495 identified34 matching pairs
blocked by dynamic state. No execution was claimed for those runs.

Run497 identifies actual per-eye VRS: base rate1x1, combiners PASSTHROUGH/OVERRIDE,
different image pointers and otherwise equal state. The image is effective under
OVERRIDE according to the [D3D12 VRS combiner contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist5-rssetshadingrateimage).
Original-position VRS snapshots now preserve each draw's exact image and restore
its state. Run500 confirms the maps differ, so equal-image-only selection cannot
admit the matched pairs. The isolated reference/paired comparison now uses a
common per-axis finer-rate map. It is never coarser than either original map;
all49 combinations of the7 valid shading rates are tested. Both original game
draws still use their own maps. This is explicitly a common-rate shader comparison,
not proof that native per-eye VRS outputs are bitwise unchanged.

Run502 crashed during startup before the pair test: report
Cyberpunk2077-20261007-031728-11680-21520, EXE+E6ABAD. IDA shows a null object in an
array passed to a virtual callback; the stack scan contains Codeware. Causality
is unproven. Crash analysis and the complete function export are saved; the
reporter was closed. The same build then reached run503's actual consumer:
sources176/0,638784 mesh comparison bytes equal,5 registered immutable assets,
2 reference draws and1 paired draw. No differences were detected, but both
reference contributions were zero, so error21 correctly fails the test. The93x93
VRS maps differ in186 cells; the common map refines71/116 cells respectively.
This blank run is not a successful single-pass rendering result.

Build504 captures bounded original depth/stencil checkpoints before eligible
source draws (64MiB/eye budget) and seeds both isolated variants from those
completed copies. It does not change the original or converted PSO depth rules.
This supplies the prepass data absent from the prior cleared targets. Private
depth seeding is tested with native-source state restoration.181 C++/GPU tests
and717 Python tests pass. Native visible-output comparison remains pending.

Installed504 DLL/PDB, settings unchanged at deployment:
DLL `2BF93A3AF1114C6E36678BAF53B9BDD75F338EC8CA5263D1D1578E1D184650B0`;
PDB `62924260ABCED17D47FA5506E4DBFE12AA567B1B69EF2D57FA831E2BBBB6A1BA`.

## Visible Prepared Pair And Bounded Coverage (2026-10-07)

Run505 still produced zero contribution with per-draw depth checkpoints. Build506
adds per-draw GPU pipeline-statistics/occlusion queries, GPU readback of the actual
predicate, native depth/stencil state, and contributions from all three color
targets. Queries resolve only after their matching EndQuery; all query resources
are retained with the native recording and read only after its submission fence.
The contribution gate now counts nonzero color samples across all three targets,
not just RT0; pre-existing depth/stencil is never counted as new contribution.
The reader independently requires actual vertex/pixel execution, passing samples,
and the same nonempty color counts for both comparison variants.

Run507 identifies the first empty pair176/0: each original reference runs12 VS
invocations for6 triangles, but all primitives are clipped before rasterization.
The paired draw runs12 VS invocations and12 clipping invocations, also with no
surviving primitives. Both CPU and GPU observe predicate1. This is evidence of
actual execution, not visible rendering. Run509 tries three different compatible
pairs in one process; all remain empty. The old64MiB/eye checkpoint budget admits
only10 source commands, making first-source selection an ineffective visible
test. Neither run crashes; the launcher closes the game and restores preview.

Build510 captures one depth/stencil baseline at the retained-pass entry for each
eye, before importing/rendering that pass's geometry. All isolated source-pair
comparisons share those completed private snapshots. This admits78 commands in
run511 without a full-screen depth allocation per source. It is explicitly NOT
the native depth state immediately preceding each individual draw. Both original
shader references and the paired variant start from the same per-eye prepass
baseline and common finer VRS image. Native frame output is still untouched.

Run512 proves visible equality for sources200/24 after eight empty candidates:
7920 compared mesh bytes match; all three G-buffer planes and depth/stencil are
bitwise equal between the two references and one full paired-bank draw. Each
color target has161 contributing pixels on the left and159 on the right. The
reported483/477 contributions are sums across three color targets, not distinct
screen pixels. Actual GPU counters:

| Counter | Left reference | Right reference | Paired |
| --- | ---: | ---: | ---: |
| IA vertices |24|24|24|
| VS invocations |12|12|12|
| Clipping invocations |8|8|16|
| PS invocations |580|582|1162|
| Samples passing depth/stencil |161|159|320|

Thus this pair genuinely shares vertex work, while still shading both eyes.
This is neither whole-pass coverage nor a frame-time/FPS gain: source capture,
readbacks and duplicate reference draws remain diagnostic overhead, and no native
draw or CPU preparation is suppressed. The overall512 suite fails its separate
whole-source-bank gate: six commands lack provisional PS b4 selector inputs
(sources37/215:292 bytes;89/90/266/267:132 bytes). The repeat capture creates348/354
banks. No crash/reporter appears; the game closes and preview is restored.

Build513 distinguishes source exhaustion(error24) from a selected pair that
cannot be constructed(error27). The test driver can inspect up to64 candidates
in the same process, bounded by its deadline. It records every empty/successful
attempt, stops immediately on mismatches or resource failures, and in all-pairs
mode requires at least one visible success plus confirmed enumeration exhaustion.
The launcher requests this full coverage mode. Source capture and launcher console
output are compact, while their complete JSON artifacts remain unchanged.
The final513 regression suite passes181 C++/GPU tests and720 Python tests.

Installed513 DLL/PDB, settings unchanged at deployment:
DLL `C464250EDD787E451B9139C4B7868539C8243427E97E51C4F93872A6166F3D3B`;
PDB `AAE66069F1536C14739A668116180788FAA469ADA49656F12EC62263CBA28CDF`.

Live514 passes23/23 launcher checks, including the full native prepared-pair
enumeration.31 compatible pairs are tested in one process, followed by confirmed
exhaustion.9 pairs have nonempty, bitwise-equal outputs in both eyes;22 are empty
and do not prove visible correctness. No differences or resource validation
failures occur in the31 comparisons. The visible pairs span3 native shader
families and include a3-instance draw:

| Left/right source | Indices per instance | Instances | Reference VS total | Paired VS |
| --- | ---: | ---: | ---: | ---: |
|202/26|36|1|48|24|
|204/28|39|1|38|19|
|208/31|21|1|18|9|
|209/32|6|1|8|4|
|214/34|1860|1|2480|1240|
|219/42|12|1|16|8|
|220/43|108|3|214|107|
|221/44|2460|1|846|423|
|223/46|28224|1|32864|16432|
|Total|||36532|18266|

For the9 visible pairs, actual VS invocations total36532 for the original
references and18266 for the paired variants. These are independent isolated
comparisons, not one batched whole-scene rendering or measured FPS improvement.
The repeat capture creates354/354 source banks in514; the intermittent missing
selector data observed in512 is not claimed fixed. All test controls are restored,
post-test HUD/both-eye checks pass, and there is no crash/reporter. The game is
terminated after the normal close timeout; preview is restored.

Next required implementation: retain texture arrays across an ordered compatible
geometry sequence, compare the complete sequence with the original per-eye order,
then consume current-frame shared CPU preparation and suppress genuinely redundant
native work. Per-pair equality alone does not authorize any of those steps.

Normal515 passes9/9 after the513 changes. The game reaches stable HUD, both-eye
and repeated packet/CPU preparation checks pass, all diagnostic controls are
cleared, and no crash/reporter appears. The launcher closes the game and restores
preview. Post-run SHA256 checks match the513 deployment manifest for game/plugin
UserSettings, vrport.ini, launcher settings, DLL and PDB; calibration remains
absent. No game, CrashReporter or RenderDoc process remains. No commit/push made.

## Ordered Prepared Geometry Sequence (2026-10-07)

NativePreparedPair request2 now consumes all admitted direct sources from the
completed retained capture as an ordered sequence. Strong original-root ownership
is explicit. Original PSOs with the existing read-only/single-layer native geometry
contract can remain source-only even without a converted family. Candidate pairs
require the prior metadata/instance checks, then actual GPU equality of both mesh
copies. Bank-incompatible or mesh-different pairs remain original per-eye draws;
they are counted separately and never acquire a shared identity.

The existing BuildStereoMeshPlan constructs a common supersequence. Its projection
must equal each complete admitted eye source list in native ordinal order. One
original-root bank covers the reference stream and source-only fallback draws;
paired banks cover selected common draws. References render each eye in its own
order. The optimized stream uses eye masks1/2/3 in one persistent set of texture
arrays, without per-draw target copies or clears. Both variants begin with the
same per-eye pass-entry depth and one common finer-rate VRS map over all admitted
sources. Four eye/variant target initializations and one final image readback are
required. Per-source topology, viewport/scissor, stencil, blend and shading-rate
state are applied, and the original native caller's bindings/state are restored.

Batch telemetry exports every admitted source and planned emission. The Python
verifier checks exact per-eye projections, no duplicates/omissions, paired command
indices, recorded counts, shared targets, nonempty equal outputs, GPU execution
and zero native suppression. The launcher's --prepared-sequence mode runs after
the optional individual-pair regression, before the whole-source-bank gate.

Build516 passes181 C++/GPU and722 Python tests. Live517's ordered comparison passes:
75 reference draws become44 optimized draws (31 both-eye,6 left-only,7 right-only).
All three G-buffer targets and depth/stencil are bitwise equal. Reference VS counts
21926+24055=45981 become24278; PS counts66385+71018=137403 remain137403. Registered
material-owner count is51;54555104 mesh-comparison bytes match. The separate full
source-bank check still fails after the successful sequence comparison. No crash
or reporter occurs, and the launcher closes the game/restores preview.

This is the complete ADMITTED subsequence, not all354 retained commands or the full
scene. It still uses completed diagnostic input snapshots and extra reference
draws; current-frame CPU preparation and native draw suppression are not enabled.
The lower draw/VS counts do not establish an overall FPS gain.

## Independent Constant-Pool Publication (2026-10-07)

A deterministic reentrant sampling fixture exposes a source-input cache bug:
while sampling one UPLOAD-to-DEFAULT copy, an unrelated copy into a disjoint range
of the same destination changes its global mutation counter. The old publication
check rejects the first valid sample and permanently leaves that static material
range uncaptured. The pre-fix fixture fails with "Unrelated pool copy invalidated
an in-flight constant sample".

Build518 replaces that whole-resource sampling ticket with up to64 bounded active
range observations per resource. All admission/invalidation/removal happens under
the existing cache mutex; no lock is held while mapping/sampling D3D12 resources.
Overlapping writes and full invalidation cancel affected observations; generation,
enable, typed-hint and memory-budget checks remain. RAII removes observations on
every exit, with the observing call holding the owner alive. No cache budget is
increased. These are still provisional CPU bytes: original-scope ownership and
exact GPU snapshot matching remain required before shader-input use.

Tests now cover interleaved disjoint292/132-byte copies, overlapping replacement,
preservation of the newer overlapping sample, full invalidation, generation
change,64 active independent observations, rejection of the65th, and admission
after all scopes exit. The full181 C++/GPU suite passes.

Live519 passes24/24. Both initial and repeat captures produce354/354 source banks,
with zero declined selectors and zero GPU input mismatches. The sequence compares
79 reference draws with45 optimized emissions:34 both-eye,5 left-only,6 right-only.
Mesh comparison covers54621368 bytes. All color/depth/stencil outputs match;
reference VS22940+24063=47003 becomes24282, while PS65036+70683=135719 remains135719.
No mesh/bank pair is rejected. Test controls, including prepared request/state/
selection, are verified cleared; no crash/reporter appears. The launcher closes
the game and restores preview. This confirms the improved input coverage in these
two live captures, not universal coverage of every scene or material.

Installed518 DLL/PDB, settings unchanged at deployment:
DLL `F96D5ED2BD7DCB29E446B7609683983B3555F4C9FD428F03BB684D5C45DFF34F`;
PDB `4F5A76A5CDB6FA678DCBE683FC4595834BB749DFCF6F1ABA02B815BDB593CD68`.

The launcher also supports --prepared-sequence-turn: take a fresh retained capture
at a reversible10-degree simulator yaw, verify a new native preparation epoch and
the complete bank, compare the ordered sequence again, then verify both-eye scene
motion and return to the original pose.

Live520's baseline sequence/bank pass, but the new turn branch raises an unbound
still_running callback before its turned capture. Its finally block restores the
pose and the launcher closes the game without a crash. The callback is moved to
the common launcher setup, before all users.

Live521 crashes while loading the save, before any test/sequence request:
Cyberpunk2077-20261007-051135-17024-30536. The archived518 DLL/PDB parses the dump as
EXE+0xB8084B, reading address0. IDA identifies a virtual call through a null held
object during reference release (caller+0xFF384). The bounded stack scan contains
engine addresses, not a proven unwind. Complete function exports and the dump
analysis are saved under build/single-pass-20261006/crash-521-* and
prepared-startup-crash-521.log. No causal link to the new sampling/sequence code is
established, and no speculative engine null-check patch is applied. The reporter
is closed. This crash is not claimed fixed or conclusively random.

Live522 repeats the same518 DLL and passes the complete turn scenario. After a
fresh10-degree yaw and new native preparation epoch, the retained bank contains
300/300 commands.74 admitted sources (36 left,38 right) become44 emissions:
30 shared,6 left-only,8 right-only. There are31 eligible pairs; the order-preserving
plan deliberately leaves one pair separate.31,189,060 mesh comparison bytes match,
and all final color/depth/stencil planes are bitwise equal. VS22062+22301=44363
becomes22579; PS62286+62474=124760 remains124760. Initial/turned/returned scene
samples show both eyes actually moving and returning (RGB mean differences about
44.49/43.21 outward,1.61/1.59 returned). Pose cleanup is confirmed restored.

Normal523 then passes9/9 with all diagnostic controls off. No crash/reporter occurs
in522 or523; both games close and preview is restored. Post-run settings and exact
installed DLL/PDB hashes match the518 manifest; calibration remains absent. No
game, CrashReporter or RenderDoc process remains. The startup crash's cause stays
unproven despite these successful repeats. No commit or push made.

Remaining work is still production single-pass: broaden material/buffer ownership
beyond the admitted subset, consume current-frame inputs without diagnostic
readbacks/reference rendering, share CPU scene preparation, suppress the redundant
native work, and measure clean frame time/FPS across scenes and settings. None of
the current ordered comparisons alone proves these remaining requirements.

## Raw And Structured Material Buffers (2026-10-07)

The519 repeat capture has167 direct commands blocked only by PS t4/t6 buffers.
DXIL reflection identifies t4 as ByteAddressBuffer and t6 as structured64; t5 is
the existing structured128 path. The native SRV observer now tracks PS registers
4,5,6 independently in its per-recording key, for both individual and cached
setters. Exact root/slot/descriptor/native-record checks remain. T5's default
helper contract is unchanged; raw32-bit elements and structured64 get explicit
register-specific range checks. ABI fixtures exercise new t4/t6 entries, including
GPR/FLAGS/XSTATE/stack/unwind preservation. The format rules follow the
[D3D12 buffer SRV contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_buffer_srv).

Build524 passes181 C++/GPU and723 Python tests. The first525 capture nominates all
334 t4/t6 bindings. Actual t4 backing pools are8/32MiB, but individual views are
only about2-19KiB; native class1/low type13, committed backing, no direct MA owner.
T6 is a64-byte structured view, native class1/low type8, with a placed64KiB MA
allocation. The repeat525 capture stops on pre-existing CBV pool failure1061;
there is no crash. No material buffer copy was claimed for that build.

The native pool selector1F7EB8 explicitly supports low type13, and the generic
creator220110 routes it through the existing pool allocator. Complete IDA exports
are saved as material-buffer-pool-select-1f7eb8.c and
material-buffer-create-220110.c under build/single-pass-20261006. The existing pool
reader now has an explicit low-type13 option; its IA default remains type5. The
geometry-only disabling switch is not applied to type13, matching native code.
Occupied slot, size-class bounds, live record, pool resource and owner still must
match. Tests reject treating this raw pool as IA or accepting an unoccupied slot.

NativeMaterialBufferSnapshot captures each entire original t4/t6 SRV range at its
certified source occurrence, using an8MiB private arena per eye. Pool/placed MA
allocation and allocator are pinned; placed offsets use the guarded native getter.
Only fixed-read class1 DEFAULT buffers with observed COMMON/GENERIC_READ creation,
no UAV flags and a verified GENERIC_READ source state are accepted. The source
does not receive a new transition. Exact view bytes are copied, without reading
pool padding or changing source indices. Private SRVs preserve NumElements,
raw/structured format and stride, rebasing only FirstElement to the private range.

The arena finalizes to GENERIC_READ on the original recording. Source submission,
native record/physical ownership and actual source-fence completion are required
before publishing a private view. Material status7 distinguishes these completed
copies from immutable native textures and private T5 views. Reference banks clone
only the affected8-slot CPU table and replace t4/t6 descriptors; paired banks bind
the corresponding private views directly. A mismatched original binding is not
silently replaced by an unowned native buffer. Copy receipts and range/owner
non-overlap are independently checked by the report parser.

Build526 passes182 C++/GPU and723 Python tests. Live527 stops at the first source's
CBV pool check1061 before reaching the new material copies. Additional bounded
CBV pool diagnostics in528 expose the underlying preflight reason, write-history
details and old/new epochs without relaxing that check. The intermittent CBV pool
rejection remains unresolved;529 does not reproduce it.

Live529 passes the whole scenario on528. Both captures complete all334 material
buffer copies, totalling1,206,872 actual copied bytes. The initial capture has248
fully prepared material commands versus81 before this extension; the repeat has
247 out of353 commands. The admitted ordered sequence has118 sources (57 left,
61 right) and73 optimized emissions:45 shared,12 left-only,16 right-only. Four of
49 eligible pairs remain separate to preserve each eye's order. There are no mesh
or bank pair rejections, and all color/depth/stencil comparisons are bitwise equal.
Actual counters are VS74722+85212=159934 versus108070; PS936210+935678=1871888
versus1871888. No native draw is suppressed and no overall FPS improvement is
claimed. The compared output now contains about873k nonzero pixels per eye in each
color plane, rather than the much smaller earlier subset.

The next geometry-coverage constraint is explicit:529's repeat creates146 private
IA commands;200 are deferred for arena capacity and7 for UPLOAD backing. Material
readiness alone cannot admit those commands. Private input-copy deduplication or
more precise capture, followed by current-frame production ownership, remains
necessary; merely declaring the complete source bank does not solve this limit.

Installed528 DLL/PDB, deployment preserves all settings:
DLL `050772B1D465BDE30483574D8D8DF4391F416A57221435181FA9D9843C057FE4`;
PDB `DD6C14B0794089ACB91383A18EB7FA317ED609189F19B757B6F3F60C34A10254`.
Live530 passes the complete head-turn regression on the same528 DLL. At the
10-degree pose,240 material-buffer copies complete (909872 bytes),196 material
sources are ready, and the full source bank covers300 commands. The admitted
sequence has101 references (55 left,46 right) versus63 emissions:38 shared,
17 left-only,8 right-only. All color/depth/stencil comparisons are bitwise equal;
VS78364+40553=118917 becomes80704, while PS960771+951198=1911969 remains1911969.
Both-eye scene motion and return are verified, pose cleanup is restored, and all
diagnostic controls are cleared. There is no crash/reporter. CBV pool diagnostics
are zero in the successful captures; this does not establish a fix for the earlier
intermittent1061 failures. The source subset is still limited by geometry/input
coverage, and native suppression remains zero.

Final regression coverage for528 is182 C++/GPU tests and723 Python tests. Normal531
passes9/9 without a crash/reporter. The game closes and preview is restored. Final
SHA256 checks match the528 manifest for installed DLL/PDB, game/plugin settings,
vrport.ini and launcher settings; calibration remains absent. No game, reporter
or RenderDoc process remains. No commit or push made.

## Same-Recording IA Snapshot Reuse (2026-10-07)

Inspection of529's repeated capture identifies the dominant arena consumer:
one6MiB slot7 buffer is described45 times in an eye, largely by indirect draws.
Those repeated views alone account for264MiB of redundant requested storage.
CPU draw operands cannot trim the indirect instance range before argument data
is known, so the implementation retains its complete original view coordinates.

NativeCopyWindows tracks previously recorded private copy ranges within one native list
generation and eye-owned arena. A later source occurrence may reuse a covering
range, but still performs the existing original binding, allocation/pool, resource
state, parent and recording checks. Originals remain pinned through the recording.
The cache is not an ownership/read grant and never reuses between eyes, lists,
generations or frames. Sources allowing UAV writes are excluded from reuse.

Observed resource/address writes invalidate matching entries in every active
window; transitions and UAV barriers invalidate affected resources, alias/enhanced
barriers invalidate conservatively. Unknown draws, dispatches, bundles, stream
output and clear-state operations invalidate that recording's window. Only the
audited read-only geometry/pure-indirect draw paths preserve it. Reset closes the
window; Finalize closes it after recording all uses. Any observer-coverage failure
disables reuse. With no active arenas, window notifications return before locking.
No CPU Map, GPU readback or new source transition is added for a cache hit.

The64MiB per-eye arena bound is unchanged. Logical source coverage is kept in the
old copiedBytes receipt, while new transfer telemetry separately counts actual
copies, reused bytes and allocated arena bytes. Every private view exports its
original copy occurrence. The Python verifier requires an earlier covering copy
with the same source/private resources, byte mapping, native recording and eye;
it checks per-command logical/actual/reused byte sums and allocation growth.
Unit coverage includes bounds, subviews, mismatched identities, concurrent address
writes, unknown/audited draws, barriers, reset and failure-closed observation.

Build532 passes183 C++/GPU and724 Python tests. Live533 passes the complete launch
scenario and both retained captures. Initial capture:347/354 private IA commands,
54.8549MiB transferred,530.0229MiB reused, per-eye arena26.6249/28.2314MiB. Repeat:
345/354 commands,65.6496MiB transferred,518.5900MiB reused, per-eye arena35.0990/
30.5520MiB. Remaining repeat failures are9 UPLOAD-backed inputs, not capacity.
These are diagnostic capture bandwidth/storage reductions, not production FPS.

The admitted sequence expands to243 references (122 left,121 right) and143
optimized emissions:100 shared,22 left-only,21 right-only.14 of114 eligible pairs
remain separate to preserve each eye's order. All final G-buffer/depth/stencil
planes match bitwise; mesh comparison covers83,876,428 bytes. VS239865+236872=
476737 becomes277747; PS943186+942841=1886027 remains1886027. Native suppression
is still zero. This is substantially broader sequence coverage, not full-scene
single-pass or elimination of CPU scene preparation.

Installed532 DLL/PDB, deployment preserves all settings:
DLL `531ECE310FA0909283AEB8B58EAAB25E59BE1FDB6133701295FDC2CF22B72C95`;
PDB `9502F5E53C27B781CFF2B4583EBB24D3DE0021E6BAE12E8CDAFD04D0A366B925`.
Head-turn534 gets valid turned source captures, but building their CPU descriptor
tables fails with E_OUTOFMEMORY(0x8007000E). All300 logical plans are Ready and no
selector/CBV is missing; actual bank construction fails. The preceding completed
prepared comparison still holds its per-pair banks and inputs. No crash occurs;
pose cleanup and game shutdown complete.

Build535 switches the prepared path to the existing StereoBindingBank::CreatePass:
all paired materials share one resource/sampler bank, with identical shared tables
deduplicated. The completed Work is also released after its GPU fence and final
comparison, while exported POD results remain readable. Live536 confirms244
references become151 emissions with equal images and zero extra SRV snapshots;
the93 shared draws use196663 descriptors in one paired bank (reference bank69734).
However, this run exposes a new race in the immediate cleanup, so it is NOT a
successful full regression.

Crash536: Cyberpunk2077-20261007-081716-21448-10264, DLL+0x2177C1 in
prepared_pair::Reset, reading0x1D0. Exact535 symbols identify NativePreparedPair.cpp
line458. Reset observes active before waiting for the mutex; Poll then completes
and deletes Work, and Reset dereferences the now-null pointer after locking.
Build537 rechecks both active and Work under the mutex in Draw, BeforeSubmit,
Submitted, Reset and Poll. A test-only deterministic fixture holds the mutex until
a Reset callback passes its fast active check, retires Work, then releases the
callback; production builds contain no test hook. The crash report was inspected
and its reporter closed; the head pose had not yet changed. No unrelated engine
crash is used to explain this confirmed mod bug.

Build537 passes183 C++/GPU tests, including that deterministic retirement race,
and725 Python tests. Live538 passes the complete source/turned sequence scenario:
baseline243 references become137 emissions, with106 shared draws using164375
descriptors in one paired bank; source bank69747 descriptors. VS473137 becomes
259034. At10-degree yaw,191 references become105 emissions,86 shared draws,
100243 paired descriptors and36879 reference descriptors. VS345527 becomes177325.
Both sequence results are bitwise equal for both eyes' color/depth/stencil and
have no extra SRV snapshots. Source banks and all post-test HUD/control/crash
checks pass. Both eyes visibly move and return; pose cleanup is restored.

Repeat538 captures347/354 private geometries using54.3756MiB of actual copies
and528.3160MiB of reuse; arena bytes34307824/22710592. Turned capture293/300 uses
44.7532MiB actual and523.8618MiB reused; arena bytes23686544/23241904. No increased
arena budget, resolution change or detail reduction is involved. The prior
out-of-memory failure and null-Work reset crash do not recur in538. This run
demonstrates broader correctness and diagnostic memory savings, not a production
frame-time/FPS gain or shared CPU scene preparation.

Installed537 DLL/PDB, deployment preserves all settings:
DLL `D2DE20590E7F1C258FC387A692D20F29737F16BF66F806359A25D1FD9EEC3837`;
PDB `0798B78A7EDC0AD051F7D82A0ECD96D640E0024A35F0A1C219C6DE631BDD7B6F`.
Normal539 passes9/9 with all diagnostic controls off, without a crash/reporter.
The launcher closes the game and restores preview. Final installed DLL/PDB and
game/plugin UserSettings, vrport.ini and launcher hashes match the537 manifest;
calibration remains absent. No game, reporter or RenderDoc process remains.
No commit or push made. Production current-frame input ownership, shared CPU
preparation, redundant native-work suppression and clean-FPS measurement remain
unfinished; this milestone does not redefine the full single-pass goal.

## Executed Coupled Graph Schedule (2026-10-07)

The test-only CPU-overlap candidate keeps original phase-1/GPU edges, restores
every originally ordered shared-object access, and supplies every required native
context publisher before its synchronizer. On the538 saved graph this produces
the explicit CPU cycle44->46->50->99->108->26->33->44. The peer synchronizer cannot
finish early while still consuming the original MAIN late-context prefix. The
candidate is correctly rejected, not executed. The diagnostic lives only in
tools/single_pass_tests/cpu_preparation_overlap.hpp and context_order_test; it is
not a production fallback or a second scheduler.

OwnedExecutionRequest2 now selects the existing fully coupled whole-discard plan
for a single owned native root. Request1 retains the unchanged reference schedule;
default controls remain off. The native sorter compiles the changed CPU/GPU
dependencies, all native definition/child owner protections remain, and the native
resource compiler receives the actual changed graph. No native node, shader draw,
CPU gather or scene preparation is suppressed. The old independent CPU schedule
hook is disabled during this experiment so it cannot apply a second set of edits.
The launcher exposes this separately as --owned-coupled, incompatible with legacy
frontier/source-snapshot tests and retirement cycles.

Schedule telemetry ties source and executed topology to the actual consumer,
original compilation and terminal receipts. The reader checks exact node/object/
work identities, the three GPU-frontier edge substitutions, CPU prefix readiness,
preserved unrelated CPU edges and shared-owner serialization. Source bindings are
recaptured at declaration and compared to the execution graph at that same point;
the coupled dependency plan is rebuilt from them and must remain identical.

Live541 executes the changed graph and completes native resource compilation and
both-eye retained reference rendering, but the new report rejects a fingerprint
sampled at a different time. Saved evidence identifies node63/work611830's last
definition QWORD changing before declaration. IDA export schedule-binding-611830.c
shows that work function does not read its first/object argument. Build542 does
not blanket-ignore node bytes: it recaptures original/actual bindings together,
requires exact matching definitions at declaration, preserves frozen topology and
rebuilds the same plan. Tests distinguish legitimate current binding updates from
a stale/missing binding sample or a changed node identity.

Live543's individual capture passes, but the outer launcher still expects the
old cpuSchedule/pairedFrontier fields. This harness-only error is fixed without
changing the DLL. Live544 then passes20/20 in one process: two different native
consumers(5774/5877), epochs1/2, both executing the coupled mode. The155-node graph
changes112 CPU ranks and54 GPU ranks; source928 edges become1956. All591 required
native context-prefix pairs are proved, with52 shared-object orders and one
intentional serialized discard reversal. Native resource compilation completes
for each execution, both CPU terminal groups finish, and no crash/reporter occurs.
Each capture retains176 MAIN and178 peer draws, with bitwise-equal private/native
G-buffer and depth/stencil results under the NEW schedule.

That comparison does not prove full-frame visual equivalence between old and new
schedules: the original node/draw work still runs, retained rendering compares
against that same execution, and post-test screenshots can come from normal tail
frames. Persistent coupled operation, exact-frame final-image comparison, temporal
history/RT validation, current-frame paired shader consumption, shared CPU
preparation and actual redundant-draw suppression remain unfinished. No total FPS
gain is claimed from this scheduling experiment.

Reference545 passes23/23 on the same542 DLL, including full input snapshots and
the existing prepared sequence:245 references become143 emissions with bitwise
equal outputs and schedule mode reference. This checks that the new opt-in path
does not change the existing reference mode. Local coverage is183 C++/GPU tests
and729 Python tests. Both experimental and reference games close, with settings
restoration reported. Normal546 passes9/9 with the experimental controls off.
Final183 C++/GPU and729 Python regressions pass. Game/plugin settings and installed
DLL/PDB hashes match the542 deployment manifest; calibration remains absent. No
game, reporter or RenderDoc process remains. No commit or push made.

Installed542 DLL/PDB, deployment preserves settings:
DLL `CD8B567F3A874F279BFDB1B8B8321C0E41A56D11A95552D078447EE8975EFD3D`;
PDB `42E96D1F8DD9C7A95F078C78F05FB571D62403D7C9921C627FB2D21F989D096C`.

## GPU Geometry Selection Without A Pre-Draw CPU Fence (2026-10-07)

Build547 replaces NativePreparedPair's mesh readback -> CPU memcmp -> later draw
recording with GpuGeometryCompare. Each planned pair compares all consumed IB/VB
views and the exact selected slot7 instance slice on the GPU. It produces two
64-bit predicates: equal and fallback. The existing native pre-submit permission
gates both branches; a failed permission cannot accidentally execute fallback.
Initialization is ordered before comparisons by a UAV barrier, and a final
transition orders their results before predicated commands. Source ranges are
bounded, retained, same-device, and validated including unaligned end words.
This follows the D3D12 buffer-predication contract:
https://learn.microsoft.com/en-us/windows/win32/direct3d12/predication

579 live580(coupled + turn) and581(original schedule) pass24/24, including the
intentional selector rejection. Normal sequence checks415 ranges/402720 bytes
across248 sources; turned checks316 ranges/294264 bytes across196 sources.
The corrupted expectation disables every mesh predicate and all three measured
graphics regions have zero VS/PS invocations and samples. Originals remain intact.

Build582 batches comparisons by actual resource pair. A16-byte GPU metadata row
describes each1024-byte block; a2D dispatch covers arbitrarily many blocks within
the existing512MiB/8192-range bounds. Independent atomic AND/OR reductions preserve
all range/pair results without a UAV barrier between batches. WARP/hardware tests
include reversed resource pairs, unaligned tails, multi-range reductions, chained
permission and1025 blocks crossing a dispatch row.189 C++/GPU and752 Python tests
pass.583's normal sequence has415 shader-input ranges in5 dispatches and449 mesh
ranges in3 dispatches, with contributing bitwise-equal output.

583 is NOT a complete successful launch regression: during that launch the
negative reader was tightened to require zero executed-draw counters, but its582
DLL still published the planned counts after rejection. Its GPU counters actually
show zero VS/PS/samples and all113 geometry predicates disabled. Build584 corrects
the executed counters to zero while preserving separately recorded batch counts.
The full latest-build regression must be rerun; do not count583 as a pass.

584 live585 now passes24/24 with coupled scheduling, including the corrected
negative case and head turn.415 input ranges/402720 bytes require5 dispatches;
turned316 ranges/294264 bytes also require5. Geometry comparison uses3 dispatches
in both views. Output remains contributing and bitwise equal:248 reference ->151
optimized executed draws initially,196 ->102 turned. Pair counts vary with the
captured source stream; this is not a before/after FPS benchmark. Corrupting one
expectation yields accepted0/rejected1, zero executed reference/paired draws,
zero VS/PS/samples and no native suppression; recorded batch counters remain intact.

Installed584 DLL `47105AD5EBFA8391A7867832271E6ED6AD65779481BCF710F596EC049D5B28B4`;
PDB `34549183AD77A96408248D1E77F2C582BFA6555BC2181C056346547691EE402D`.
Archive:build/single-pass-20260927/deploy-input-batches-584. Final local suites
pass189 C++/GPU and752 Python tests. The full permanent single-pass objective
remains incomplete: shared CPU preparation, current-frame construction and
native second-pass suppression still need implementation and FPS verification.

Original-schedule586 passes24/24, including positive and deliberately rejected
batched input checks. Normal587 passes9/9 with diagnostics disabled, including
the new fault control at0.585/586/587 close via the normal-close timeout fallback
with no crash/reporter and restored preview. Final DLL/PDB and every game/plugin
settings hash match584's manifest; calibration is absent. No game/reporter/
RenderDoc process remains. No commit/push; overall goal stays active.

## GPU Common VRS Map (2026-10-07)

Build588 replaces the prepared renderer's CPU VRS reduction and rate-map upload
with GpuFinerVrs. It reads every admitted original-position rate snapshot through
an SRV array and computes the same per-axis minimum in two GPU dispatches. Private
snapshots allow shader reads even if the original native image denied them. The
reducer restores all input states and leaves its private output ready for VRS.
It never changes the original game maps or coarsens the existing reference policy.

Compatibility and preparation now inspect only image metadata, not GPU pixels.
The old retained-source VRS Map loop and Source::rateBytes are gone. Original
snapshot readbacks and the GPU-generated map are compared only in prepared_pair
Poll after the consumer's own fence has completed. Invalid rate codes are flagged
on GPU and clamped to1x1; a flagged or unequal map fails the rendering proof.

WARP/hardware fixtures produce source images and consume the common map in one
submission without a source readback/fence wait. They cover all49 valid rate pairs,
non-group-aligned extents, duplicate maps, invalid codes and restored source states.
191 C++/GPU and753 Python tests pass. Reconfiguring the main CMake build picked up
the new source file after the first build's unresolved-symbol failure.

Coupled589 passes24/24, including rejection and turn. Its93x93 common map uses248
source images initially and196 after turning, with2 dispatches and23656 diagnostic
readback bytes. Both maps have0 differing cells against the original CPU policy.
Paired output is nonempty and bitwise equal:248 ->151 executed draws initially,
196 ->100 turned. The deliberately rejected case executes0 reference/paired draws.
Original-schedule590 crashed during startup before any test step; it is not a
successful regression. Its dump is being investigated. No FPS improvement is claimed.

590 crash evidence:Cyberpunk2077-20261007-145219-4056-23012, read-null at
EXE+B8084B. Exact588 DLL/PDB was used for tools/crash/read_dump.py. The fault is
a native virtual call on a null object from sub_B80848; its immediate return
address maps to sub_FF384, which decrements a reference count before this call.
The higher stack scan includes dynamic-buffer cleanup but is not a full unwind.
No mod frame occurs in the bounded fault-thread scan. The crash happened while
QuickBoot awaited Continue, before source capture or GpuFinerVrs activation.
This localizes the failure but does not prove who broke the object's lifetime,
nor prove it random. No speculative null-check patch was applied to the engine.
Artifacts:gpu-vrs-crash-590.log and crash-590-{b8084b,ff3b8,14b51d,2185e8}.c/.json.
The already-started normal591 run subsequently passes9/9 without diagnostics.

Installed588 DLL `0C0C1C0146553C3F2797114CD76F1523A53E641E4C1684AC160875CD612A2437`;
PDB `87D8212AC94023DCF2090FF644650CAB1D9CA76837E48028B7960FA07233A86A`.
Archive:build/single-pass-20260927/deploy-gpu-vrs-588.

Remaining current-frame integration: construct guarded material descriptor plans
without marking predicted selector bytes CPU-verified; use finalized IA/instance/
CBV/T5/material copies with GPU admission. A private consumer inserted inside the
original ExecuteCommandLists hook after both source lists is a candidate, but its
native asset residency/use-fence lifetime must be proved, not inferred merely from
COM allocation pins or descriptor-ring retirement. No owned current-frame consumer
has been implemented yet. Shared CPU preparation and native suppression remain open.

Original-schedule retry592 passes24/24 with the same588 build and no crash. Its
success does not establish the root cause of590's startup lifetime failure.
Normal591 passes9/9. All successful runs close via the launcher's normal-close
timeout fallback, with restored preview and no reporter left. Final installed
DLL/PDB and every game/plugin settings hash match588's manifest; calibration is
absent. No game/reporter/RenderDoc process remains. No commit/push. Goal active.

## Guarded Tables Before Readback (2026-10-07)

Build593 adds a separate GuardedPlan/GuardedTables route. It re-evaluates the
supported selector recipe, or the exact audited multilayer B4/T5 recipe, from
predictions. Each prediction is bound to its exact finalized private GPU view.
No CPU-byte-valid flag is fabricated. Ordinary Build still rejects unverified
inputs; guarded outputs always carry their required GPU checks and source scope.
Original descriptor provenance, source indices, ranges, owners and all copy
receipts remain checked and retained.

NativeRetainedGeometry builds these plans and CPU tables in BeforeSubmit once
both source eyes have finalized and their submissions have been certified. It
records that payloadsProcessed, cbvPayloadComplete and overlayStage are all0 at
construction. The later paired renderer consumes the guarded tables; its
reference still uses separately validated legacy plans. GPU admission remains
mandatory for every paired/reference/fallback draw. Rendering itself still starts
after the completed-source path, so this is current-frame table construction,
not yet current-frame GPU consumption or shared CPU scene preparation.

594/595 exposed reader integration mistakes: the export pattern omitted guarded
symbols, then the decoder expected a nonexistent source-stream verified field.
The reader now resolves all guarded exports and uses the existing strict source
row validator. Saved595 evidence passes offline:354/354 plans and tables,
605 required checks,424568 expected bytes, all built before CPU payload handling.
596 exposed E_OUTOFMEMORY when constructing a second independent CPU descriptor
cache for the late reference path. No game crash occurred.

Build597 shares only immutable descriptor storage between early and late plans.
Exact existing cache keys and each plan's own leases still apply; sharing a heap
does not certify predicted contents. Separate phase counters validate the cache.
Coupled598 passes24/24:354 sources initially/repeated,300 turned. Early cache:
105767 descriptors/2657 tables; after independent late validation:
107103 descriptors/2824 tables,6000 hits. Turned cache:72862 ->73822 descriptors,
2239 ->2359 tables. Paired output remains bitwise equal:248 ->141 executed draws,
196 ->102 turned. Wrong predicted input executes0 draws. Original599 passes24/24.

Build600 additionally transports the guarded T5 descriptor itself in the early
packet. Paired-root binding now takes that descriptor, with a required matching
GPU byte check, rather than borrowing the later CPU-verified T5 wrapper. The
reference continues to use the legacy verified descriptor.

Residency audit for the next current-frame consumer: native ExecuteSubset at
1F5BB0 calls the actual queue at1F5DDC(return1F5DDF), then SignalFence atA08838.
1F40DC registers texture residency record+0x60 through1F5A28 before its fixed-read
state bypass. This matches the structure of Microsoft's D3DX12Residency design,
but the game's exact binary remains authoritative. In particular, ProcessPagingWork
at95AD3C frees its master set before queue execution; the old R14 pointer must NOT
be treated as a live residency membership witness. A future consumer needs a
validated live source-set/outer-fence contract, not just allocation COM pins.
Exports:current-frame-{residency-register,native-submit,residency-queue,
residency-submit,residency-fence,residency-set,paging-work}-593.c/.asm/.json.
Reference:
https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Libraries/D3DX12Residency/d3dx12Residency.h

Build600 local suites pass191 C++/GPU and757 Python tests. Coupled601 passes24/24,
including the early T5 descriptor in the paired root:248 ->135 executed draws,
196 ->101 turned, nonempty bitwise-equal both-eye output. Deliberate input rejection
still yields0 executed draws. This confirms early tables/views can replace the
late ones in the tested shader path; GPU recording is still delayed until source
completion and remains diagnostic, with native suppression0.

Installed600 DLL `D90B3B4CA9BEF52DCDFF0C9B11B5900F2C686DD67F81CD645CE690EDDCF0D0AC`;
PDB `9A9085268E1088CD47078136FD5023383B44446EC04088294F18A6B30E5813E7`.
Archive:build/single-pass-20260927/deploy-guarded-t5-600.

Original-schedule602 passes24/24 and normal603 passes9/9 on build600. Both
close through the launcher's normal-close timeout fallback, without a crash
or remaining reporter; preview is restored.

## Early Complete Source Packets (2026-10-07)

Build604 creates the complete candidate packet at the original BeforeSubmit,
after both source arenas finalize and their submissions are certified. It
uses owned IA/instance views, private CBVs, guarded tables/T5, staged T4/T6,
immutable material leases and the original depth/VRS captures. No CPU instance
payload is retained in the packet or uploaded as replacement draw data.
The independent completed path still checks every admitted packet, and the
prepared comparison now consumes the early packet with a separately validated
reference descriptor bank. This does not yet move GPU consumption to that frame.

MatchesStagedPrivate grants only construction of a descriptor for an exact
finalized copy/recording/queue. MatchesPrivate still requires GPU completion.
The report requires exact ready/skipped scope and counts, both-eye coverage,
zero preconstruction readback flags and later verification of every ready packet.
The guarded report also now rejects a missing T5 check even if aggregate counts
are changed to match the malformed rows.

NativeQueueResidencyScope recovers the native ExecuteSubset frame with the OS
unwinder and validates its original live source sets, held execution lock,
successful paging status, manager generation and upcoming use fence. Texture
membership is checked against the inline managed object, not the allocation
owner, freed master set or cleared insertion bitmap. Fixed texture-state flags
do not bypass residency. Current integration is observation-only and rejects
sources from a different native subset; it does not issue an extra GPU submission.
Neither scope validation nor source preparation grants native draw suppression.

Build604 local suites pass192 C++/GPU and766 Python tests. Installed DLL:
`E7710F298D6A309C74678CF444FBBC27273CA48995272C880CEE83467ACFD0E3`;
PDB `967FEA690B5C2E0BC1E4FC4C368C27CACD6A3EB4B9C810C5EE06719EF221D8E1`.
Archive:build/single-pass-20260927/deploy-staged-sources-604.

Coupled605 passes24/24. The initial packet contains248 ready direct sources
out of354,12276 material bindings and415 shader-input checks. All248 are later
independently verified; construction flags for CPU payload processing/CBV
readback/T5 overlay are0. The prepared renderer executes248 reference versus155
optimized draws with nonempty, bitwise-equal both-eye results and suppression0.

Build606 adds precise native residency rejection diagnostics, without relaxing
admission. Coupled607 also passes24/24. Its first native scope captures correctly,
including the current OS-unwound frame and original closed source sets. The first
texture fails code334: actual resource matches, but managed pageable=0, managed
generation=0 and membership is absent, while manager generation is22675. This is
an untracked class, not evidence that native residency permission succeeded.
Both605/607 close without crashes/reporters and restore preview. No extra owned
consumer submission is enabled. Next integration needs an independent physical
allocation residency lease for this class rather than weakening membership checks.

Installed606 DLL `6052A4A17B930A4FEE4AD7C70563D155035C588EFFFD4C07C8AA24D743EF881F`;
PDB `E7FD82734033DD1E46D52F9F10C023D5E00E9C3D9BC3C625F32C76C76F917A90`.
Archive:build/single-pass-20260927/deploy-staged-residency-606. Its local C++/GPU
suite passes192/192. Settings hashes remain those of600.

SDK alternative checked against primary documentation: MakeResident takes the
physical owner (committed resource or placed-resource heap), adds a counted
residency reference, and must precede GPU access. Its batch failure is atomic.
Only the extra acquired references may be balanced with Evict after completion;
submitted work with failed signaling must retain them. This is a separate proof
from native managed-object membership or immutable contents.
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-makeresident
https://microsoft.github.io/DirectX-Specs/d3d/ResourceHeaps.html#make-heaps--other-objects-resident

## Owned Physical Residency (2026-10-07)

Build610 adds an authenticated physical-owner accessor to NativeTextureRead and
a counted SDK residency lease. It deduplicates physical pageables but retains
every native allocation/allocator pin. Acquisition failure is atomic; potential
submission is marked before execution; the exact queue signal is associated
afterwards. Cancel is allowed only before possible submission. Retirement rejects
pending fences and UINT64_MAX; missing/failed completion quarantines ownership.
Each acquired reference gets one eviction attempt. Uncertain accounting disables
further acquisitions; it never triggers speculative retries or early release.

The existing completed-source renderer exercises this lifecycle. Successful and
intentional input-rejection reports both require fully retired, error-free leases.
Early failures/reset now cancel immediately or quarantine a potentially submitted
lease instead of waiting indefinitely for a future Work replacement.

Original-schedule608 passed24/24 but admitted only128 early packets:130 MAIN
instance views failed the current-recording check after their list was recycled.
Build610 separates authenticated submitted-copy ownership from current recording
and GPU completion, without waiting on its fence. Early coverage must now include
every independently valid completed candidate; losing candidates is a hard error.
Normal609 on606 passed9/9 with diagnostics off and no crash.

Local610 suites pass195 C++/GPU tests and769 Python tests. New WARP/hardware
fixtures exercise an evicted committed resource plus two placed resources sharing
one heap, actual GPU copies, pin retention, deduplication, cancellation and fence
retirement; pure tests cover failed acquisition/signaling/eviction and device loss.

Coupled611 passes24/24. Initial248 source draws become148 executed diagnostic draws
with nonempty, bitwise-equal both-eye output. Its214 unique immutable source leases
resolve to123 physical pageables; all retire after the consumer fence with no error
or quarantine. The turned and deliberately rejected-input variants also pass.
No native draw suppression, common CPU preparation or production FPS gain is
claimed. GPU consumption still starts only after the legacy completed-source gate.

Installed610 DLL `F4B0005AA55A1A4C2C1FE28848F4A3540CD09456CBBBDD69C5E524B30810F2DC`;
PDB `5AC99105CED21B68E55827FC387733B706333232B4F761C2B8DBA464211D3985`.
Archive:build/single-pass-20260927/deploy-owned-residency-610. Same-frame recording
and submission of the early packet is the next integration step; the SDK lease
does not provide source ordering, content validation or native suppression itself.

Original-schedule612 passes24/24. It prepares244/244 independently eligible
packets before readback,122 per eye, with12123 material bindings and409 input
checks. Its214 immutable leases resolve to124 physical pageables and retire
without errors/quarantine. Normal613 passes9/9 with diagnostic controls off.
Both launches close the game without crashes/reporters and restore preview.
Final DLL/PDB and all four settings hashes match610's manifest; calibration
remains absent. No game, crash reporter or RenderDoc process remains. No commit
or push was made. The full permanent single-pass goal remains active.

Next: refactor the prepared renderer's command recording away from the later
native draw callback, record an owned list from the staged packet, then submit
that list after both certified original source submissions on the same queue.
Use the owned SDK residency lease through that consumer's fence. Preserve GPU
input/mesh predicates, per-eye order, source-copy final states and both-eye
color/depth/stencil comparisons. Do not substitute native scope membership for
the missing unmanaged-texture proof, or claim CPU-preparation/FPS savings while
the original native draws and diagnostic reference/fallback commands remain.

## Owned Current-Frame Consumer (2026-10-07)

Build614 adds an opt-in current-frame path. Retained BeforeSubmit constructs the
full early packet and records/closes an owned DIRECT list before the final source
submission, with CPU payload/readback/overlay processing still zero. DeviceHooks
calls its consumer submission after both actual source Execute calls and successful
queued source fences, outside RegisteredQueueOrderMutex. No completed source fence
or CPU readback is required for recording or submission.

NativePreparedPair shares the original GPU recording body with this owned path.
Current references use guarded early tables; selector acceptance gates reference,
paired and fallback draws. T4/T6 admission uses the exact staged copy/queue/recording
proof; the legacy renderer still requires completed copies. The consumer owns its
allocator/list, command-resource batch, additional SDK residency references and
completion fence. Unknown execution or signal failure quarantines the whole work
item. Native draw callbacks cannot consume the owned-mode Work.

PreparedCurrent reports recording/submission/completion separately and names the
owned destination serial as such. RetainedCurrent independently records the source
event gates. --prepared-current saves initial, repeat, turned and fresh fault-injected
results without writing a legacy PreparedPairRequest. --observe-current makes no
control writes and validates the entire comparison/residency/current-source chain.
Neither path suppresses the original native draws or shares their CPU preparation.

Build614 local suites pass195 C++/GPU tests and778 Python tests. Installed DLL:
`28EA07DC13558F4044850D6A428C0BB2C50FFE3C872AF3FD450D8FA9E64A3298`;
PDB `2892F30B71B9A49B7E9C157F27C49CEBBAA495E3D7A833544A24FA9CF61E2C16`.
Archive:build/single-pass-20260927/deploy-current-frame-614. Native run615 is the
first current-frame execution test; implementation alone is not a passing result.

Run615 records/submits248 packets with all source timing gates valid, but the
scenario fails at the bounded NativeStereoProbe completion check, before reading
the owned comparison. No crash occurred; the game closes and preview restores.
Review found SwapChain used all prepared activity to enable native live bindings,
including the owned list that needs no native callback. Build616 separates
NeedsNativeBindings from Active, with a regression fixture for inactive, legacy,
owned and invalid-argument paths. Missing queue/frame now fails as owned mode
instead of allowing a fallback to legacy. Failed current captures also preserve
native probe/live state and prepared current/comparison/residency counters before
cleanup.615 is retained as a failed scenario, not a shader-equivalence result.

Run617 reaches owned completion in source frame1:248 reference ->134 executed
optimized draws, zero both-eye differences, nonempty contributions, no native
callback and no CPU completion wait. SDK residency214 source leases/124 physical
pageables retires successfully. The scenario still fails its observer because it
mistakes actual native registrations0 for an asset-free pass. Build618 adds a
separate Current.assetLeases counter and mode-specific protection provenance;
owned mode must keep native registrations0 and match SDK leases independently.
This preserves truthful counters rather than pretending native registrations
occurred.617 is not a completed multi-capture/fault-injection scenario.

Build618 local suites pass195 C++/GPU and779 Python tests. Coupled619 now passes
the full25/25 scenario: initial248 ->147 executed draws, repeat248 ->136, turned
196 ->101. Each positive capture records and submits in its own source frame,
uses zero native draw callbacks and zero source CPU completion waits, and produces
nonempty bitwise-equal both-eye color/depth/stencil. The fresh fault-injected capture
executes0 reference/optimized draws with expected error29. Its residency also
retires normally. Initial/repeat/fault use214 source leases/123 physical pageables;
turned uses190/110. Native registration and suppression counters remain0.

Installed618 DLL `1F5597B1245BBFA5349838A21DB9D59FCE3598C01B63B918BE65003AC5C311F8`;
PDB `D581646AA49A5B2BA205092DD7FABE39E07787600C9DB8E0098132FD5894340A`.
Archive:build/single-pass-20260927/deploy-current-report-618.619 closes the game
without a crash/reporter and restores preview. This proves current-frame diagnostic
consumption, not replacement of native outputs or reduced native CPU preparation.

Original-order620 also passes26/26:240 ->136 initial,241 ->147 repeat,194 ->101
turned, and0 draws for the deliberate input rejection. Every positive comparison
is nonempty and bitwise equal; no native callback/source CPU fence wait is used.
All SDK leases retire and the game closes without crashes/reporters.

Next authoritative-output requirements from the code audit:
- Current targets zero color and import depth only; admitted source subsets omit
  indirect and other excluded work. They must never overwrite the native G-buffer
  as though they were complete. Capture full pre-interval color/depth/stencil and
  preserve all excluded commands in order, or reproduce the full producer chain.
- Existing retained Seal/ExportEye and BeforeBarriers/BeforeResources are pre-access
  export points. A bounded contiguous interval can use its true baseline and end
  before the next excluded draw, with an original rendering path on rejection.
- PairedPassFrontier preserves both PostGBufferOverrides -> Lighting suffixes and
  orders producer frontiers. Whole-list submission is too late for replacement;
  establish a real command-list or native continuation boundary after both producer
  prefixes and before either affected suffix. Child-job completion must be proved.
- For CPU sharing, PrepareStage and ProxyGather hooks locate preparation, but the
  saved native analysis narrows reusable mesh/material templates to1EA24C ->1EA780.
  Preserve per-eye distance/LoD/history/upload/streaming effects in1E9228/1E8778/1E9B88.
  Replaying draws or merely grouping two views does not share this preparation.

Legacy-callback compatibility621 passes24/24 and normal622 passes9/9 with all
diagnostic controls disabled. Both close without crashes/reporters and restore
preview. Final DLL/PDB and all settings match618's manifest; calibration remains
absent. No game/reporter/RenderDoc process remains. No commit/push was requested
or performed. The full permanent single-pass/FPS objective remains active.

## Complete Interval Baseline (2026-10-07)

The prepared renderer now retains a full GeometryTargetCheckpoint per eye,
including all three color targets and depth/stencil. Capture occurs at the native
pre-interval position after tracked writable-state validation. Its total private
allocation budget is64MiB/eye; failure rejects the diagnostic instead of substituting
zero colors. Both comparison outputs import that baseline, and post-consumer
readback compares reference, optimized and unchanged baseline planes.

Positive admission requires actual VS/PS/sample execution plus a real per-eye
color/depth/stencil delta. Deliberate input rejection requires no execution and
unchanged baseline, not zero absolute color occupancy. PreparedBaseline24Q and
RetainedBaseline16Q record these proofs. The GPU fixture includes a nonzero initial
baseline; this does not establish full native output coverage by itself.

Coverage audit of619/620: all84 indexed-indirect argument and full-IA snapshots are
complete, but none of those sources has all material inputs. Their extra missing
reads include VS t53/t54 Texture3D and t77/t78 buffers, plus PS t79 and the shared
PS t57/t69/bindless inputs. The22 direct material exclusions (20 turned) require
VS t10 buffer and PS t55/t57/t69/bindless snapshots or valid original read epochs.
Acquisition85 is broad preflight failure,87 fixed-owner/read-state failure; neither
may be reclassified as safe solely from format. Original-order620 also has slot7
UploadDeferred IA after NoProducer13, with no instance snapshot. These need producer
or upload-epoch coverage, not weaker staged validation.

The new NativeSubmissionFrontier probe observes58DC98 before1FAFA4 consumes the
native phase range. It copies slot/context/list/generation tuples, retaining actual
recording tokens where available, and correlates them at the real queue call.
Qualified graph labels additionally require the live owned declaration, unwound
synchronizer/frame owner, and NativeContextOrder proof. Unknown labels/tokens remain
unknown. The probe is default-disabled, preserves native arguments/return/jobs, and
exposes no execution permission. It is armed only for explicit current captures.

Saved619 suggests a concrete splice after batch443 index2 and before index3:
PEER discard109/slot84 at index1; MAIN discard26/slot85 at index2; then velocity
solid27/slot86, velocity discard28/slot87 and decals29/slot88 at indices3/4/5.
This is a candidate only until the live complete range, optional initial context
and remaining suffix contexts are matched. The existing original Execute loop can
retain native child completion without suspending recording or waiting on GPU.

Build623 passes196 C++/GPU and786 Python tests. Coupled624 passes25/25 with the
full baseline:248 ->146 initial,248 ->151 repeat,196 ->101 turned, with zero
both-eye differences. Baseline payload is74,977,650 meaningful bytes over10 planes;
approximately9.82M color components were already nonzero. Deliberate input rejection
executes no shaders/draws and preserves all baseline color/depth/stencil exactly.

The live frontier probe installs successfully and captures27 ranges/309 slots,
153 contexts and61 authenticated tokens;61 actual submissions match, with no
replays or overflow. Batch27 qualifies both producers and all suffix lists:
indices1/2 are nodes109/26 at slots84/85; indices3..7 are nodes27/28/29/30/32 at
slots86/87/88/89/91. All share invocation19 and source graph fingerprint
7242777683317427689. Index0 remains an unknown earlier prefix. This now supports
a diagnostic insertion after index2, but not native output replacement permission.

Installed623 DLL `8FB3583DCD4D0568AB20CB02C088574B050708D5A5956A5E258B93DD0AFDAABC`;
PDB `BBB7EEAB8E4703951CF9FF21910E768F6A8F5BAEECC233DEF2FCB76C60696F27`.
Archive:build/single-pass-20260927/deploy-full-baseline-623.624 closes without a
crash/reporter and restores preview. Code review subsequently fixed a separate
pending-construction exception path that could leave the observer enabled.

More exact material metadata from619's earlier same-process descriptor capture:
VS t10 is a full60MiB RAW R32_TYPELESS UPLOAD view, not a DEFAULT immutable buffer.
Its actual writer/range/no-overwrite epoch remains unproved. The t4-6 cached setter
layout is an8-slot0x44 bundle; extending its register arithmetic to10 would overrun
that layout. The R16_UINT textures are256x256 single-mip render-target-capable
DEFAULT resources. They require original-SRV-occurrence mutable snapshots; changing
format admission alone is insufficient. Consumer descriptor overrides also need
generalization beyond the current p23/count8 material table.

## Certified Prefix Insertion (2026-10-07)

Build625 adds --prepared-infix, requiring owned-coupled current mode. FindBoundary
joins the exact latest native batch to both producer recording tokens and graph
parents; every suffix list must be qualified to the same graph/range with a later
GPU slot. Unknown earlier prefix lists remain earlier; unknown suffixes, replay,
changed list arrays or mismatched parent/epoch/queue reject the insertion.

DeviceHooks executes the native prefix, acknowledges only that prefix, and submits
the private consumer outside RegisteredQueueOrderMutex while retaining the native
outer/ImageSubmission scope. It then executes and acknowledges the native suffix.
Constant-write timeline and preparation accounting are split at the same boundary:
an early prefix fence cannot certify unexecuted suffix writes. In-flight capture
ownership lasts until the suffix callback, even if the private fence finishes early.
Regression tests cover that lifetime and the prefix-versus-suffix timeline edge.

This mode still writes only private targets. It neither suppresses native geometry
nor exports an incomplete admitted-source subset over native G-buffers. Complete
baseline and insertion position are prerequisites, not full native replacement.

Build625 suites pass196 C++/GPU and789 Python tests. Installed DLL:
`4463C80865C8A2D3323E18810D7E046E08D3628024BB117F070E06D0332D94BE`;
PDB `DF7E483BFD5500804B1FFD655455A021EFDE1EEE85B6220B09504E377D4851C0`.
Archive:build/single-pass-20260927/deploy-infix-625. Run626 is the first live
prefix-insertion validation; implementation and parser checks are not its result.

Live626 passes25/25 including all four captures. The certified8-list batch inserts
after index2: three original prefix lists, one owned consumer, five original suffix
lists. MAIN/PEER indices2/1 and all matching graph identities are checked before
submission. Initial248 ->137, repeat248 ->160, turned195 ->100 execute with exact
both-eye color/depth/stencil equality from full baseline. The input-fault capture
executes0 draws/shaders and leaves the nonzero baseline unchanged. Residency retires
successfully in all cases. No crashes/reporters; game closes and preview restores.
The renderer still uses private outputs only; native suppression remains0.

Next native-equivalence candidate, measured from624/626: final stage9 tail is
84 PEER draws(ordinals94..177) and83 MAIN draws(271..353). The immediately prior
draws93/270 are indexed-indirect exclusions; no excluded draw follows either tail.
Every tail uses asset77 and has staged material/geometry ownership. Saved plans
contain64 paired tail draws in624 and74 in626, with no pair crossing the tail
boundary. This is167-source coverage within the unfinished whole-scene objective.

For authoritative comparison, capture a full baseline immediately before each
stage9 tail, preserving all earlier native work, then join prepared output to the
native exit planes already recorded by retained Proof/Seal. Current pass-entry
baseline is too early for that join, and prepared output has no exit-reference
input yet. The common-finer VRS policy must also be accounted for when comparing
against the actual original per-eye native VRS output; private-vs-private equality
alone remains insufficient. Only after native equivalence and valid export/fallback
are demonstrated can the corresponding original tail be suppressed.

Original-order full-baseline627 passes26/26 without prefix insertion. Legacy628
aborts before any render test because the game loses foreground before Continue
input; no crash is recorded, and cleanup closes the game/restores preview. The
same-build legacy retry629 passes24/24. Failed628 remains preserved rather than
being relabelled as renderer success. Review confirms prefix/suffix accounting
and capture-lifetime fixes after the live tests; no remaining concrete findings.

Normal630 passes9/9 with all diagnostics disabled. Game/reporter/RenderDoc are
closed; all four settings, DLL and PDB match625's manifest, calibration remains
absent, and preview is restored. No commit/push. The permanent single-pass goal
remains active: authoritative output replacement, common CPU scene preparation,
native redundant-draw suppression and measured clean-FPS improvement are unfinished.

## Stage9 Native Exit Comparison (2026-10-07)

The new opt-in --prepared-tail requires certified prefix insertion. It captures
a full checkpoint immediately before the first native stage9 direct draw per eye,
then admits only a complete final stage9 run: every later captured eye command must
be direct, staged-ready and free of source errors; truncated source streams fail.
Only ordinary list close or pre-access barrier seals qualify its native exit.

The owned consumer receives the exact native Proof/Seal readback layouts0 from
each source recording. After its own same-queue completion fence, it compares both
reference and optimized output directly against those native color/depth/stencil
planes. No additional native copies, source readback wait or VRS changes are added.
PreparedNativeExit24Q records per-eye/plane differences, with160Q bounded first-
difference samples. Positive native mismatches report43, not a passing native proof.
Expected input rejection29 still requires unchanged baseline and zero execution;
its differences from the genuinely drawn native exit remain observable.

This adds the missing authoritative comparison input, not native suppression.
The actual per-eye VRS remains unchanged, while the tested common-finer policy
is explicitly retained; any resulting native mismatch must be investigated rather
than silently accepted. Local C++/GPU tests now pass197/197, including shared
native-exit bounds/component/aggregation helpers.

Build631 also passes793 Python tests. Installed DLL:
`8D928D69EDD145A8A36E85CDCE0EEA772ECBFA774A498283C1189860E55C81E2`;
PDB `183AC4FA535258A7374C59CD4A6EE7AD55D5E0C1865561A1AF1536F73ACA3F67`.
Archive:build/single-pass-20260927/deploy-native-tail-631.

Live632 qualifies all167 tail commands and executes92 optimized draws. Both
private variants remain bitwise equal, but direct native exit comparison correctly
fails43:32294/47719 color component differences, while depth/stencil are exactly0.
Reference and optimized have identical native differences. First mismatches occur
at tile-aligned coordinates(1008,1184) and(464,1168); common VRS refines83/134 cells.
This suggests VRS attribution but does not prove it. The failed scenario is retained;
no native draw is suppressed, no native output replaced, and no crash occurs.

Microsoft's VRS spec permits one non-arrayed screen-space image and does not expose
independent per-viewport shading-rate state. Next diagnostic adds a separately
seeded original-PSO reference using each exact original source rate image, and
post-fence attribution of common-rate differences to changed cells. It must not
waive native error43 or treat the hypothesis as verified before measurement.
https://microsoft.github.io/DirectX-Specs/d3d/VariableRateShading.html

Build633 adds the unchanged-native-rate calibration and bounded tile attribution;
197 C++/GPU and795 Python tests pass. DLL:
`FE08D1291FAE66723AF229C8060F376F80F4B680E97C70853E8040B572B9C640`;
PDB `1C12CDEEB4AFB73D844FD0A63C0C7AA57ADF6ADD8F3A89F0ACA41C9A26F17FC0`.
Archive:build/single-pass-20260927/deploy-native-vrs-633.

Live634 proves the167-draw original-rate private tail exactly reproduces native
color/depth/stencil:0 differences across all10 planes. Common reference and paired
output remain equal to each other, but have34432/42822 native color differences;
all lie inside refined VRS cells, with0 outside. Both eyes' original maps are
uniform across their source draws, actual tile size16, map93x93, and the common map
is never coarser. Strict native error43 is retained; neither private output nor
native suppression is enabled by this diagnostic finding. No crash/reporter.

The user explicitly approves the common finer VRS map after seeing this result.
The next opt-in acceptance policy must retain raw native-equivalence counters and
require exact original-rate calibration, valid per-cell attribution, no outside
color differences, no native depth/stencil differences and no paired/reference
differences. Approval is not permission to accept unexplained mismatches.

Native-target export audit identifies the next missing contract. The engine's
1FE758 flushes actual barriers, clears native state maps, then calls Close; querying
those maps at Close/BeforeSubmit cannot recover final states. Retained observation
currently stops at Seal, and reason11 includes both barriers and resolve operations.
An export receipt must track actual post-seal barriers/writes/aliases through
successful Close, retain allocation identity and freeze exact final subresource
states. Committed and placed targets need explicit ownership treatment; immutable
shader-read leases exclude RT/DS. The unknown earlier prefix permitted for private
diagnostics must also have a noninterference proof for native export.

The valid insertion point alone does not grant COPY_DEST access. Any future export
must restore the captured final states before the unchanged suffix, and certify
native use/residency before prefix Submitted consumes source tokens. No such native
target write is enabled by the current approved-VRS acceptance policy.

Build635 freezes an explicit --allow-vrs-refinement opt-in per tail capture,
defaulting off. PreparedNativeAcceptance8Q distinguishes approved acceptance from
raw bitwise nativeEquivalent. Calibration, uniform original maps, componentwise
refinement, complete attribution, zero unexplained color/depth/stencil and exact
common-reference/paired equality are mandatory for the approved alternative.
Rejection29 never reports accepted native output.197 C++/GPU and799 Python tests
pass, including negative cases for each missing proof.

Installed635 DLL `77C997949634DEFA4DC0B2503468BD94670C3775EC71B8EE56F00C5A389D10CE`;
PDB `70660A7CD925F88CA426EBED116F5D4EB9E5F1BE0BFB4798414B35C43561F54D`.
Archive:build/single-pass-20260927/deploy-refined-tail-635.

Live636's first comparison passes the approved policy:167 ->97 executed diagnostic
draws, exact native-rate calibration and zero unexplained/paired differences. The
launcher then rejects its source-list join because its new tail reporter used native
side indices instead of array-eye indices. The corrected reporter explicitly maps
eye=1-side and retains nativeSources separately; tests now use independently specified
array-eye fixtures rather than copying the reporter output into expected input.
Saved636 evidence passes the corrected join offline; its aborted full scenario is
not relabelled complete. No DLL change was required for this Python-only fix.

Same-build637 passes the full25/25 approved-tail scenario. Initial167 ->97 and
repeat167 ->89 executed diagnostic draws; turned120 ->60. All three positive
captures have exact original-rate/native equivalence and0 unexplained/common-pair
differences. Raw common/native color differences remain recorded, accepted only
under the user-approved finer-rate policy. The fresh input-fault capture executes
0 draws and leaves baseline untouched; native acceptance remains0 as required.
All SDK leases retire. No crash/reporter, game closes and preview restores.
These counts exclude the original game draws and calibration/reference overhead;
they are not a production FPS result or native draw suppression.

Legacy638 passes24/24. Normal639 stops during loading when a new CrashReporter.exe
appears(PID24340,game PID21276), before single-pass tests. No PID-matched native
report or new ReportQueue directory is produced; Application events contain no
matching fault and the plugin log has no fault reason. Preserve639/plugin.log and
the failed launch evidence; its cause is unproved, not labelled random or attributed
to single-pass. The reporter is closed by existing cleanup.

The launcher now records reporter path/windows and, when no native report exists,
captures bounded live-thread evidence before reporter cleanup. That is explicitly
not an exception dump; helper timeout/failure still permits cleanup.25 QuickBoot
tests pass, including existing-report/no-helper and timeout cleanup cases.
Normal retry640 on the same635 DLL passes9/9 with no crash/reporter. Final DLL/PDB
and all settings match635's manifest; calibration remains absent, preview restored,
and no game/reporter/RenderDoc process remains. No commit/push. The full permanent
single-pass objective remains active; native destination ownership/final states,
original-draw suppression, shared CPU preparation and measured FPS gain remain.

### Native Target State Observation

Build641 adds a read-only target observer from the actual first stage9 draw,
including internal checkpoint/restore barriers, through native Close/submission.
It captures authenticated native resource/recording/creation metadata, not allocator
ownership. Generic OM-resource lookup includes private array views without making
them eligible for native whole-target capture. Draw/UAV/clear/copy/resolve/discard,
alias, split/unknown-command and reused-list cases fail conservatively. Local tests
pass198 C++/GPU and804 Python tests; no native export or suppression is introduced.

Live642 captures all8 targets and10 subresources with no initial identity rejection.
All targets are placed resources, each on its own observed heap at offset0.
Both source ledgers are then rejected by foreign-list events: global CPU observation
order is not GPU execution order. That refusal currently prevents local Close states
from being preserved. Initial/repeat prepared rendering still pass:167 ->93 and
167 ->91 executed diagnostic draws, exact common-reference/paired output, exact
original-rate/native calibration and zero unexplained differences under the approved
finer-VRS policy. Native suppression remains0.

642 completes16/25 checks, then fails HUD freshness before the turned capture.
The observer remains active beyond its requested frame, doing unnecessary work on
later commands. The game is closed by normal cleanup, no crash is reported and the
preview is restored. Do not relabel this partial run as a full pass. Follow-up work
separates local state from foreign uncertainty and bounds observation to the native
infix batch, without relaxing HUD readiness or granting export permission.

The access window needed for future native export is per target: its tail exit
through the insertion after both source lists. Commands before that target's source
are already represented by the GPU baseline; they do not all require exclusion.
The later source's entire recording, including its pre-stage9 prefix, must still be
checked against the earlier source's targets. Writes, aliases and premature reads
all matter. A source-local frozen state alone does not certify that window.

Read-only native audit:0x22318C dispatches the tagged owner at texture-record+8.
Tag1 releases the allocation at record+0x10 through its vtable; tag2 decrements a
custom control block instead.0x876714/0x876E98 create that control, with the underlying
allocation at control+8;0x8776E0 releases it when the control refcount reaches zero.
The numeric record+0x10 value must never be blindly treated as IUnknown. Creation
kind2/heap offset0 alone does not identify which native owner tag is present.

Build643 separates source-local states from foreign-recording uncertainty. Local
barriers/Close can now freeze independently, while aggregate Sources still refuse
foreign uncertainty and keep PrefixUnknown. SourceLocal reports local flags and
the first foreign event; Owners contains only the observed tag and owner number.
Neither report grants allocation/residency/export permission. Actual Execute
observation retires the tracked generation even after local validation fails.

Observation ends at the completed infix batch, or failure/cancellation. Finish
latches closed, invalidates in-flight captures and drops operational OM bindings,
retaining only the selected source evidence. Delayed callbacks or Begin cannot
reopen or mutate that finished window; Clear starts the next capture. Foreign
events during the out-of-lock initial metadata capture remain explicit uncertainty.
Final local suites pass198 C++/GPU,807 Python and25 QuickBoot tests.

Installed643 DLL `87418817C4518B484448234B12BDE479C2163FE6B567D52D2E6BD8937F239520`;
PDB `0488D6E8CF8ADB9F4C9854190523DCFBC3385041768DF72EF604EA1454894AE3`.
Archive:build/single-pass-20260927/deploy-target-handoff-643. Settings are unchanged
by deployment.

Live644 passes25/25, including turned HUD readiness and input rejection. Initial
167 ->107, repeat167 ->92 and turned120 ->60 diagnostic executed draws have exact
common-reference/paired output and no unexplained differences under approved VRS.
The deliberate input fault executes0 draws, leaves the baseline unchanged and
reports expected error29 without native acceptance. Native suppression remains0.

All four captures retain10/10 final subresource states,2 successful Closes,0 local
source failures and0 source resets. All8 owner tags are2. Both eyes' colors finish
RENDER_TARGET(4); MAIN depth/stencil finishes DEPTH_READ|NON_PIXEL_SHADER_RESOURCE|
PIXEL_SHADER_RESOURCE(224), whereas PEER depth/stencil finishes DEPTH_WRITE(16).
The unchanged suffix therefore requires different depth restore states per eye.
Local identity/pending submission validates in both sources; aggregate submission
eligibility remains refused due foreign events, and PrefixUnknown is not cleared.
This remains observation, not an export/access-window certificate.

644 closes the game via the normal-close timeout fallback, reports no crash or
reporter, and restores preview. The earlier642 HUD failure is preserved as failed
evidence; HUD readiness thresholds were not changed to make644 pass.

Normal645 passes9/9 with diagnostics disabled, no crash/reporter and preview
restored. The game is closed; no game/reporter/RenderDoc process remains. Final
DLL/PDB and all game/plugin settings match643's manifest; calibration stays absent.
No commit/push. The single-pass goal remains active, not complete.

Next native-output ownership check should first audit a borrowed submission epoch:
the saved ExecuteSubset code at1F5DDC calls the queue, returns from our full infix
hook, then calls A08838 to signal; bulk submission calls91F158 afterwards too.
This proves insertion before native fences, not allocation retirement. The missing
link is the upstream RT/DS tag2 owner/free/reuse path to those fences. Prove that
link before deciding whether a new intrusive owner pin is necessary. Source-local
frozen states, a per-target access window and residency are separate requirements.

### Output Access Window And Native Epoch

Build646 adds a bounded Reset-through-Close command journal. At the authenticated
adjacent-source infix it checks each source's post-exit suffix, then the later
source's entire recording against the earlier source's targets. Physical placement
metadata is captured while resources are live; the new observed-allocation query
also covers private resources without granting native owner/state permissions.
Actual OM writes, read/copy operands, barriers and opaque commands are observed.
Unsupported hook coverage permanently refuses qualification for that process.

The narrow shader exclusion requires every watched plane to remain exclusively
RENDER_TARGET or DEPTH_WRITE with no simultaneous access. Only the explicit retained
Proof scope permits diagnostic source copies and balanced restore barriers. This
is based on valid D3D12 resource-state use, not complete bindless descriptor scans:
https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12
The journal still grants no native output/export/residency permission by itself.

The native tag2 lifetime audit found Release1FA164 queues zero-ref handles into the
current retirement bucket.1FB564 increments outstanding native contexts and its
0-to-1 transition through A34594 clears/rotates that bucket. Bulk1F5E5C calls the
queue, then91F158, then91C59C; only the last outstanding context publishes the
retirement fence through91C894.91C4A0/91C814 permit reclaim only after completion;
6480E4/6483F8/6485D4/22318C eventually release the alias control/allocation.

NativeTargetLifetime now authenticates this same-invocation borrowed epoch with
37 native code chunks, exact target/control/heap/allocator identity and the native
recording lease. A source handle must start positive; zero references at Bind are
accepted only with exact current-unfenced-bucket queue membership. ContainsRecording
ties the lease to the exact live native ExecuteSubset list array, independently of
resource residency. No native AddRef, native writes or long-lived owner pins occur.
NativeTargetLifetime acceptance is observed before and after prefix execution,
but is not yet used to enable output writes. Audit exports:target-lifetime-audit.

StereoTargetArray now supports separate before/after states per native plane. WARP
and hardware fixtures cover mixed depth/stencil states, differing restore states,
and predicate0 preserving destination versus predicate1 copying selected eye data.
Barriers remain unconditional, as required by the DX12 predication contract:
https://learn.microsoft.com/en-us/windows/win32/direct3d12/predication

Local verification:201 C++/GPU,810 Python,25 QuickBoot tests pass. Installed646 DLL
`5D85787623E50F0878FCD6D2242C42C1DB53A8C652CF5FD246A57072BAE33A3F`;
PDB `963620243D2AFF6DDE3FD9031362F2EA2FFA5172B2D68F4464BE175E37671548`.
Archive:build/single-pass-20260927/deploy-target-window-646. Deployment preserves
all settings; live verification follows. No native export or draw suppression yet.

Live647 passes25/25 rendering checks, closes without a crash/reporter and restores
preview. Initial167 ->97, repeat167 ->86 and turned120 ->60 diagnostic draws retain
the existing image/input-refusal guarantees. New output qualifications remain
refused: the journal exhausted256 recording slots/16384 rows/4096 markers after743
Resets, with only1 selected source registered. All8 lifetime captures failed the
code contract. No native write was attempted on either failure.

Follow-up648 recycles old NONsource generations on successful Reset and only the
oldest closed NONsource slot when new-list capacity is exhausted. Source generations
remain retained and cannot be overwritten; discarded row/marker budgets are
subtracted, with no limit increases. Regressions exercise1024 repeated Resets and
768 capacity evictions through the same slot-selection helper used at runtime.
Proof scopes now explicitly name the3 colors+depth, ignoring any stale fourth MRT.

All37 lifetime hashes match the installed EXE on disk, with no intersecting PE
relocations or known repository detours.647 pending dumps omit all guarded code
spans, so the live mismatch is not yet identified.648 caches exact failed RVA and
expected/live hashes in stage2 diagnostics; the acceptance guard is unchanged.

Installed648 DLL `2A162FB25B8520000C631CAC4DDCFA5332F9FCCDC0B53128D401D3A8486CEB76`;
PDB `04839CC899CA0B43F022DBFC4B1FE769BD37264DED65BCCF32434EF5DEF78290`.
649 stops before tests at the bounded Continue-input limit, without a crash.
Read-only memory sampling during649 proves36/37 native code spans match.87466C
has only a5-byte entry jump changed, through a MinHook relay into installed
cyber_engine_tweaks.asi+2BD10. Evidence:target-lifetime-audit/live649-contract-mismatch.json.
650 same-build retry passes25/25. Journal slots remain67 after774 Resets, both
sources are captured, but retained events still hit16384. Lifetime rejection now
correctly names87466C. Neither new output qualification is claimed successful.

Build651 adds pressure reclamation of closed NONsource journals, preserving all
open/current/source events and unchanged limits. The swapchain exclusion audit
checks all32 slots against each target and verifies table/lock/reference metadata
through the borrowed epoch; its factories allocate fresh texture handles.49 native
body/chunk hashes match disk, including the unmodified resize body suffix. The CET
entry itself is not authenticated by this structural check, so it must not alone
enable native exports. A bounded known-wrapper/trampoline validator is pending.

Installed651 DLL `FF921803D00049B603D97EEE9C7F2C75ED7ED23708EB356DCAB7A4B4413B6A81`;
PDB `02772403B5053017319FD6CA4B852F1B4D9D926824BD83A140C316C2C6AE6409`.
Local201 C++/GPU and810 Python tests pass.652 again stops at Continue before tests;
startup evidence retains visible loading supervisor with native mode2. Game cleanup
closes it with no crash/reporter. Readiness remains unproved on that run. Do not
classify this startup failure as an output-access or rendering success.

Build653 bounds CET compatibility instead of ignoring an arbitrary detour. The
original complete native resize body is accepted, or the exact observed E9/FF25
chain to the installed CET PE/wrapper/ResetState and seven-byte original trampoline.
Every chain validation rereads the code spans, PE metadata and pointers coherently.
Live654 read-only sampling confirms the expected singleton, wrapper hashes and
trampoline rejoin at874673.48/49 native guards match; the additional mismatch is
CET's shutdown detour at10E02B4. Its known wrapper is CET+2BE10, original pointer
at singleton+2418, with six stolen bytes and rejoin10E02BA. Evidence files are
live654-known-cet-chain.json and live654-known-cet-shutdown.json. Native writes
remain disabled; compatible resize alone does not authenticate shutdown.

Installed653 DLL `78A8EB2CEC92D1F651C5F18F19203A422D682F75848C2D02BB0F92E42FD27129`;
PDB `1B86EC479D655A2E22E276EDD0AA1394056C7E9485FC4DE779E2EFCD11E484C5`.
201 C++/GPU and810 Python tests pass. New QuickBoot checks pass28 tests.

Continue readiness audit: the loading supervisor can be visible with a valid
player before keyboard acceptance. InitialLoadingScreenLogicController initializes
skipButtonPanel hidden;1446DC0 registers Continue input and shows the panel only
after both controller+701/+702 flags. QuickBoot now inspects that exact property
through a bounded current visible widget tree, with unknown/errors refusing input.
The first654 integration incorrectly required inkLoadingLayer.mode3; live evidence
showed the authenticated ready panel at layer mode2. These are separate enums:
the controller loading state3 is not the layer's mode3.654 therefore times out with
no input, no tests and no crash. The corrected Python conjunct requires fresh
layer mode2 AND explicit current panel proof. World/HUD/focus checks are unchanged.

Normal655 on installed653 passes9/9 with the corrected QuickBoot script: one
Space press after authentic panel readiness, native menu0 and94 HUD submissions
across1046ms. Focus game after5.11s and Codex1.15s later is verified. No crash or
reporter; game closes and preview restores. This verifies the startup correction,
not the not-yet-compatible shutdown guard or the output-access journal.

Build656 authenticates BOTH known CET hooks with one bounded signature-parameterized
reader. Resize restores7 stolen bytes; shutdown restores6. Native untouched bodies
remain accepted, while unknown entry opcodes/relays/modules/wrappers/trampolines
are refused. Native/CET code, PE metadata and dynamic pointers all require coherent
second reads. Read-only654 evidence matches the accepted installed CET implementation;
this compatibility check does not replace the separate allocation/access proof.
Local201 C++/GPU,810 Python and28 QuickBoot tests pass.

Installed656 DLL `BE0C9016A37610372F3EAEDB74541C07C81E2EFB30C1B2C04A07E59063CB2F74`;
PDB `47C128C7285E6C5A642E677EEDBFB2FB694F332958A87B3221488F277E97E6CB`.
Archive:build/single-pass-20260927/deploy-target-window-656. Settings unchanged on
deployment; live target-window657 follows. No native copies/suppression enabled.

Live657 passes25/25 with the new authentic Continue gate. All8 native target
lifetimes are captured, bound to their exact invocation and validated after prefix
execution in ALL4 captures (initial/repeat/turned/input-fault). All reportfailure0.
The access journal still correctly refuses: at its peak, protected/open histories
fill16384 rows. Diagnostics now retain both source identities and lengths, including
the full PEER7311 rows and truncated MAIN1283 rows in the first capture. There are
70 retained recording slots,1343 resources and3221 markers, so stale slot growth is
no longer the blocker. Later row reclamation does not erase an earlier overflow.

Build658 therefore raises only the measured event budgets to32768 rows (about1.2MiB)
and8192 markers. Resource2048/recording256 limits, source/open/current protection,
and fail-closed overflow remain unchanged. Tests derive pressure cases from these
constants. This is a diagnostic budget, not an FPS claim or native write enable.

Live659 passes25/25 with both complete source histories and no overflow:
30978rows/5010markers, MAIN7586rows, PEER7677rows, wholeReset/Close coverage inboth.
The next rejection is Unknown atMAIN row7584, near the end of its source list.
Lifetime remains8/8. Build660 adds schema2 FailureEvent telemetry without changing
admission; live661 confirms kindUnknown/tag13 atMAIN row7595. Rendering tests again
pass25/25 and cleanup closes the game without crash/reporter.

Native Close1FE758 calls breadcrumb functions1FF0D0 and1FF9B0. Their vtable-slot66
WriteBufferImmediate calls return at1FF1FA/1FFA22 and write marker values/modes1/2.
The breadcrumb global entry is3493DD8+0x70*kind: heap+30,resource+38,GPUVA+40,
slotStride+20. Context+48 holds its marker object; slotGPUVA+60 andslotId+68 are
assigned by6DB8C0.87573C creates the dedicated placed breadcrumb buffer atoffset0.
Heap-tier2 targets may haveheapFlags0, so DENY_BUFFERS cannot be assumed. Next work
resolves only these authenticated calls to the actual buffer and physical span,
then uses normal overlap checks. Unknown callers/writes remain refused.

Installed660 DLL `FFE93802C30A4261C05F25AF5709EDDC0D6B77B7F0D96B0CB60FEEE2C6FCA12E`;
PDB `8139A2DC88D4C78E6ACA64580D503129D1AF0EC666FD41FDC03C074AFDC0CC27`.
Local201 C++/GPU,811 Python tests pass;28 QuickBoot tests remain current. No native
output copy or draw suppression has been enabled. Full single-pass goal is active.

Build662 adds NativeBreadcrumbWrite with17 audited native body/chunk fingerprints,
exact caller/mode/value and occupied-slot checks, and held physical buffer metadata.
Native-factory provenance remains distinct from API creation receipts. Schema3
reports resolution/fallback counts.202 C++/GPU and812 Python tests pass.
Installed662 DLL `A72CC8A30AB843A477987939D2DA0B109BE1EFEC79B2D3B567EE643E80754130`;
PDB `BB1A53580F3D8BAC659DA9957988C6D345038FC1091B1AB50EE31105335194C6`.

Live663 passes25/25 and keeps lifetime8/8, but window admission still refuses the
late MAIN marker.84/702 breadcrumb writes resolve;618 fall back, last failure5
identifies the unavailable current TLS context at native Close. This is not a
reason to ignore the marker. The exact native frame provides its actual holder:
RDI at1FF1FA(IN), RBX at1FFA22(OUT); each points to the marker holder, not directly
to the marker. Follow-up removes the invalid TLS assumption and obtains that
holder through OS unwinding, with exact native-stack parameter/mode agreement and
coherent before/after reads. No arbitrary stack scan or historical VA lookup.

Build664 implements that native-frame route. IN parameters/mode are atRSP+20/+50
withRBP=RSP+30; OUT usesRSP+20/+40 and ignores inheritedRBP. The holder is recovered
only from the audited nonvolatile register after OS unwinding to the nearest exact
game return. Actual runtime-function bounds, native-stack parameters, holder and
physical metadata are rechecked; struct padding is not compared.
Local202 C++/GPU,812 Python and28 QuickBoot tests pass.

Installed664 DLL `A3887F3390ED6FB0EAE6314CA12F4859B56FF3DA82A3F3AD4A29C936879278BB`;
PDB `7675A5495357A6D88280966D20EEA8227439AE47901FA48E23E17E561533A021`.
Archive:build/single-pass-20260927/deploy-target-window-664.

Live665 passes25/25. ALL4 captures now qualify the adjacent-source access window
with failure0 AND validate all8 target lifetimes after prefix execution. Breadcrumbs
resolve702/702 initial,702/702 repeat,698/698 turned and702/702 fault; no unknown
fallbacks, stale generations or physical-metadata conflicts. Retained journal rows
are31006/31158/26552/30898, below32768. The initial MAIN suffix checks335 events;
the PEER-side check includes7925 events from its own suffix and the later whole
recording, including352 shader invocations excluded by exact write-state proof.

Prepared diagnostic draws remain167 ->93 initial,167 ->88 repeat,120 ->60 turned,
with exact common-reference/paired images. Fault inputs execute0 prepared draws.
Original game draws still execute; native suppression0, native output copy0 and
all public telemetry permission fields0. Window/lifetime qualification is not yet
the complete export transaction, permanent single-pass or a clean-FPS result.

Normal666 passes9/9, no crash/reporter, game closed and preview restored. Final
DLL/PDB and all game/plugin settings match664's manifest; calibration is absent.
No game/reporter/RenderDoc remains. No commit/push; full single-pass goal active.

### Native Output Export, Build667 (2026-10-08)

The export transaction is now implemented behind default-off
CyberpunkVR_PreparedNativeExportEnable. An opaque same-native-invocation receipt
authenticates the exact source journals and target states; public telemetry does
not grant access. Eight native target lifetimes acquire separate, authenticated
SDK heap residency. Material and target leases validate as one submission group,
so a later-member refusal leaves every lease acquired and cancelable. Duplicate
preparation after preparation/submission rejects without releasing live resources.

A second owned DIRECT list follows the prepared render list before the untouched
native suffix, under one owned fence. It copies all10 color/depth/stencil planes
with arrayEye=1-nativeSide, restores exact per-plane final states, and reads the
native destinations back for comparison. CPU access/lifetime failure omits the
export list. GPU input rejection predicates only the writes, not state barriers
or readback, preserving original native contents. Local suites pass204 C++/GPU,
819 Python and28 QuickBoot tests, including group cancellation and reentry cases.

Installed667 DLL `A43F8844294C942066096272A6AB4639A64BA288D1BE7BEF11B6B09ACE613EDF`;
PDB `E3B0D6288A2814FD2F6FA7A75A904E45F5FDD495A9019400551424EDA1D5A7CE`.
Archive:build/single-pass-20260927/deploy-native-export-667.

Live668 initial/repeat/turned/fault output reports all complete the native export
and compare10 planes,77552640 readback bytes, with zero differences for both eyes.
All8 target heaps acquire/submit/fence/retire successfully, and access/lifetimes
revalidate after the source prefix. The positive tests compare native destinations
against paired output. The fault test executes0 prepared draws and compares against
the original native exit instead: inputRejected=1,preservedOnInputReject=1,error0.
The expected pair-input error29 is separate from export success. Initial diagnostic
GPU draws are167 ->97. Original native draws still execute; nativeSuppressed=0.
This proves current-frame native output transfer, not production FPS improvement.

Next: suppression needs a genuinely earlier input decision or a complete deferred
original fallback independent of speculative selector inputs. The current GPU
input predicate is produced AFTER original source lists execute and cannot gate
those earlier draws retroactively. The existing source reference bank is guarded
by the same selector predictions; inverting that predicate is not a valid fallback.
Native exit readback also ceases to be an independent oracle if those originals
are suppressed. Production must remove redundant diagnostic redraws/readbacks and
measure clean FPS; the full single-pass goal remains active.

Live668 completes25/25 launch checks. Default-off regression669 also completes
25/25; all four captures report requested0 and no export work. Neither run
observes a crash/reporter; both close the game with the normal close-timeout
termination fallback and restore preview. A normal diagnostics-off launch follows.

Normal670 completes9/9 with all test controls zero, no crash/reporter and restored
preview. Game shutdown uses the same close-timeout fallback. Final installed DLL,
PDB and all game/plugin settings match667's manifest; calibration remains absent.
No game, CrashReporter or RenderDoc process remains. No commit/push was requested.

### Full T5 For Deferred Fallback, Build671

Live668 contains no finite-only shortcut: all167 final-tail sources use family1950
with B4(228 bytes) and T5(2048 bytes) predictions. Every T5 view covers the same
original1MiB resource,8192 records of128 bytes. The fallback therefore needs a
complete source view independent of those predicted record indices.

Build671 adds explicit FullOriginalView snapshots. Default selected-range limits
stay4KiB; full mode requires the exact original1MiB view and retains the8MiB arena
cap. Fresh actual-call/source/parent/state checks are repeated at every occurrence.
A same-recording no-write window may reuse only an internally sealed previous
copy, with identical source/resource/generation and held destination ownership.
Writes or unknown operations invalidate reuse; a new version consumes anotherMiB.
Finalize closes the reuse window, not the lifetime of already recorded copies.

The current-frame reference and paired banks both consume the full T5 view when
--full-t5 is selected. Sparse texture tables STILL require GPU prediction checks;
this does not yet make the entire fallback prediction-independent. Diagnostic
readback separately captures the ORIGINAL complete1MiB at EVERY occurrence and
compares it against the selected private version after the original source fences.
Those large reference readbacks are diagnostic, not a proposed permanent path.

Local suites pass204 C++/GPU,834 Python and28 QuickBoot tests. Installed671 DLL
`3995F3FC83FA06419A89CDB4CB654969D1A5F16C4732950BEFCC63968D454174`;
PDB `D520D31AF9DA6F1BBC31F8E694097E3BC0B109E5501FD46758C0CBC3689BCCC4`.
Archive:build/single-pass-20260927/deploy-full-t5-671.

Live672 passes25/25. Initial/repeat/fault each cover167 full views with2 actual
1MiB transfers and165 reuses; turned covers120 with2 transfers and118 reuses.
All independent original byte comparisons have zero differences. All4 native
output roundtrips still pass10-plane comparison; fault executes0 prepared draws
and preserves original native output. Diagnostic executed draws are167 ->104
initial,167 ->96 repeat. This is no native suppression or clean-FPS result.
The game closes with the close-timeout fallback, no crash/reporter and preview
restored. Default-off and normal regressions follow.

A separate opaque BorrowedSourceRanges no-overwrite witness is implemented but
not yet connected to native fallback execution. It retains exact root/list/heaps
and range versions; active witnesses keep version tracking enabled across native
submission. Descriptor mutation scopes bracket driver calls plus metadata updates
for CBV/SRV/UAV/sampler/copy and heap creation. Readiness requires all corresponding
hooks; default-off avoids atomic mutation updates. Session resets, overflow,
concurrent mutation, changed ranges or heap cookies invalidate the witness. Pure
policy/store tests cover these paths; live borrowed-table admission is NOT proved.
It grants neither native lifetime/residency nor texture state/content permission.

Default-off regression673 passes25/25, with no full-T5 work in any capture and
native export still correct. No crash/reporter; game closed and preview restored.
Normal diagnostics-off674 passes9/9 with all controls zero, no crash/reporter,
game closed through the normal close-timeout fallback and preview restored. Final
DLL/PDB and all game/plugin settings match671's manifest; calibration is absent.
No game/CrashReporter/RenderDoc process remains. No commit/push; goal still active.

Next descriptor step: bind the witness to the actual PSO/family rather than all
unused root declarations. The same shader pair has runtime id1888 in672 (id1950
in668), VS d13385d2... and PS9a4947a3.... Active finite intervals are p0[1],p1[0],
p2[0],p18[1],p19[0],p21[0],p22[4],p23[0..6],p24[0],p29[0..1],p30[3]. Only p31 is
dynamic, PS t0 space1. Reflection declares32768 descriptors; eight dynamic sites
use uint16 halves and two use raw T5 DWORD values, so declaration alone is not a
runtime bounds check. All ten sites come from T5 record DWORDs100/104/108 in the
audited family. An actual-data GPU bounds check can evaluate B4 reachability and
these indices without comparing them to CPU predictions. Do not check all unused
T5 records indiscriminately, since unused records may contain sentinel values.
Until qualified, retain the full native physical remainder of p31. Pending-bound
native managed-set membership needs a post-prefix opaque receipt, and unknown
selected textures still require lifetime/read-state/content/alias protection.

### Borrowed Table Observation, Builds675-681

The witness now joins the actual current PSO, Bound/Lookup family identity and
verified sourceRoot/shader uses. Only active finite intervals and the complete
physical remainder of each active unbounded declaration are retained. Live p31
is exactly32768 slots; the old32689 figure was the highest selected sparse extent,
not the full native table. Every source has12 active range intervals.

Live676 captured167/167 witnesses but rejected every pre-prefix validation. The
unchanged epoch was insufficient to identify the reason. Build677 added explicit
tracking failure masks and bounded native pageable-set searches. Live678 proved
heap-cap overflow: reasonMask32,167 pins,0 mutations,storeOverflow2,trackingFailure0.
Late private bank creation had entered the global1024-heap metadata store.

Build679 fixes the ownership boundary without raising limits: internal heap
creation stays unregistered; internal descriptor writes/copies invalidate and
version-stamp overlapping ALREADY tracked destinations, without reading private
source descriptors or registering their resources/origins. Real native callbacks
retain full canonical tracking. Tests cover2048 disjoint private operations,
watched writes, unrelated writes, malformed overlap and the real native heap cap.

Live680 passes25/25. Initial/repeat/fault validate167/167 source witnesses both
before AND after the original prefix; turned validates120/120. Initial2004 range
rows contain5475262 repeated descriptor references, not unique resources. One
unchanged dynamic table contains32768 slots:29599 authored nulls and3169 resource
SRVs, all weak-cookie-mapped at observation. Of these3158 are Texture2D,8 arrays,
and55 carry RT/DS/UAV flags. These are metadata counts, NOT complete read ownership.
All full-T5 comparisons and native10-plane output roundtrips remain correct.

Typed PendingPageableUse can now bind an independently authenticated resource OR
heap against a pending native source set and revalidate after token consumption
inside the SAME ExecuteSubset. It never grants texture state/content permission.
Live678/680 find no exact-source-set member for328 already held immutable asset
occurrences. The first rejected heap is also absent from all8 captured sets
(37 member visits,0 matches). Same-invocation union alone cannot solve that case.
Existing explicit SDK residency remains the proven path for prepared asset reads.

GpuMultilayerMembership is implemented and passes WARP/hardware fixtures. It reads
actual GPU B4/fullT5, checks all5 unique index transforms covering10 shader sites,
and produces per-source64-bit accept/reject predicates. It does NOT compare CPU
predictions. Producer0 disables both branches. This module is not yet connected
to game draw admission or native suppression.

Build681 adds occurrence-bound OwnedSrvCoverage for actual emitted sparse SRVs:
generated null holes are excluded, cache hits reissue current provenance and
unions retain all original contributors. CPU heap identity/shape/required owners
must match. SourcePass copies storageExtent; paired CreatePass copies count, so
coverage outside count cannot be assumed copied by that different route.

Build681 also implements opaque FixedAssetPins for a numeric weak candidate:
bounded registry POD lookup, verified byte try-lock, positive-only native ref CAS,
guarded atomic allocator/allocation AddRefs, then native ref release BEFORE unlock.
Resource COM AddRef occurs only AFTER unlock using the pinned allocation's owned
resource. Phase2 constructs ImmutableOwner from those exact pins and still requires
existing upload/history/physical/state checks. Original AcquireImmutable is unchanged.
Independent review and pure race/lifetime tests pass; UNUSED-ASSET LIVE PIN SUCCESS
IS NOT YET PROVED. Descriptor-cookie integration is a separate unfinished step.

Borrowed observational receipts now release after the native suffix (or completed
capture), preserving numeric reports without keeping descriptor tracking pinned
after diagnostics stop. No borrowed heap is used for GPU rendering by this observer.

Local681 suites pass207 C++/GPU and848 Python tests. Installed681 DLL
`08AD52637E0343DA4BDF47EE60333E756ECCB4F8219D2BB99C3A755A11FB0EF4`;
PDB `BE120C1A4EE1582DC0C1EAD7804095055C102F2EA24B60B595FAADF6C51B2410`.
Archive:build/single-pass-20260927/deploy-fixed-asset-pins-681.
Live682 passes25/25 after coverage/cleanup changes, with no crash/reporter, game
closed via the normal close-timeout fallback and preview restored. Normal683 follows.
Native suppression remains0 and no clean-FPS improvement is claimed; goal active.

Normal683 passes9/9 with all controls zero, no crash/reporter and preview restored.
Final DLL/PDB and game/plugin settings match681's manifest; game closed, calibration
absent. Descriptor-cookie integration below was added after that verified build.

### Full Fixed-Range Capture, Builds684-686

BorrowedFixedRange now snapshots canonical descriptor values and numeric weak
cookie candidates under the metadata lock, then releases that lock BEFORE native
pin acquisition. Only a safely owned resource is queried for its existing cookie;
no new cookie is assigned and no COM call uses an unpinned weak pointer. Exact
source recording/context plus range version/canonical values are revalidated after
capture. Each view is checked independently even when assets share a cookie.
Unknown, null, unsupported views/shapes, missing candidates and pin/cookie/immutable
refusals remain distinct. Coherent PARTIAL capture does not pass whole-range
Validate and grants neither SDK residency nor GPU/native suppression permission.

An initial684 fixture caught zero-mip null SRVs being accepted by the general
decoder. The fixed-range classifier now checks explicit2D/array null shape, mip,
plane, array and LOD fields. AS/buffer/implicit descriptors cannot become typed
nulls. Python's first draft also confused inventory null index10 with other-kind
index12; this was fixed with unequal-counter regression cases before live685.

Live685 passes25/25. Initial range covers32768 slots;3111 candidate assets were
tried and3110 immutable resource/cookie/allocation/upload proofs succeeded. One
pin refusal is native handle32757, cookie20, slot32756, bootstrap failure124/detail3.
Other slot classes:29599 UnsupportedDescriptor (previous broad inventory calls
these nulls),55 UnsupportedShape,3 UnsupportedView,0 unknown/missing candidates.
The range is explicitly incomplete. Existing renderer and10-plane native export
still pass in initial/repeat/turned/input-fault captures. No crash/reporter, game
closed via close-timeout fallback, preview restored.

Joining existing source selector evidence shows none of the55 flagged entries or
3 unsupported views selected in this capture; only slot32756 occurs among selected
unsupported entries (106 material occurrences across the larger capture). This is
observed selection, not a guarantee for other scenes or a fallback permission.

Build686 adds bounded canonical descriptor samples (at most4/status/range) and
coherent POD-only native identity details for bootstrap rejection3. These reads
grant no pins/access. It also retains genuine CBV PreparedCopies in prepared
sources and adds MatchesRecordedPrivateCbv, which verifies the opaque recorded,
finalized arena receipt and exact private-view/source occurrence without CPU waits.
PreparedMembershipBank builds an owned-input bitmap from actual SourcePass SRV
coverage intersected with held material leases/cookies. It is compiled but NOT
yet connected or positively verified in the live GPU admission path; its standalone
test currently covers bitmap bounds only. Do not equate it with a complete fallback.

Local686 passes208 C++/GPU,863 Python and28 QuickBoot tests. Installed686 DLL
`11651FE56323C23960E517FCFDB12FA2B4F3DE6AF358A5D7CD9689221FCB6F57`;
PDB `5F34C7B3F791CA938C69B1B38939BF4ED0CC8FE1B42FCE14B9CA4E20678B795F`.
Archive:build/single-pass-20260927/deploy-fixed-diagnostics-686.
Live687 passes25/25, no crash/reporter, game closed and preview restored. Initial
3110 immutable assets are retained again; no whole-range qualification is claimed.
Canonical null samples are explicit resource-null Texture2D R32_UINT descriptors
with mapping5768,MostDetailedMip0,MipLevels0,Plane0,Clamp0. The fixed-range classifier
currently rejects these zero-mip views; a dedicated GPU/API test is required before
deciding whether that restriction is appropriate for null resources. Do not silently
relax it based only on the game's descriptor use. Microsoft's API distinguishes
null-resource descriptors from default-view descriptors and requires valid pDesc:
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createshaderresourceview

Slot32756's coherent POD sample is native handle32757,refs2,owner tag1,flags0x81,
binding0xC0,2x2 RGBA8/oneMip/resourceFlags0. This lacks fixed-state bit0x40, unlike
accepted0x41/0xC1 flags, explaining bootstrap rejection3 without implying a stale
pointer. A separate read-state/content contract is needed, not a fixed-flag bypass.
Normal688 passes9/9 with every test control zero, no crash/reporter and restored
preview. Game is closed; DLL/PDB and all game/plugin settings match686's manifest.
Calibration remains absent. No RenderDoc/game/reporter process remains.

Independent null-descriptor fixture689 passes WARP and hardware: exact native
Texture2D R32_UINT/mips0 and optional array0/mips0 both produce8 zero loads with
8 nonzero real-texture controls and8 written execution markers. Normalized mip1
and array1/mip1 controls also pass. The debug layer does not reject these null
cases on this machine; the only warning is1328 about buffer initial COPY_DEST.
This is an implementation observation, NOT a portable zero-mip qualification.
The Microsoft binding specification explicitly requires plausible numerical fields
even for null resources:
https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html#null-descriptors
Keep the classifier conservative; a future owned bank can explicitly normalize
authenticated null-resource descriptors rather than claim unchanged native bytes.

Slot32756 appears106 times only in earlier stages(84 indirect stage0,22 stage7),
ZERO times in the complete stage9 tail tested by prepared single-pass. It must not
block stage9 progress, but is required when broadening pass coverage. Static native
evidence376E1C/377790 and update21EB24 proves flags0x81 can be written later, even
with resourceFlags0, so never admit it as fixed. A separate original-occurrence
tracked snapshot is the proposed route for that2x2 resource. The current goal is
still incomplete: no original-draw suppression, permanent pass or clean-FPS win.

Final suite689 passes210/210 C++/GPU tests (including the new null diagnostics).
Default-off690 passes25/25 with fixed-asset counters exactly idle in all4 captures,
borrowed-table validation and10-plane export still passing. All controls return0,
no crash/reporter is observed, game closes via close-timeout fallback and preview
is restored. Final DLL/PDB and all game/plugin settings match686's manifest;
calibration absent. No game/CrashReporter/RenderDoc remains; no commit/push.

Next concrete integration is PreparedMembershipBank -> GpuMultilayerMembership ->
GPU AND over actual per-source acceptance, while preserving the original draw path
on rejection. It must bind the SAME private B4/fullT5 and exact owned p31 descriptors
as the consumer, respect SourcePass storageExtent versus paired count, and retain
immutable SDK residency. This removes CPU expected-byte equality from admission,
not the separate native suppression/fallback requirement. Never treat a missing
bitmap member after original-draw suppression as a valid no-op fallback.

### Actual GPU Membership Admission, Builds691-693

Default-off --actual-membership now connects PreparedMembershipBank to the actual
prepared consumer. The GPU reads each source's authenticated private B4/full T5
and checks all reachable texture indices against its owned descriptor bitmap.
The exact p19 B4 is explicitly written into the reference-bank CPU descriptor;
paired B4 uses the same receipt-validated view. Both paths use full private T5.
Paired p31 joins reference/source CPU table identity, coverage and merged copied
count; a source's maximum bitmap bit must fit that actual paired count.

Membership executes BEFORE a GPU AND of its per-source accept QWORDs, BEFORE mesh
comparison and every prepared rendering consumer. AND compares N QWORD results
against N literal-one QWORDs; these uploads are predicate constants, NOT predicted
shader data. Old expected-byte checks remain unchanged only in the default mode.
The actual-mode fault clears all bitmaps without altering B4/T5, requiring a real
MissingDescriptor rejection. Zero-mask sources may legitimately accept; the global
fault case must reject at least one actual selector and execute no prepared draws.

Live692 passes25/25. Initial/repeat167 sources all accept; each covers10521 repeated
owned slots with0 omitted,2672 records and13360 selector visits. Turned120 sources
all accept(1920 records). Fault167 all reject withMissingDescriptor, global AND0/1,
0 prepared draw execution and exact original native10-plane preservation. Positive
10-plane native exports remain identical. Initial diagnostic executed draws are
167 ->111, still in addition to untouched original game draws. No FPS claim.

Build693 groups membership compute work by the actual pair of B4/T5 buffers.
Immutable32-byte source metadata retains original output/bitmap indices while
stable first-seen resource groups issue one dispatch each. Per-source readback
remains48 bytes. ABI schema2 reports exact resource-pair dispatch count; schema1
historical reports still mean one dispatch per source. WARP/hardware tests verify
interleaved groups, distinct offsets, GPU input mutations, zero gate and the actual
membership -> AND -> predicated consumer chain without intermediate CPU waits.
Local693 passes210 C++/GPU,875 Python and28 QuickBoot tests.

Installed693 DLL `8756856E9E5201128AF15C73D3E8EF134C3809187921DA54D623AA09A3B02DB5`;
PDB `FA52AF6A8865689C0993BFED99DABDFB53A115F0040FE0435D7C641918D0FA56`.
Archive:build/single-pass-20260927/deploy-actual-grouped-693.

Live694 passes25/25. Initial/repeat167 sources and turned120 sources now issue
exactly2 membership dispatches, all accept. Initial/repeat check2672 records and
13360 selector visits; turned checks1920 records. Fault167 sources all reject,
with0 prepared draws and exact native10-plane preservation. Positive exports
have0 differing pixels on all10 planes. No CPU admission waits or native draw
suppression. Default-off regression695 also passes25/25. Both runs close the
game with no crash/reporters, restore preview settings and leave no processes.
Normal diagnostics-off696 passes9/9, closes without crash/reporters and restores
preview. All four persistent settings hashes remain equal to the693 manifest.

Next elimination boundary, based on native-code review:23A938 iterates48-byte
stage definitions(stagenumber+16,technique+20) and calls1D57280 ->1D57210(packet
build/sort) ->1F1208. The latter still runs1F0418 per-packet preparation,1F1A88
instance appends and1F68D0/1F2098 batch flush, so gather completion is NOT proof
that all inputs exist before draw. Current stage9 has31 distinct DEFAULT kind1 B4
pool slices and one original T5 buffer; moving their copies earlier still requires
proof of prefix writers/states. Proposed route: split stage9 at graph-definition
level, put admission after BOTH eye prefixes and before either original tail.
Original native tails can then be GPU-predicated fallback, paired tail on accept.
Whole-node before/after callbacks(+32/+40), stage callbacks and suffix edges must
execute once in their original roles. Closed D3D12 lists cannot be edited/split.
Common CPU preparation remains separate at1EA24C ->1EA780; preserve per-eye
LoD/history/streaming and shader-registration side effects. No such split exists yet.

### Native Stage Definition Observation, Build697

NativeStageDefinition captures each reachable RenderElements definition's full
64-byte header and all48-byte stage entries, twice, while the original tree is
held. It authenticates the exact graph root, child path, native type and array;
opaque padding, callback pointers and prebuilt-list partitions remain unchanged.
Owned execution publishes a bounded128-byte definition row plus contiguous stage
bytes for each eye's GBuffer_Discard root. The reader joins source graph and
consumer/epoch/pair identity. Failure is observation-only and cannot alter native
rendering. Rows enumerate unique reachable definitions, NOT execution order or
path multiplicity; a future split must preserve SceneDriver child-handle order.

Static review found NO top-level23A938 node in694's155-node graph: RenderElements
are children of1EC1D0 SceneDriver. Constructor1D428C0 ->984AC4 sets the ordinary
discard child's gate1D41BE0, before/after null. Other children still require capture.
The parent handles context begin1F0F80, child dispatch1EC404 and finish1ECFB0.
Splitting stage arrays alone therefore cannot create a native queue boundary.

Local697 passes211 C++/GPU,902 Python and28 QuickBoot tests. Installed DLL
`5FED9A7C875F13144C6505DCE58890D6BBF40CE0CDB0461D19C21F7D82BB88F7`;
PDB `556ADADABC81BBED25B1F486BC5CF853E209ECE1ED188CA45207A55FB08816B3`.
Archive:build/single-pass-20260927/deploy-native-stage-697.

Live698 passed25/25 but the reader's resolver pattern omitted OwnedStage exports,
so it did NOT record stage evidence. Fixed the pattern, added a mocked-main
regression requiring all three exports before process access;903 Python tests pass.
Repeat699 on the same DLL captures4 definition rows/12 entries. Both eyes share
the same SceneDriver and the same two RenderElements objects. Child1 is[7/tech2,
7/tech0], gate1D41BE0; child2 is[9/tech2,9/tech0,8/tech2,8/tech0], gate1134C40.
Both children have null before/after and all stage callbacks null; every prebuilt
index isUINT_MAX and partitions are0/0. Crucially, stage9 is NOT the declared
final stage: stage8 follows, even though it emitted no draws in this scene.
Do not remove stage8, infer permanent emptiness, or equate the observed draw tail
with the definition suffix. Any execution split must preserve these entries and
their original order. Full699 passes25/25, including initial/repeat/head-turn and
forced input rejection. Positive membership accepts167/167/120 sources with2
dispatches each and exact10-plane exports; fault rejects167, executes0 prepared
draws and preserves the native image. Initial diagnostic pair draws167 ->90 are
still additional to originals, not a native suppression or FPS result. The game
closes without crash/reporters and preview is restored. All four699 stage
observations are verified. Normal diagnostics-off700 passes9/9, also closes
without crash/reporters, restores preview and leaves no game/reporter processes.

Early admission needs a separate snapshot prelude as well as prefix/tail native
recordings: current input copies are injected immediately before EACH native draw
and finalized at source Seal. Native1F1208 interleaves1F0418 item preparation,
1F088C uploads and1F191C batch draw flushes. Do not execute that function twice.
Proposed GPU order: both prefixes -> proved input snapshots/membership -> original
tails on rejection -> existing late paired-render/export on acceptance -> suffix.
The late export/state/lifetime receipt stays intact. A new early receipt must
certify four native recordings; lowering the current late afterIndex is invalid.
First execute this split with all original draws enabled, then compare early
shadow snapshots byte-for-byte to existing inline copies before allowing skips.
An unconditional owned reference using the same sparse banks is NOT a fallback
for missing membership entries; the untouched original native tail is required.

Native context review favors owned graph-node splitting over manual Close/Reset.
1ED074 ->1FB564 ->1FD49C acquires a genuine new context/allocator/residency set;
1EC68C only switches TLS and does not restore bindings. 1FAFA4 reconciles adjacent
context+0x538 state histories viaB9C6D4 ->1F4C78 BEFORE Close. 1FE758 clears these
histories and closes residency, and1FA82C returns the context to the pool. Calling
Close early would discard required reconciliation evidence. 91EED4 immediately
submits/retires one context, so it is not a local split. Native1EC404 publishes one
handle per producer slot; adding a second raw list would bypass its provenance.

Smallest proposed execution fixture: split ONE eye into owned prefix and tail
graph nodes, retain the existing eye order, execute every original draw once and
prove three distinct native recordings with normal setup/residency/reconciliation
and identical final10-plane outputs. Then split both eyes/reorder both prefixes
before both tails. The late block is setup1EDD64,bind20C3AC,RenderElements[9,9,8,8],
unbind20C02C,endA2BABC. Keep this whole block and stage8 continuation semantics;
the declaration contains more than the current scene's observed draw suffix.
Before implementing expansion, capture child-handle order AND multiplicity, not
only unique RenderElements rows. New SceneDriver definitions require explicit
clone provenance, native child ownership and allocator/destructor handling. The
current OwnedNativeGraph validator rejects changed node count/identity; original
owner membership and declaration/resource-replay joins likewise cannot accept
new nodes implicitly. Extend these contracts explicitly, never bypass them or
mutate the shared original definition used by both eyes. Setup definitions and
their parameters(7/0x200010 in static review) still need an exact source capture.

### Scene Sections And Expansion Foundation, Build701

NativeSceneSections now captures the complete56-byte source SceneDriver and its
eleven ordered16-byte child handles, including repeated references. Strict
profiles split the existing list6/5; the late RenderElements retains[9/2,9/0,8/2,8/0].
Setup/end parameter7 and bind/unbind parameter0x200010 are verified. Special
77D214 is allocated as24 bytes, not32: its observer reads only24 and zero-pads
the numeric report. Full RenderElements headers/stages and source tree are joined
and rechecked. No native object or execution order is modified by this capture.

Live702 proves the exact shape in all four captures, including head turn and
input rejection:2 source roots,22 child rows,split6/5, no observation failure.
Both views share the original parent/children, so changing that parent in place
would change both eyes. Full702 passes25/25; membership2 dispatches with167/167/120
accepted source rows, fault167 rejected, all10 export planes equal and original
native output preserved on rejection. Native suppression remains0. Normal703
passes9/9. Both games close with no crash/reporter, preview restored, no processes.

SceneGraphExpansion is a new source-bound topology helper. It appends one tail
node while preserving every original handle/ordinal; only the chosen eye's root
object becomes the prefix view. Incoming edges stay on prefix, outgoing edges
move to tail, and both phases gain prefix->tail. Exact contraction restores both
source edge sets. Source adjacency, ranks, node/object bounds and56-byte overlap
are checked; the other eye's shared source object remains unchanged. It creates
an execution template usable by unchanged OwnedNativeGraph::Create, not permission
to publish it. Native edge handles may be untagged, and embedded edge ordinal is
NOT the physical edge ID; actual699 data and fixtures cover both distinctions.
Saved155-node native graph tests expand either eye to156 nodes. Applying this
expansion to the coupled graph passes native context-order modeling:76 publishers,
13 synchronizers,600/600 required publication dependencies,0 missing for either
eye. The previous coupled graph had75 publishers and591/591 dependencies.

BorrowedSceneSections owns stable C++56-byte headers and child arrays, not native
children. Prepare creates immutable topology addresses without a prospective
reader ID; Bind later joins the ACTUAL admitted extraReader without relocation.
Execution must still validate the live retirement state, original snapshot,
native code contract, mapping and terminal lifetime. Complete six-slot SceneDriver
vtable plus async range jobs and destruction paths were audited, including cold
chunks: non-destructor methods only read the header/borrowed handles, AA02E0 stores
raw parent pointer in a job, and A98398 releases context/payload only. C++ views
must survive all aggregate/compiler completion, not just callback return. They
must never enter native unique-owner vectors, native destructors, or the original
NativeDefinitionTree as fabricated owners. Quarantine must retain their storage.

The expansion/view helpers are currently standalone verified building blocks,
NOT yet wired into native graph publication. Next: use them under a default-off
one-eye execution mode, bind views to real extraReaders after final graph-ticket
capture, and add explicit source-to-execution mapping at declaration/verification.
Source owner mappings remain original-only; clone lifetime is a separate strong
C++ owner. Do not turn exactReferenceCompiled into an unconditional expanded-graph
admission. Preserve current dynamic declaration-time definition validation for
identity nodes, while the two views require their exact immutable source sections.
Telemetry/Python must distinguish original155 and execution156 node counts.
Existing late prepared-pair frontier requires one full source recording per eye;
it must NOT be weakened to accommodate split recordings. First prove all original
draws/resource declarations with native prefix/tail publication before early
snapshot-prelude/admission and suppression.

Local701 passes214 C++/GPU,932 Python and28 QuickBoot tests. Installed DLL
`122B5AEA9EC5C0306B625C18B535CC60D74FA481BA72B3F3DAE8D9A8340A993B`;
PDB `746CC7C305C474B4898215DC869988EE464F5785F5659E564E9A9B4071BB3A9C`.
Archive:build/single-pass-20260927/deploy-scene-sections-701.

### First Native One-Eye Split, Builds704-706

Default-off OwnedSceneSplitEnable=1 / --scene-split now expands native view1's
GBuffer_Discard into prefix109 and tail58, with immutable C++ borrowed SceneDriver
views. Original source mapping/owners remain untouched. The section snapshot is
recaptured against the exact COUPLED node, so its phase ranks match Plan.Source.
Prepare fixes clone addresses before graph compilation; Bind joins a real admitted
extraReader after the final graph ticket exists and before publication. A typed
Ledger::AdmitExpanded independently checks the expansion, bound views, owner
generation/protection receipt, compiler/context proof and consumer identity.
HasPendingExtraReader revalidates the exact active lease, not merely a reader count.

Declaration checks keep source and execution topologies distinct. Identity-node
definitions may refresh only when equal to current source bindings; selected root,
its other-eye alias and both clone prefixes remain frozen. Views and original
sections are checked again before execution. All native SceneDriver/async-job code
and vtable slots are fingerprinted before our hooks. Existing publication rollback,
aggregate/compiler completion and quarantine retain both clone headers/arrays.

Live705 passes22/22, including initial and repeat native execution. Actual graph
155 ->156 nodes,1956 ->1958 coupled edges; bound/code/declaration/execution checks
all1,600/600 context prerequisites,0 errors,0 suppressed draws. Initial scope137
view1/stage7/parent109 records52 draws on list2803791066064; scope139 stage9/parent58
records84 on list2803792206368. View0 parent26 still records51+83 draws on its one
list2803792237168. All three share queue2796089969248 and observed batch89 at
positions1/2/3. Repeat again has three different lists in1/2/3, same stage draw
counts, complete target records and parentStatus0. Objects join exact graphParents
scope/handle and the cloned prefix/tail object addresses, not guessed eye labels.

This is actual native recording/submission evidence, not just a topology plan.
The selected retained reference and both nonblank eye images also pass, but this
does NOT prove whole-frame10-plane pixel equivalence or a performance gain. Both
eyes still prepare/render natively. The unchanged native frame remains the only
fallback; no prepared-current/source-stream contract is relaxed for split lists.

Build704 DLL `19FAC839BA80DDD40F81FB652E98EC73E8B4C644F8FBBE967CE5D74F76577EB2`;
PDB `D38B04CF09FF008B30EC3DCF831A9A0197B67469CF661F4401BE00966E6FFECA`.
Archive:build/single-pass-20260927/deploy-scene-split-704. Local214 C++/GPU,
947 Python and28 QuickBoot tests pass. Live705 closes without crash/reporters and
restores preview settings. Runtime fault cases after Prepare/Bind before publication
still need explicit testing; the typed admission/cancel state fixtures cover the
helper contracts only.

Build706 adds independent existing SubmissionFrontier arming for scene-split,
latched with retained capture without enabling prepared-current/infix. Completion,
failure and Python cleanup disarm it. Python now joins three observed pass-target
lists to exact native range/slot/submit rows, genuine native reset token/generation,
residency set and returned same-consumer invocation. The two observers' batch
counters and ResetList sequence vs native generation are deliberately NOT equated.
705 offline validates observation-only publication; native lease proof awaits706.

Build706 DLL `B8EEA562EEBEC5BB6BFD76443D9FAA7745E279FD6AE3CC239CB328F0E837B015`;
PDB `7AC8663A25B12A0CBEA9A216173F9A83C0763DD065446E4685F384F8ECDD3B70`.
Archive:build/single-pass-20260927/deploy-scene-split-706. Local214 C++/GPU,
955 Python and28 QuickBoot tests pass. Launch707 ended before tests because
Windows rejected foreground activation. The unified launcher now retries only
ForegroundRejected within its existing loop for at most2 seconds, preserving
game+5s and Codex+1s after VERIFIED activation. Missing windows/permanent errors
remain fatal.36 QuickBoot CPU tests pass after this scoped fix.

Launch708 focused the game successfully but crashed before tests were armed:
ReportQueue/Cyberpunk2077-20261008-103505-21044-27228. Exact706 DLL/PDB analysis
finds EXE+19088D0 memset writing24B to readonlyEXE+2B3AA70. Native ushort-vector
growth575FC8 ->6DDD4C ->14B9D0 requests capacity4->6; old data{1,2,3,4} and allocator
trailer are valid. B8E718 ->B8E744 ->143CC4 returned the invalid allocation span.
The common allocator pointer is correct, but slab metadata needed to explain
the bad result is missing from the dump. No causal link to focus or scene-split
is established, and this cannot confidently be labeled random. Split/observer
exports are absent from captured dump pages; the launcher never reached test
arming. Reporter was closed and preview restored. Preserve
scene-split-crash-708.log and target-access-close-audit/crash708-* evidence.

709 reaches the world and executes the split without crash; the first report was
rejected by an incorrect Python join comparing graph callback workContext with
native bank context. These are different objects: NativeGraphParent reads work
context+18/+20/+38, NativeSubmissionFrontier resolves the registry bank and reads
native context+30list/+60state/+630residency. Corrected decoder keeps these types
separate and preserves exact native slot->submit context/list/generation joins.
It additionally checks tableIndex=producerSlot+1, native context ID1..128 and
token-eligible kind/state/submitted fields.956 Python tests pass, including a
regression where a work-context substituted into Submit is still rejected.
Original failed709 artifact remains unchanged. Offline corrected validation proves
range18/invocation19, slots242/243/244, native batch27 at positions1/2/3, genuine
native generations107452/107455/107456 and52/84/134 draws. Retest710 follows.

Live710 passes22/22 on the same706 DLL. Initial AND repeat verify genuine native
publication, complete declared/executed graph, all3 native reset tokens/residency
sets and preserved stage draw counts. Initial generations98882/98885/98886 and
repeat108072/108075/108076 map to prefix/tail/otherEye in batch positions1/2/3.
Each observer and split control reads back0 after capture. No crash/reporters,
preview restored and game closed. The708 startup allocator failure did not recur
in709 or710; its underlying cause remains unproved, not silently dismissed.
Current suites:214 C++/GPU,956 Python,36 QuickBoot. Normal711 and default-off
prepared-path regression follow before extending to both-eye split/early admission.

Normal711 passes9/9. Default-off712 initial/repeat prepared exports pass, but its
turned capture refuses the late boundary with2510 (BoundaryFailure::Replay): all
19 observed ranges had graphStatus1,0 qualified native tokens. SceneSplit startup
control was0. The subsequent assembly message obscured this earlier refusal;
no replacement/export was admitted, native draws remained, and no crash occurred.
The failure report now collects bounded seqlock-consistent owned stats/resources/
compiler/retirement/admission/schedule even when an earlier decoder throws.
It does not convert a refusal into success or weaken boundary conditions.
Full default-off retry713 passes25/25 on the SAME706 DLL, including turn/rejection;
the original712 evidence remains preserved. Exact reason the owned graph was not
active in712 is not recoverable from its earlier report. Both runs close, no
crash/reporters, preview restored. Do not claim that the refusal's cause was fixed.

### Both-Eye Expansion, Build714

BothEyesPlan composes existing exact expansions S0 -> peer S1 -> main S2, exposes
only the final execution graph, flattens original-to-final mappings, and contracts
both edge sets exactly back to S0. Four clones must be disjoint. Source capacity
is218 (final<=220). Both phase DAGs must contain actual dependency paths in order
Ppeer -> Tpeer -> Pmain -> Tmain, not merely coincidentally sorted ranks.
The prefixes have NOT been moved ahead of both tails yet.

Both section snapshots and typed admission use original coupled S0 nodes. The
intermediate S1 changes MAIN ranks/adjacency and is not substituted for its source
identity. Declaration validation refreshes only identity definitions through S1
and checks both existing plans; original selected/shared aliases and four clone
prefixes stay frozen. Both views may join one actual extraReader for a shared
native owner, without fabricating a duplicate owner binding or reference count.
The existing Consumer and aggregate/compiler/quarantine lifetime owns all headers.

Control2 / --scene-split-eyes both uses schema2: finalN+2/E+4 and a direct12Q
OwnedSceneSplitSections export (two6Q rows peer/main: view,prefix,tail,original,
prefixObject,tailObject). Reserved stats10..14 remain0. Schema1/control1 is
unchanged. The Python verifier reconstructs both expansions and requires four
exact native publications in positions1/2/3/4. Existing frontend controls and
prepared-current/source-stream incompatibility remain explicit.

Offline saved graph:155 ->157 nodes. Native publication-order model:77 publishers,
13 synchronizers,609/609 dependencies,0 missing. Source and declaration mutation,
overlap, shared-owner, capacity and lifetime helper fixtures pass. Local Python979
and QuickBoot36 pass; full C++/GPU and live verification follow.

Build714 passes215 C++/GPU,979 Python,36 QuickBoot tests. Installed DLL
`251A1FA202D5AC1945816AAB3E7599FD455DA391474659E5702AB8AC5042DB52`;
PDB `14B61171B002302BDD1CDD51D39164A4544F6E05F921DE7DE8FD9225C222D99F`.
Archive:build/single-pass-20260927/deploy-scene-both-714.

Live715 passes22/22 with BOTH mode enabled. Initial/repeat actual157-node graph
has1960 edges,609/609 publication prerequisites and0 suppression/errors. Native
queue order is prefixPeer109,tailPeer58,prefixMain26,tailMain83 at positions1..4.
Initial native generations100884/100888/100889/100892 and repeat110447/110450/
110451/110454 are distinct and have exact native context/token/residency joins.
Stage draw counts are52/84/51/83 in both captures; stage8 declarations remain even
though this scene emitted no stage8 draws. All controls restore0; game closes,
no crash/reporters, preview restored. This proves four real native recordings,
NOT a shared GPU draw path, shared CPU preparation, whole-frame pixel equivalence
or FPS gain. A same-launch head-turn capture and default-mode regressions follow.

The unified scene-split run now adds a third capture after a10-degree simulator
head turn, shares the reversible-pose/screenshot/HUD lifecycle with the prepared
sequence test, and restores the original pose in finally. It requires fresh
consumer/epoch, unchanged selected-eye mode and re-decodes actual native tokens;
draw counts are never hardcoded.991 Python tests pass after this launcher change.
Live716 passes23/23: initial, repeat and turned BOTH captures have genuine native
publication. Turned draw counts become48/60/48/60 at the same four section positions,
with generations119389/119392/119395/119397. All609 dependencies remain valid;
pose is restored, both images/HUD pass, controls clear and game closes with no
crash/reporter. This validates changed visibility, not a new rendering suppression.
Single-eye regression717 also passes23/23 with the same reversible turn; normal
diagnostics-off718 passes9/9. Both close cleanly without crash/reporters.

Pre-reorder audit: prefix child77D214 is not state-only. Its enabled path binds/
uploads an848-byte camera block through1E2C94 ->1F088C/1F4700 and records indexed
ExecuteIndirect via62331C ->623BB4 ->623C58 (20-byte arguments,maxCount1, command
signature registry+0x13BC678, current OM targets). Bind20C3AC has a flags&1 clear
branch (three RTVs and depth via771000/20DED8 and7710B0); the captured0x200010
profile does not take that branch. Draws, uploads and indirect reads still matter.
715 observed distinct per-eye OM resources: view1 colors614/620/625 depth609,
view0 colors212/285/286 depth283; stage7/9 share them within each eye. This proves
neither physical-heap nonaliasing nor absence of cross-eye shader reads. Existing
stage-scoped passTargets cannot attribute every setup/special-child side effect.
Do not reorder mainPrefix before peerTail based only on distinct resource IDs.
Additional77D214 path detail:62331C also uploads16-byte constants through the
special-manager handle+0x7B4 and448-byte item constants through renderer+0x60.
Its indirect argument buffer handle is special-manager+0x7A0 (read on this path).
1F088C may emit a real CopyBufferRegion for DEFAULT-backed buffers. The smallest
next capture keeps current native order and attributes accesses for ONLY Tpeer
and Pmain to child boundaries and exact native recording/submission identities.
It must include actual CBV/SRV/UAV/counter/IA/indirect ranges, clears/copies/CPU
uploads, physical heap/offset identities and transitions/aliases. Prove both
W(Pmain) intersect R/W(Tpeer) and W(Tpeer) intersect R(Pmain) are safe before swap.
NativeTargetAccess::Shader currently records a generic marker and OM writes, not
all shader reads/UAV operands; its existing journal alone cannot authorize this.

Final default-off regression719 passes25/25 on714, including initial/repeat/turn/
forced-rejection. Membership accepts167/167/120 in2 dispatches, rejects167 in the
fault case, all10 exported planes compare equal, and native output is preserved
on rejection. No native draws suppressed. No crash/reporters; preview restored,
game closed. All four persistent settings hashes equal the714 deployment manifest.
Current installed714 is verified for both-eye split23/23, one-eye split23/23,
normal9/9 and the existing prepared path25/25. Full single-pass/shared CPU prep,
native duplicate-draw removal and measured clean-FPS improvement remain unfinished.

### 2026-10-08: selected scene access capture720-723

Build720 adds an opt-in passive journal for Tpeer/Pmain, using the existing native
dispatcher and D3D hooks. No native commands are suppressed or reordered. Child
identities hold the exact consumer and frozen graph identity; command events join
the actual list/token/generation/context while recording. The journal retains
API-time COM resource ownership, creation/placement metadata, copy byte ranges,
barriers, actual output bindings and explicitly incomplete shader markers.
Descriptor-table metadata capture is implemented/tested but not yet integrated as
complete shader operand coverage. The pure access planner also remains non-executing.

Review fixes include capture-epoch isolation, stable parent identity per recording,
unknown initial OM state for partial recordings, and explicit failure on aborted
captures or missing/unclosed selected recordings. Finish is idempotent; clearing
the control cancels a pending observation at the next retained FrameBoundary.
Source target-access admission is unchanged. Its internal ClearView forwarding
does not duplicate events in the new journal. All controls default to zero.

DLL `DA9DACD4103038D85D0B2AD8625E29E2B882BA2095055781F1D21E3B2570DFA3`;
PDB `43544D1D4DFA91CB31BD34C66B8E7D64D8223648E4244F7CF45B3C2E404A0EB1`.
Archive:build/single-pass-20260927/deploy-scene-access-720.
C++/GPU219/219, Python1024/1024 and QuickBoot36/36 pass. Python reader/launcher tests cover bounded
seqlock publication, exact native publication joins, control cleanup, incomplete
failure evidence and registry consistency. No shader or reorder permission is inferred.

Run721 stopped during ordinary pair warmup due to a Python local import shadowing
the existing restore_controls function. The game did not crash; launcher closed it
and preserved all settings. The import now has a distinct alias, with regressions
covering main's symbol table and both cleanup signatures. Evidence remains in721.

Run722 passes26/26, including initial/repeat/turned scene access and both-eye image,
stable-HUD and control restoration checks. Observer epochs advance1/2/3; each has
exactly2 selected closed native recordings and5 attributed child invocations, with
zero overflow/failure. Captured event counts are19827/25252/22849 across the wider
retained recording window; cumulative shader/unattributed counters also include
discarded unselected recordings and must not be interpreted as selected-draw totals.
The unchanged four-list native order is Ppeer,Tpeer,Pmain,Tmain. All native draws
remain enabled. Run723 normal/default-off passes9/9. Games/reporters are closed.

Concrete722 dependency: Tpeer's native, unattributed tail event430 transitions
MAIN's1024-byte constant buffer0x15824092000 from state1 to COPY_DEST1024. Pmain
then writes[0,848) at event6 and transitions1024->1 at7. Its special child repeats
1->1024 at954, writes[0,848) at956, then1024->1 at957. Pmain's tail prepares the
same buffer1->1024 again at1799. These are list-local indices, not CPU-global event
sequence/GPU order. Turned capture has the same pattern at Tpeer310 and Pmain
6/7,882/884/885,1727. All these events are native/internal0.

The two eyes'848-byte copy destinations are physically distinct: peer buffer
0x13a10eeedc0 lies at heap0x160823335e0+46727168, main buffer0x15824092000 at
heap0x16071b45940+10092544. Selected journals use13 native resources, all with
creation/placement metadata; only these two constant buffers lack descriptor
cookies. No cross-resource placed-memory overlap was found in this capture.
An empty same-buffer read/write conflict list does NOT authorize moving Pmain
before Tpeer: the exported resource-state precondition already crosses that boundary.
IDA follow-up identifies native cross-list handoff:1FAFA4 builds contexts in supplied
slot order and traverses each queue array backwards, invoking B9C6D4(previous+0x538,
previous,next+0x538). It calls1F4C78 to reconcile the next context's first-use maps
into the previous context's pending barriers.1FE758 emits FinalFlushBarriers via
1F7164 before Close. Therefore the observed transition is not necessarily a fixed
scene dependency or a default-state reset. Any reordered execution must go through
this native reconciliation with the new actual context order; reusing closed lists
in a new order is invalid. The graph-slot ordering guarantee is still under review.

Selected native shader totals are177/177/149 graphics markers, with no compute:
Tpeer84/84/60, Pmain scene51/51/47, Pmain special indirect42 in all captures.
The42 indirect reads are20-byte arguments covering[0,840) of one15360-byte buffer;
no count-buffer read was observed. Three native848-byte copy pairs,7 transitions,
and8 unknown tag13 events occur in each capture. Shader reads remain incomplete.

Default-off prepared regression724 passes25/25 including initial/repeat/turn and
forced rejection, after normal723 and observation722. Game and reporters closed;
preview and controls restored. All four persistent settings hashes still match720.
Native redundant-draw removal/common CPU preparation remain pending. No FPS
improvement is claimed here.

Graph-order follow-up proves the native rank chain:1ECF2C copies phase1 rank to
work_context+0x38;1EC404 publishes the context to that renderer slot;58DC98 passes
ascending consumed slots to1FAFA4. Fresh compilation can therefore regenerate
handoffs for a new order. Exact722 barrier caller attribution has not been sampled;
the observed tail pattern is consistent with this verified mechanism.

The new pure ScenePrefixOrderPlan proposes, in each phase, removing58->26 and
adding109->26,26->58,58->83 while keeping109->58 and26->83. The saved722155-node
source builds the same157-node BOTH expansion, then validates the candidate. CPU
ranks become109=101,26=102,58=103,83=104; GPU84,85,86,87 respectively. Every other
rank and payload is unchanged. Transitive closure changes exactly one ordered
pair:58->26 becomes26->58. The candidate has no runtime wiring or execution
permission; synthetic cycle/rank/dependency tests and the saved source test pass.

Native shader snapshots now separately record creation-time stage completeness
and stream-output presence. The legacy VS/PS-only byte API remains Unknown; full
graphics descriptions and successfully parsed streams provide explicit metadata.
Real WARP GS/SO creation and real stream-parser matrix tests pass without GPU
commands. This does not make manifest resource-use lists exhaustive reflection.

Native state maps can reduce later mutable-resource inventory work but cannot
replace access coverage:1F51C4 skips buffer kinds1/2/5, and1F40DC bypasses texture
tracking for record+0x56 flag0x40 after registering allocation use. First/current
states also omit intermediate uses without journal transitions. Original maps
must be captured before stitching: B9C6FC clears successor first-use maps, and
1FE758 clears all four maps before D3D Close. Fixed assets and CPU writes retain
their independent ownership/history requirements.

Suppression review: the final native direct draw is forwarded in
src/Stereo/CommandListCensus.cpp; retained source observation occurs before that
forward. A future default-native CPU commit gate must cover the actual original
call while preserving observation and restoring predication. Do not merely reuse
retained mode2. The consumer must have independently valid original bindings and
per-eye VRS for fallback before committing any deferral. Query/UAV/stream-output
side effects require separate admission, not just matching G-buffer pixels.
Microsoft documents that queries are not predicated and SetPredication cannot be
recorded in a bundle:
https://learn.microsoft.com/en-us/windows/win32/direct3d12/predication
Full descriptor preservation cannot copy from a shader-visible heap on the CPU;
it needs original non-shader-visible descriptors or qualified native bindings:
https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html

The paired branch now consumes the left owned IA stream directly through
RecordSourceGeometry, with its original firstInstance. The converted family must
still certify instanceRebaseSafe; the GPU compares the corresponding right slice.
No new compact CPU instance upload is created by NativePreparedPair. If bytes
differ, two original-PSO source draws execute in that exact supersequence position.
Both branches are recorded, but only one executes. Unshared sources remain exact
single-eye draws. Native graphics/compute bindings, including an originally null
compute root, are restored afterwards. Original game draws remain untouched.

MeshResults exports the actual GPU predicates after the final diagnostic fence.
The report separately verifies planned pairs, recorded commands, executed branch
counts and preserved per-eye order. A structural single-pair mismatch may be
skipped during bounded enumeration only when fallback, permission and equal output
are proved; it is never counted as successful paired rendering. Empty, overlapping
or malformed predicates, output mismatches and missing permissions fail closed.

Standalone WARP and hardware tests create DEFAULT-buffer inputs and modify them
with GPU copies, then compare and execute both conditional branches in the SAME
submission. No CPU fence or readback intervenes between producer and consumer.
Coverage includes equal/unequal data, multiple ranges per pair, all byte alignments
and masked tails, a multi-group range, differing source offsets, late permission,
disabled fallback, invalid bounds and repeated-recording rejection. These tests
exercise actual predicated commands, not only inspection of shader output.

Live548 passes the full launch/source/sequence/head-turn scenario. Baseline:
243 original-source reference draws,100 planned/actually equal shared pairs and
43 singles,143 executed optimized draws.343 optimized commands are RECORDED because
200 fallback commands are also present but skipped. VS474390 becomes276100.
The GPU compares36,845,610 bytes per side across399 ranges/400 dispatches, reading
back only1600 predicate bytes after rendering rather than73,691,220 mesh bytes
before rendering. Both eyes' G-buffer/depth/stencil match bitwise.

At the10-degree head turn:193 references,92 equal shared pairs,9 singles and101
executed optimized draws;285 optimized commands recorded. VS349480 becomes176438.
375 ranges compare21,106,336 bytes per side;1472 result bytes, zero mesh readback
and zero intermediate CPU mesh-fence waits. Both eye images again match bitwise;
all source/pose/HUD/control checks complete and the game closes without a reporter.
No real native mismatches occurred in548; unequal-branch execution is verified by
the explicit standalone GPU fixture, not claimed from the live equal-only case.

This removes one actual CPU/GPU round trip from the prepared renderer, NOT all
upstream waits. Sources in548 are still completed captured-frame snapshots.
CaptureInstanceWriter/NativeInstanceObserver currently copy original slot7 into
READBACK, and PrepareInstanceOverlay subsequently uploads the matched CPU bytes in
a separate submission before BuildPrivateGeometry. That original-occurrence copy
needs a directly consumable DEFAULT destination preserving original instance
coordinates and pre-submit permission, instead of the readback/upload round trip.
Material/selector preparation and the coupled schedule also still need current-frame
integration. Full native suppression, shared CPU scene preparation and production
FPS improvement remain unimplemented; the additional comparison/fallback commands
must not be presented as reduced CPU recording cost or production single-pass.

Final local coverage:185 C++/GPU and732 Python tests. Installed547 DLL/PDB:
DLL `724719665DB947977172183CDC924887640BFC4201D3479DBB519D6B4CAA5978`;
PDB `C93FA73F29049C6218F8A0CB8BF68509FFE28BEC488885DE714DF1E5DFA3C142`.
Deployment archive:build/single-pass-20260927/deploy-gpu-mesh-547. Settings preserved.
Live548 completes24/24 checks; normal549 completes9/9 with diagnostics disabled.
Both launchers close the game, using their termination fallback after the normal
close timeout. Neither run reports a crash or reporter. Preview is restored and
all installed DLL/PDB, game/plugin UserSettings, vrport.ini and launcher hashes
match the547 manifest; calibration remains absent. No game/reporter/RenderDoc
process remains. No commit/push made; the full single-pass goal remains active.

## Original-Position GPU Instance Copies (2026-10-07)

Native instance capture now writes DEFAULT storage immediately before its original
draw. GpuCaptureArena preserves the entire original VB view's coordinates while
copying only the selected instance records; firstInstance, firstIndex and
baseVertex are unchanged. Each eye has its own recording-bound arena, finalized
to GENERIC_READ at the original pass boundary. Inputs cannot supply CPU payloads.
The owning views become available to a same-recording consumer after Finalize,
without a CPU fence or a new upload. Actual per-copy permission is still separate
and must be enabled by the original native pre-submit validator.

NativeInstanceObserver::RecordGpuCapture uses the existing original-read checks:
native ring/allocation pins, exact recording and parent, current bound slot7,
producer version, PSO and original draw operands. It additionally checks the view
metadata used by the private arena. The original diagnostic READBACK and private
DEFAULT copy share the same initially-zero predicate. Rejected submission cannot
authorize either copy. No source resource state transition or later original
resource read is introduced.

The retained path no longer creates an instance upload buffer, a second instance
copy command list/allocator, or a dedicated instance-copy queue submission/fence.
PrepareInstanceOverlay now only verifies the already-recorded native GPU copies
after the ORIGINAL source fence. Its diagnostic private readback is recorded at
the original pass boundary. BuildPrivateGeometry consumes the captured native
views, rather than reconstructed CPU-upload versions. Source-readback and final
image comparisons remain in the diagnostic path; this does not claim their
removal from the entire source-preparation pipeline.

The standalone WARP/hardware fixture captures two versions around an intervening
GPU mutation of the source and renders both private versions in that SAME command
list. Its shader explicitly consumes SV_InstanceID, so the comparison verifies
original instance coordinates as well as pixel color. Both48/64-byte layouts,
firstIndex/baseVertex, late permission, recording generation, duplicate occurrence,
altered view/mapping, incomplete fence and uncertified completion are checked.
There is no CPU payload readback between capture and those draws.

Live551 rejects the first attempt at pass sealing with reason1813, without a crash.
The new caller incorrectly enclosed ValidateNativeViewForRecording in probe::Scope;
shader_snapshot::Capture intentionally returns no metadata during internal-command
suppression. Build552 limits that scope to resource creation and copy commands,
keeping the metadata check outside it. No ownership/layout gate was weakened.

Live553 passes24/24 checks, including repeated original-schedule capture and head
turn. Initial capture:255 native copies/13,760 bytes,345/354 complete private
geometries. Repeat:255 copies/13,792 bytes,345/354 geometries. Turned:201 copies/
10,240 bytes,291/299 geometries. Every admitted private instance version matches
the original GPU bytes and reports the exact ORIGINAL list/generation, with zero
instance CPU-upload bytes or extra instance-copy submissions.

Per-eye arenas reserve32MiB each,64MiB total for a two-eye capture. Required view
extents are12,583,584 bytes initially,13,145,152 on repeat and12,582,912 after the
turn. These logical extents are NOT allocation savings. Mode2's historical raw
stats field3 describes the required extent; the updated Python report names it
viewExtentBytes and separately reports allocatedBytes. Legacy mode1 retains its
old reader for historical reports. Native mode independently reconstructs packing
from all recorded sources, including any later rejected sources, and verifies
separate per-eye ownership plus original command-list identity.

Prepared sequence553:242 references ->150 executed optimized draws,92 shared
pairs; VS473359 ->292630. Turned:188 ->99,89 shared pairs; VS332240 ->176611.
Both eyes' color/depth/stencil remain bitwise equal. These are diagnostic executed
draw counts with conditional alternatives still recorded; native suppression is0.
No total FPS or reduced native CPU preparation is claimed.

The next dependency is current-frame material binding. All354 ready source banks
in the initial capture include sparse tables, so there is no finite-table-only
subset to use as a substitute for solving this. Observed sparse parameters13/31/33
span31,476..32,764 descriptors and share source heap identity3 in both eyes. This
is evidence for investigating native in-frame heap/descriptor allocation and
ownership, NOT permission to reuse a heap after its original lifetime. Material
buffer/T5/selector readiness and actual coupled producer-consumer ordering remain
to be joined before current-frame production consumption.

One alternative examined in Microsoft's documentation is direct root CBV/untyped
buffer descriptors to avoid a separate heap. However, descriptor arrays cannot
be dynamically indexed as root descriptors, and root views do not provide buffer
bounds. The existing eye-indexed paired descriptors cannot simply be relabelled
as root descriptors. Any such route needs a different validated shader/layout
contract; otherwise use the native descriptor arena with its actual lifetime:
https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-descriptors-directly-in-the-root-signature
https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html

Build552 passes185 C++/GPU and734 Python tests. Normal554 passes9/9 with diagnostic
controls off.553 and554 close the game via the launcher's termination fallback
after its normal-close timeout; no crash/reporter, preview restored. Installed:
DLL `0DA6377BBF612F77405A16508A94EE282E6A3C1B609C79D6520126527C142596`;
PDB `32E4B00EEF3815DA4F81BF12C567B38FEDAECCE6C9488E45D0B2DE4E847A9619`.
Archive:build/single-pass-20260927/deploy-native-instances-552. No commit/push.
Full current-frame paired execution, common CPU preparation, native suppression
and clean-FPS verification remain required; the overall goal stays active.

## Native In-Frame Descriptor Tables (2026-10-07)

IDA exports descriptor-flush-555.c and descriptor-allocator-555.c confirm the
native resource-table allocator's current heap/base/capacity and cursor at
context.state+0x14. The ordinary materializer advances that cursor after copying
finite tables; heap rollover obtains another block. NativeDescriptorArena uses
only free space in the ALREADY allocated current block, with original function
fingerprints, native recording validation and a compare/exchange of the cursor.
It neither calls the allocator's fatal-on-exhaustion path nor rolls the cursor
back. The complete before/after state must differ only by that increment.

During retained reference draws, finite resource tables are copied from the
existing immutable CPU source-table receipts into the original shader-visible
heap. Sampler and large texture tables retain their actual native bindings.
Original root/heap/table state is restored before the original game draw. The
source recording owns this use; it is NOT a future-frame descriptor lease or
permission to borrow another eye's expired table. Native submission checks and
the retained color/depth/stencil comparison remain required. Normal rendering
with source-table diagnostics disabled does not invoke this route.

Build555/first live556 rendered the retained pass without a rendering error, but
the reader omitted the new exported symbol pattern. The harness rejected the
capture; no crash occurred. The symbol set was fixed and has a direct regression
test at resolve's call boundary. Live557 then completed the full24-check scenario:
201/354 source draws bound native copied tables on both initial and repeated
captures, with13,026/13,024 descriptors allocated.153 sources used their original
bindings because a complete new copy did not fit the remaining current range.
This result proves partial native descriptor consumption, not whole-pass coverage.

Build558 adds per-recording immutable-table reuse. Cache keys include the current
shader-visible heap and exact CPU heap/owner/count/identity/version. The cache is
discarded on any native token/list/generation/context/registry change. Each reused
entry retains its original range owner, while the CURRENT source gets its own
binding/recording validation. Only misses advance the current native cursor.
All-hit draws reserve zero new descriptors. Entries report first-source identity,
original parameter and actual GPU handle, and the reader verifies each reuse
against an earlier allocation in the same recording/heap, plus non-overlapping
new ranges and full immutable CPU-table identity.

Live559 passes24/24 checks. Initial and repeat captures both bind328/354 sources
through the native descriptor route:22,857 logical descriptors,2564 fresh slots
and20,293 reused.26 remaining sources retain the original bindings due to current
block capacity. After head turn,283/300 sources bind20,523 logical descriptors
with2519 fresh and18,004 reused;17 capacity deferrals remain. The retained native/
private G-buffer and depth/stencil comparisons are bitwise equal for both eyes.
The separately prepared paired sequence also passes:242 ->141 executed draws,
101 shared pairs; turned193 ->101,92 shared pairs. Native suppression remains0.

The new descriptor route is used during the ORIGINAL source occurrence, without
material-selector readback or a separate shader-visible heap for those reference
draws. It does not yet replace the completed-snapshot banks in NativePreparedPair,
and does not prove cross-eye table lifetime, full-source coverage or a FPS gain.

The remaining capacity limit is the current context's local block, not evidence
that the complete heap is full. Export descriptor-ring-559.c identifies native
sub_140774558: an atomic absolute-head ring reservation, modulo capacity, with
fence-tracked reclamation. Passing its fourth argument as null returns capacity
on exhaustion instead of entering its fence-wait path. The normal caller at
7744B0 searches descriptor heaps and can hit an assertion on exhaustion. These
are candidates for a nonblocking SAME-heap extra allocation, not an implemented
or authorized lifetime contract. Next work must inspect its fence publication/
retirement and heap-to-ring association before calling it; do not just increase
the current block's capacity or wrap its cursor.

Installed558:
DLL `E33173F1DCA2DDD5CD795E188CEC8C15E10A6168B24E9D9FA019888DC7AE38C6`;
PDB `AB0FF3EBE2EAFCF4D104B5245183F3D4421EBF8EFBE54F74390E6F95370270A5`.
Archive:build/single-pass-20260927/deploy-native-descriptors-558.
Normal560 passes9/9 with diagnostics off. Both559 and560 close the game via the
launcher's normal-close timeout fallback, report no crash/reporter and restore
preview. Final installed DLL/PDB and game/plugin settings hashes match558's
manifest; calibration remains absent. No game, reporter or RenderDoc process
remains. No commit or push made. Final Python regression remains740/740.

Local build558 coverage:186 C++/GPU and740 Python tests. The no-rollover allocation
policy remains explicit; reuse does not authorize changing heaps or reusing a
native range in another recording. Whole current-frame paired rendering and
native-work suppression remain unfinished.

## Nonblocking Native Descriptor Ring (2026-10-07)

Additional IDA evidence resolves the ring lifetime, not just its allocation:
1FB564 increments registry+13BC730 for native context kinds1/2/4 before acquisition;
the guarded1FAFA4 path also increments it for its native graphics context. The
actual bulk/single submission paths1F5E5C/91EED4 call91C59C after native execution.
Only the last outstanding context publishes the ring head through91C658/91C6B0,
paired with its native fence/value.91C7A8 advances the tail only after91C814 confirms
that fence is complete and not device-removed. These acquisition/submission paths
already belong to NativeCommandRecording's executable/lifecycle guards.

The774558 disassembly confirms its CAS uses the originally sampled head in R9;
Hex-Rays' apparent second independent head read is not the actual machine code.
Passing argument4=null skips the cold fence-wait/reclamation path and returns
capacity on exhaustion. It can retry a contended atomic reservation, but does not
wait for GPU work. NativeDescriptorArena now uses that original function only
when the current local block cannot satisfy a miss, and only for the one native
descriptor ring whose heap exactly matches the currently bound heap.

The fallback validates current native recording ownership, a positive outstanding
context count, ring capacity/occupancy, and before/after identity. It reconstructs
the unique absolute reservation from the returned physical slot and monotonic
head interval, rejecting ambiguous wrap, overflow or premature retirement. The
current context's heap/base/cursor/dirty/root-table state remains unchanged.
No allocator assertion path, fence wait, artificial capacity increase or rewind
is used. Each ring allocation and reused dependency is checked again before the
original native submission, including tail<=allocation begin and head>=end.

The full reservation/publication/retirement code is fingerprint-guarded, including
hot/cold fragments and fence-queue append/pop helpers. Source rows explicitly tag
ring allocations as kind2; missing ring proof cannot pass as an ordinary local
allocation. Ring16Q receipts expose source, registry, ring, heap, capacity, both
head/tail samples, physical/absolute range, both outstanding counts, no-wait flag
and unchanged-context result. Pure tests cover wrap, end-of-ring, exhausted
sentinel, ambiguous/out-of-range head, premature retirement and absent context.

Build561 passes186 C++/GPU and741 Python tests. Live562 passes24/24. Initial and
repeated captures bind354/354 sources, with zero deferrals.25,014 logical finite
descriptors use2881 fresh slots and22,133 reused slots. Ten nonblocking same-heap
ring reservations provide402 of the fresh slots; the rest use the current block.
After head turn,299/299 sources bind22,145 logical descriptors using2630 fresh and
19,515 reused; eight ring reservations provide356 fresh slots. Both eyes' native
and retained G-buffer/depth/stencil remain bitwise equal. All ring reservations
and reused ring dependencies pass original pre-submit lifetime validation.

The prepared sequence regression also passes on this DLL:244 ->149 executed
draws,95 shared pairs; turned192 ->102,90 shared pairs. It still uses completed
source snapshots rather than the new in-frame native descriptor bindings, and
native draw suppression remains0. These counts are not a production FPS result.

Installed561:
DLL `60168C420593E6E8993038950CDA2A401A77807F41F182A7315F2B2D2E6667C0`;
PDB `164359D0A70AA39B446E0416B8EAEA095BABB7E083F6CCC05A75E88F924CC582`.
Archive:build/single-pass-20260927/deploy-native-ring-561.
Normal563 passes9/9 with diagnostics disabled.562 and563 close the game via the
launcher's normal-close timeout fallback, without a crash or reporter; preview
is restored. Final installed DLL/PDB and all game/plugin settings hashes match
the561 manifest; calibration remains absent. No game/reporter/RenderDoc process
remains. No commit or push made; the overall single-pass goal remains active.
The same-heap native binding route still serves retained reference draws; it is
not yet a current-frame paired renderer or removal of the second native pass.

## Coupled Schedule With Full Source Inputs (2026-10-07)

The launcher/reader now allow owned-coupled together with the complete source
capture and prepared-sequence/head-turn regression. They still reject a second
legacy whole-gbuffer-plan/owned-sort scheduler and retirement mode. Source capture
in this combination requires stage7. The actual owned coupled graph is the only
scheduler; the old CPU schedule mode remains0 and its offline frontier decoder
is not used to validate a changed topology.

Live565 exposed a report assumption, not a renderer mismatch: the source stream
now starts with VRCAM, but the diagnostic instance payload buffer is assembled
MAIN first. Every one of264 recorded/submitted instance reads matched its CPU
writer bytes. The reader incorrectly required payloads in global source-row
order. It now follows explicit offsets and independently requires sorted payload
intervals to cover the buffer exactly, with no gaps or overlap. Reverse-order
payload and malformed-coverage tests protect this behavior. Saved565 GPU evidence
passes the corrected reader offline.

Live566 passes the combined scenario, including turned prepared rendering. New
CPU-boundary records show why waiting for CPU Seal alone is insufficient:
all354 source occurrences already exist when MAIN seals first, but VRCAM seals
later and neither list has yet been submitted. Both private CBV/IA/instance copy
arenas are finalized before their eventual original submission; no source payload
readback has been processed at these boundaries. Raw submit events show VRCAM at
index1 and MAIN at index2 in an8-list batch. Later recycled command-list addresses
also occur in the trace, so pointer-only matching is not a generation proof.

Build567 adds NativePairSubmission and an actual pre-dispatch check tied to both
validated native recording generations. It requires either a previously submitted
producer on the same queue or an earlier producer in a batch that the actual
DeviceHooks path sends as separate ExecuteCommandLists calls. The latter flag is
the REAL execute split decision, not preparation trace's unrelated queue-observer
mutex flag. Both original source certificates and finalized arenas are required;
dispatch is acknowledged only after the original execute calls return.

This distinction follows Microsoft's ExecuteCommandLists contract: separate calls
provide a completion dependency, whereas the driver may overlap lists passed in
one call. This is an existing diagnostic split, not a new CPU wait or a claimed
optimal production submission policy:
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists

Next integration point: prepare a speculative private consumer at MAIN's CPU Seal
using already-recorded source copies; validate pending peer finalization and the
actual ordered submission before enabling its initially-disabled GPU predicate.
Do not require the peer's fence to complete on CPU, and do not incorrectly insert
the consumer in the last CPU-sealed list(VRCAM), which executes BEFORE MAIN.
The current checks establish ordering/readiness only. They do not yet bind paired
tables, consume current-frame material/selector inputs or suppress native work.
Build567 local coverage:187 C++/GPU and748 Python tests. Live568 passes23/23,
including three coupled captures(initial/repeat/head turn), all source snapshots,
native descriptor binding, and prepared-sequence regression. In each capture,
the actual pre-dispatch certificate is eligible: VRCAM index1, MAIN index2 in
an8-list batch sent by separate original ExecuteCommandLists calls; both source
submissions are certified and their arenas finalized. The source data boundary
still precedes all CPU payload processing, and the dispatch acknowledgement is1.
No CPU fence wait was added.

Initial/repeat native-table coverage is354/354; turned300/300. Prepared sequence:
248 ->147 executed draws(101 shared), turned196 ->100(96 shared), both-eye
color/depth/stencil bitwise equal. Original-schedule regression569 passes23/23:
its source submission gate correctly reports ineligible because MAIN submits
before the VRCAM source recording exists. It does not infer readiness from a
later capture or a recycled command-list pointer.

Offline inspection of568's actual paired candidates also shows matching native
sparse-table GPU handles, heap identities and extents for all101 parameter31
pairs and the one parameter33 pair. This is useful input for borrowing the shared
native material table in a current-frame consumer, but is not a lifetime grant.
The existing prepared renderer still uses completed-input private banks.

Installed567:
DLL `6C6E25031A810C267DC9B9BE103E5CC7081535C1594754FE8FAC985AB2577E00`;
PDB `05DAFBE593D205E7E09594BABE6F1A71629FA297E341953B28BDB51DAC968C1F`.
Archive:build/single-pass-20260927/deploy-coupled-order-567. No commit/push.
Normal570 passes9/9.568/569/570 close the game with the launcher's normal-close
timeout fallback, no crash/reporter and preview restored. Final installed DLL/PDB
and all game/plugin settings hashes match567's manifest; calibration is absent.
No game/reporter/RenderDoc process remains. Final Python regression748/748.
The full single-pass goal remains active; native suppression/FPS gain are unproved.

## Original-Position T5 Capture (2026-10-07)

Build571 removes the second T5 GPU copy, its private command list, allocator,
fence and post-source submission. NativeStructuredBufferSnapshot now supports
OriginalIndices storage: every selected span goes directly from its original
read occurrence to a nonoverlapping private range whose SRV preserves all8192
original128-byte record indices. The arena finishes GENERIC_READ at native Seal.
RecordedView validates the genuine copy receipt and exposes this addressing
before CPU completion, but does not itself grant ordering or selector permission.

The retained completed-input renderer adopts those exact native copies instead
of constructing another overlay. Existing source-submission, completed-fence,
GPU B4-byte, bounded-index-path and material checks remain required for adoption.
No read outside the verified selected span is admitted. Legacy Overlay::Record
is retained for its independent fixture, not used by the retained native route.
T5 report mode1 explicitly requires no additional list/fence and joins each view
to its own source list/generation/resource/offset; it does not claim a second GPU
readback byte comparison. The exported payload is the original source readback.

New WARP/hardware fixtures mutate the original T5 buffer between two captures,
then verify both preserved high-index versions through an actual pixel shader.
They reject overlapping/foreign/incomplete adoption and validate unchanged
GetDimensions. Local suites pass189 C++/GPU and750 Python tests.
Native allocation reserves8MiB per eye; required logical view bytes are reported
separately. This is an input-path change, not a measured whole-game optimization.

This removes one prerequisite for same-frame consumption, not the entire CPU
readback dependency: general current-frame paired material/selector admission
and paired-root binding in MAIN's Seal are still not implemented. The completed
source renderer and source readbacks are still used for the current regression.

Live572/574 rejected adoption after all167 T5 copies matched their source data.
574's new error detail identified the raw SRV-structure comparison at source94.
Build575 compares all documented buffer SRV fields rather than padding/unused
union storage, including in ValidatePrivateView. The regression deliberately
gives equivalent descriptors different padding and separately rejects changes
to every meaningful field. Native ownership, indices and completion checks
remain unchanged. The field contract is documented by Microsoft:
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_shader_resource_view_desc
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_buffer_srv

Coupled live576 passes23/23. Initial/repeat captures adopt167/167 views and
342016 original GPU bytes with zero additional T5 GPU copies/lists/fences.
Required logical SRV extents total2435072 bytes across both native arenas;
turned capture adopts120/120 views,245760 bytes and2338816 logical view bytes.
Native descriptor binding remains354/354 initial/repeat. The prepared sequence
executes248 reference ->136 optimized draws(112 shared), and196 ->100 after
turning(96 shared), with nonempty bitwise-equal both-eye color/depth/stencil.
These are diagnostic executed GPU counts, not fewer CPU-recorded commands or
an FPS result. Native suppression remains0; completed inputs are still required.

Installed575 DLL `750A1F6A661DA10398BF090E65C642A15FF4A7948E9F51D470C856B7AD0500B9`;
PDB `3744CDCC22DB5000F292A5557548EFC3F5A5BCAB98C26D4A948D09BD30A54D90`.
Archive:build/single-pass-20260927/deploy-native-t5-575. Final local suites for
this build pass189 C++/GPU and750 Python tests.576 closes the game without a
crash/reporter and restores preview via the unified launcher's normal cleanup.

Original-schedule577 also passes23/23, including two167-view source captures and
bitwise-equal prepared-sequence output. Normal578 passes9/9 with diagnostics off.
Both close via the launcher's normal-close timeout fallback, without crashes or
reporters, with preview restored. Final DLL/PDB and all game/plugin settings
hashes match575's manifest; calibration remains absent. No game/reporter/RenderDoc
process remains. No commit/push. The overall single-pass goal is still active.

## GPU Shader-Input Admission (2026-10-07)

Build579 connects original selector predictions to the prepared native renderer:
every supplied CBV/T5 prediction is compared against its original-occurrence
GPU copy before material draws. The GPU-produced acceptance predicate gates the
geometry comparison, both reference eyes, paired draws and fallback draws.
Only expected comparison bytes are uploaded; they do not replace shader inputs.
The result is16 bytes, with no CPU readback between the checks and the draws.

GpuGeometryCompare results now include NON_PIXEL_SHADER_RESOURCE so predicates
can directly gate another comparison. WARP/hardware fixtures test an accepted,
rejected and initially-disabled GPU producer, including actual conditional
consumer execution. Private CBV arenas now finalize as GENERIC_READ, removing
their readback state roundtrip and permitting both shader and comparison reads.

The unified sequence test also flips one expected byte, not the source buffer,
then requires GPU rejection, all geometry predicates disabled, no VS/PS/sample
execution, zero color contribution and no native suppression. Fault control is
restored; this must pass before the normal launch checks may finish.

IMPORTANT: this is currently connected to the completed-source renderer. Its
initial admission still uses CPU-verified captures. Moving construction to the
current source frame remains unfinished; these checks must not be reported as
the removal of that earlier CPU dependency or as production FPS improvement.

Predication does not make invalid resource-state prefixes safe. Source copies
must genuinely finalize and have guaranteed GPU ordering before their consumer;
a late zero predicate cannot undo ResourceBarrier commands. The next current-frame
integration must preserve that requirement, including its failure paths. Microsoft:
https://learn.microsoft.com/en-us/windows/win32/direct3d12/predication
