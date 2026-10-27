# CyberpunkVR QuickBoot

Development-only CET helper for unattended renderer tests. It waits for the first main menu in a
process, then invokes the game's Continue/last-checkpoint path once. Returning to the menu later is
left alone.

QuickBoot observes the actual main-menu items and activates the recognized Continue/QuickLoad
item through `HandleMenuItemActivate`, preserving the normal UI transition. It does not select
an item by screen position or translated label. A missing recognized item retains the older
last-checkpoint fallback; a throwing menu activation does not issue a second load request.
The chosen path is retained as `resumeMethod` in `quickboot_status.json`.
Controlled launches also wait for fresh native loading-layer observations in its idle state
before releasing the first Continue request. The run-token permission expires and does not
change game settings. Post-load Continue input allows 250 ms for focus processing, holds Space
for 250 ms and allows 250 ms after release before restoring Codex focus. Readiness is still
determined from world/menu/HUD evidence, never from sending the key or menu request.

Run `py -3.13 -B tools/quickboot_tests/launch_to_save.py` from the repository root for a
single-process launch to the latest save. The script installs this development-only CET helper,
clicks Start Game one second after the resolution launcher appears, and uses `-skipStartScreen`
to bypass the initial "Press to continue" screen. Five seconds after the actual game viewport
appears (after the launcher), it activates the game, then activates Codex one second later.
Both foreground changes are verified; a failed activation is not reported as success.
It reports readiness only after CET
confirms an unpaused player with a valid camera in the world, no loading screen, and a short
crash-free settling period, followed by native rendering confirmation: the menu is closed,
HUD sources keep advancing and the HUD is successfully submitted to OpenXR for at least
one second / 30 display frames. A cached HUD texture alone is not readiness. Crash and world
checks continue during this wait. Temporary diagnostic counters are restored before returning.
An absent, minimized or undersized game viewport resets the readiness wait instead of
failing on its first observation. The viewport is checked again throughout HUD sampling;
the overall deadline and native HUD requirements are unchanged.
If the game crashes, the script prints its PID-matched crash report.
It also watches for a new `CrashReporter.exe` process or the Cyberpunk 2077 error window and
closes that reporter after recording the failure.
After stopping an owned test process, the suite continues checking PID-matched
reports and new reporters for five seconds. A shutdown crash fails the overall
run even if every rendering check passed; late reporter windows are closed too.

For single-pass tests use `py -3.13 -B tools/single_pass_tests/run_launch_tests.py --out build/single-pass-20260930/<run-label>`.
This runs the complete bounded batch in one launch and closes the owned game process in `finally`,
including after a failed test. Rendering tests require the launcher's stable HUD evidence
before starting and additionally check fresh nonblack images for both eyes. The HUD requirement
is for the current HUD-panel-enabled test configuration, not a reason to alter a user's HUD
preferences. Native symbols require the matching development DLL/PDB. A timeout remains a failure.
The summary lists completed and remaining checks; inspect PID-matched crash reports before
attributing a failure to the renderer or calling it random. Never run capture replay with the game.
Paired preparation traces also own each native visible ID, vtable, gather method and
update method at `0x1E9120`, with matched entry/return scopes and per-eye multiplicities. These identify
duplicate mesh preparation candidates, not permission to reuse view-dependent outputs.
Use seven merged frames with `--paired-camera-turn` so a newly submitted simulator pose
can reach the native camera history inside the measured interval. Missing observed motion
fails the test even when all compared camera constants match.

`--schedule-benchmark` runs four 240-frame paired intervals with the CPU schedule-plan
cache off/on/on/off in one owned launch. It cannot be combined with the GPU binding or
depth experiments. Reports include actual plan reuse/rebuild counts and separate CPU
timings for graph capture, plan preparation and native dependency creation. They are not
whole-frame FPS measurements. The internal `PreparationScheduleCache` control defaults
to 1, is unused when scheduling is disabled, and is restored after each interval. The
cache retains at most four exact graph variants; changed graph fields still rebuild and
validate a plan. Original graph snapshots and current collector-alias checks remain active.

The geometry-origin helper supports mode 0 (legacy unwind), 1 (compare, default)
and 2 (scoped native input). Continuous depth runs require 128 successful native
comparisons with zero mismatches before selecting mode 2. Live flush/draw/packet
identity checks remain mandatory; a missing scope retains the legacy fallback.
Each run restores the previous mode after draining. `--origin-benchmark` adds a
legacy interval before the scoped interval and cancellation test, all within one
owned launch. Reports require actual scoped resolutions and no residual unwind
calls in the scoped interval. This reduces metadata overhead, not native draw
count or per-eye scene preparation.

The test batch temporarily sets only the simulator preview window to 1280x720
so both-eye screenshots remain inspectable. It restores the prior window settings
after closing the owned game and preserves unrelated intervening changes.
Eye render resolution, FOV, mirror rate and rendering options are not changed.
This launch suite is a correctness test, not a comparable whole-frame FPS benchmark.

`--family-manifest <verified families.json>` enables the development-only native stereo-family
registry in that child process. It creates candidate root signatures/PSOs and runs a bounded
binding census, but does not replace native draws. Normal launches explicitly remove this
environment override and verify that no family assets or extra binding observers are active.
With `--native-draws --native-constants`, the runner correlates unambiguous
geometry pairs from one pass before requesting a shared constant bank. It exports
an immutable timeline journal for offline replay; neither the bank nor the
geometry report enables native draw replacement.

Adding `--freeze-geometry` to that diagnostic batch records bounded GPU copies of
eligible IB/VB ranges immediately before their original native draws. It requires
`--native-constants`. Readback is accepted only after the source recording cannot
be replayed and its submission fence has completed. The selected pass must have
matching complete GPU geometry for both eyes and matching CPU/GPU instance bytes.
This is diagnostic acquisition, not a performance mode: it adds copies and
readbacks, has a 256 MiB allocation budget, and never removes native draws.

`--compare-depth` additionally renders the selected frozen geometry with the
original native PSOs and with the paired PSOs into private depth/stencil targets.
It requires `--freeze-geometry`. Both eyes must have nonempty output and identical
depth/stencil values; undefined padding bits are not compared. Completion is
polled through a GPU fence. No output is copied back into the game and no native
draw is skipped. The comparison is a correctness test, not an FPS benchmark.

`--preparation-trace` records two frames of CPU gather/filter/finalize output,
graph selection and packet consumption. It owns the copied lists and does not
skip work. Its measured intervals include observation overhead and are not a
benchmark. `--check-pair-admission` additionally verifies that a request without
both views' output bindings is rejected, followed by a fresh stable-HUD check.
It does not enable paired rendering. The earlier experimental `--pair-prepare`
alias now performs this same rejection test: simply adding MAIN to the RTT
request reached DrawComposition with a missing texture and is no longer executed.

`--main-root-pair` tests one paired selection under MAIN's own composition/output
root, with two independent native view collectors. `--merge-root` first verifies
that warmup and fresh HUD, then requests replacement of a standalone VRCAM root
by that paired MAIN root. `--merge-root-frames 3` tests three consecutive merged
frames and a normal tail frame. A new merge is armed only after the previous
native root call returned with its composition ownership preserved; this return
is not a GPU-completion fence. The test requires exactly two depth consumptions
per observed frame and VRCAM-tagged eye copies, with witnessed depth-to-copy list
submission order. List reset/replay, reversed order and unknown cross-queue or
intra-batch dependencies cannot pass. This is not a pixel-equivalence proof.
These options remain bounded development probes, not a permanent single-pass
or benchmark mode.

`--paired-interval` follows the warmup/burst with 1200 consecutive paired roots
without full packet tracing. It checks fresh HUD and takes two both-eye images
while the mode is still active, then requires matching root/depth/eye-copy
submission totals. The native interval is capped at 1200 frames and 45 seconds;
cancellation drains at a frame boundary. Copy-list reset before submission,
replay, changed views and lost composition ownership reject the test. All run
controls return to zero and the ordinary-render HUD is checked afterward.

`--paired-head-turn` adds a ten-degree simulator yaw and return within that same
interval. Both scene images must change and return toward their initial pixels.
The exact initial simulator pose is restored, without overwriting a concurrent
pose change. This checks image motion, not complete pixel or temporal equivalence.
Neither option installs a persistent user-facing paired-render setting.

`--eager-depth compare` tests whether VRCAM depth packets can be sorted before
MAIN's first depth consumption. It leaves resource-dependent native filtering
at the normal VRCAM stage and compares the resulting native sort byte-for-byte.
`--eager-depth replace` requires a successful comparison interval first and may
then copy a matching pre-sorted array through the native gather allocator. Changed
inputs fall back to native sorting. In run 122, all 1200 peer buckets were still
empty at the early point, so the comparison correctly failed and replacement was
not enabled. This route needs earlier packet production, not relaxed validation.
With the early CPU schedule below, runs 129/130 obtained 1200/1200 identical
native sort outputs per reference interval, followed by 1200/1200 late-sort
replacements. This prepares data earlier; it does not eliminate both eyes'
separate mesh materialization or demonstrate an FPS improvement.

Preparation traces also record culling request types, the defined work-context
execution bit and materialization task intervals. Worker-end events use owned
entry metadata because the native worker destroys its task before returning.

`--graph-plan --merge-root-frames 3` owns and exports the combined native graph
before declaration, without editing it. The graph files retain active sparse
handles, node definitions and both dependency/order classes.

`--schedule-preparation check --merge-root-frames 3` additionally validates an
early VRCAM CPU preparation plan and independent native collector/metadata ranges,
but keeps the original dependencies. `--schedule-preparation apply` applies that
plan only to the native CPU job-group builder. It leaves the cached graph and
command ordering untouched, preserves particle prerequisites and all barriers
around the remaining eye work, and checks actual materializer completion before
MAIN depth. Unknown graph shapes or dependencies reject the test.
With `--paired-head-turn`, a successful bounded application is followed by the
same change throughout the 1200-frame interval. Every frame must be accepted;
fallback is a test failure. `--eager-depth compare` can additionally test whether
the early packets now reproduce native sorting. These are correctness probes
with observation overhead, not an FPS benchmark or a permanent single-pass mode.

`--paired-cameras --merge-root-frames 3` compares an early VRCAM camera block
against the exact native depth-pass upload, including all 848 bytes and its own
history/jitter. It implies the early CPU schedule. The paired graph already owns
both current cameras; this path does not predict the next jitter sample.
`--paired-camera-turn --merge-root-frames 7` adds and restores a simulator yaw step,
requiring at least one captured frame with a changed native pose/history. Only
camera probes may extend to seven paired frames and one normal tail; ordinary
bursts retain their existing bounds. Run 136 passed all seven full-byte comparisons,
including four moving-history frames. Native draws are not replaced by this test.

`--early-depth-gpu N --family-manifest <families.json>` enables a bounded current-
frame GPU comparison (1-128 selected MAIN draws). It implies native binding
observation, the paired root and early CPU preparation. The common PSOs render
the selected eye layers during MAIN using the early camera bank and actual native
geometry bindings; later original-PSO draws supply the peer reference. Depth and
stencil must match exactly and every source instance's early mask must be verified. Ordinary native
draws remain intact. Run 141 passed eight draws; run 142 correctly rejected the
wider 128-draw test because 25 source instances lacked complete peer coverage.
This is not a native draw-replacement or FPS mode.
The comparator now handles regrouped instance batches only for shader families
with a parsed instance-ordinal-independent contract. Runs 144/145 improved the
128-draw coverage to 127 complete batches; the last instance uses different
native geometry/LOD in each eye and rejected that full test. This requires
two eye-masked entries in the early common plan, not a forced shared LOD.
The early membership path now reads both CPU lists before MAIN, with exact
per-instance geometry multiplicity and conservative handling of unread upload
ranges. Run 149 passed 128 source draws: 1122 both-eye instances and one MAIN-only
instance, with bit-identical depth/stencil. Late peer matching explicitly rejects
an unexpected match for a MAIN-only prediction. This remains a selected-MAIN
comparison: adding all VRCAM-only geometry to the early union and replacing native
draws are not implemented by that result.
Run 150 repeated the 128-draw test with 1135 common instances and one MAIN-only
instance; both layers again matched exactly. Run 151 passed the ordinary nine-
check launch with the early/family observers off. The MAIN-only GPU fixture also
checks an empty peer layer; the report allows that only with zero common instances.
The early pass can now append VRCAM-only instances to a geometry binding observed
during MAIN, using owned upload data and mask 1. Runs 152/154 verified 27/8 such
instances respectively against actual native references, in addition to the 128
selected MAIN batches. Known peer-only candidates and unrecorded candidates are
reported separately; unresolved input ranges remain unknown. This is still not
native draw replacement, complete-scene coverage or a performance measurement.

`--whole-depth-target` extends `--early-depth-gpu N` to the entire selected native
depth group, including unconverted draw fallbacks. It compares the paired array
against original shaders rendering into the game's actual 2D DSV, then verifies
an exact depth/stencil array-to-native round-trip. It still executes complete
native references and is not the optimized/no-reference mode. Run 160 passed all
225 MAIN / 226 VRCAM draws, both array layers and both native copy-back images.
Use `--reflection-dxc <absolute-path-to-dxcompiler.dll>` with the family package
when unconverted DXIL shaders require modern reflection. This path belongs only
to the child test process and is removed from ordinary launches. UAV bindings,
direct resource-heap indexing, unsupported pipeline state and untracked depth
writes reject the full-group test rather than silently omitting a draw.

`--optimized-depth` selects the bounded no-reference route (with the same
`--early-depth-gpu N`, family package and optional reflection compiler). MAIN
renders into the paired depth array; VRCAM conditionally imports its prepared
layer and executes only unmatched ranges. Late CPU validation and a GPU comparison
of both native initial depth/stencil layers must admit the result. Otherwise all
original VRCAM draws execute. `--reject-depth-commit` forces that fallback for
testing. CPU admission is frozen before submission and cannot change under GPU
execution. This mode rejects enabled mod framegen.
Run 163 admitted the result with zero reference draws: 451 source draw commands
became 325 executing commands in this selected depth group. This is GPU command
reduction in one bounded frame, not FPS gain, CPU preparation reuse or permanent
single-pass mode. The fallback path still records native commands on the CPU.

`--depth-benchmark 6` runs an exact whole-target reference check, one warmup pair,
then six alternating native/optimized GPU timing pairs in the same owned launch.
It enables the all-compatible-draw early test and needs the same family package and optional
reflection compiler. Every sample waits for a fresh stable HUD, validates the
paired CPU schedule, records raw GPU ticks with each submitting queue's frequency,
and restores its controls. The native timing mode leaves all game draws/targets
unchanged. The optimized range includes initialization, copies and the admission
gate, with zero image-reference draws. Reports retain both sample orders and
negative results; they do not infer FPS from draw counts or sum per-eye time into
a frame-critical-path claim. Temporary targets and heavyweight diagnostic
observers still make this a bounded-path benchmark, not steady-state single-pass.

`--observer-profile` adds an eight-second CPU observation profile after gameplay
and the family census, before draw captures. It requires native binding
observation (directly or through the early-depth options). CBV/descriptor-copy,
heap binding, upload lookup/unwind and family binding intervals are reported
separately. These are inclusive wall times across multiple threads and include
contention; do not sum them into CPU utilization, GPU time or frame time. The
control defaults to zero and is restored before the rest of the launch suite.
Early-depth reports also record descriptor failure/overflow state and live heap
capacity. A successful depth comparison cannot override a metadata-tracking
failure.

`--early-depth-interval 240` adds a continuous no-reference depth test after the
exact reference/schedule warmup, then exercises cancellation in the same owned
game. It tracks bindings and fresh native instance uploads without bulk draw,
descriptor or preparation snapshots. Up to three depth resource sets are pooled;
reuse requires both completed GPU fences, never a CPU fence wait. Each set owns
append-only CBV arenas for its paired roots, with no per-draw GPU heap/buffer
creation. The test verifies every GPU-completed frame, admission/fallback counts,
target/arena reuse, unchanged bulk-trace counts, active-mode stereo images and
fresh HUD before returning to normal rendering. The interval remains diagnostic,
bounded by the native preparation runner's 45-second deadline; it is not the
finished scene-wide single-pass mode or a claim of FPS improvement.

`--early-depth-gpu all` removes the old 128-source selection limit. Explicit
numeric requests remain available for isolated comparisons. The full selection
and continuous route have a 1024-source safety capacity; the all-draw verifier
rejects a run that reaches that capacity instead of reporting a truncated pass
as complete. The continuous warmup now compares all admitted sources, and its
per-root constant arenas retain separate immutable entries for the full cohort.
This does not admit unsupported shaders or unknown native instance data.

Preuploaded instance ranges now have a separate GPU-identity matching domain.
Those identities are not CPU copies of transforms. The optimized route snapshots
the complete used ranges during MAIN and again before VRCAM admission, then
compares them exactly on the GPU before the conditional depth import. Any byte
difference keeps the original VRCAM batches. Reports expose `gpuInstances`
(MAIN/peer identity draws, copied ranges, copied bytes) and continuous
`instanceProofs` (total ranges/bytes). Binary instance rows with domain 3/4 contain
MAIN/peer address identities; domains 0/1 remain actual CPU instance bytes and
domain 2 contains CPU-owned peer extras. Unmatched GPU-only peer ranges still
execute natively. The extra copies are currently diagnostic overhead, not a
performance claim.

MAIN now keeps the watched instance buffer in its original read state combined
with COPY_SOURCE while taking those snapshots. It restores the exact native
state before a native transition/alias barrier, at pass finish/abort, and before
closing the command list. `--instance-benchmark` runs a 240-frame per-draw-state
baseline followed by the combined-read interval and cancellation on the same
DLL/process. The experimental `EarlyDepthInstanceReadScope` switch defaults to
1 and is restored after each interval. GPU timing includes all depth copies and
admission work; it is not a whole-frame or FPS benchmark.

`--descriptor-benchmark` runs sparse/dense/dense/sparse metadata storage in four
240-frame verified intervals in one game. It profiles descriptor callbacks and
then runs the normal continuous/cancellation checks. Known heap slots now use
dense value-ID arrays; unknown addresses and heaps beyond the allocation budget
keep sparse storage. Representation changes preserve all descriptor values,
references, lifetime cookies and range versions. `DescriptorDenseSlots` defaults
to 1 only within the opt-in observer and is restored after each interval.

Continuous reports also include native MAIN depth-entry timestamps for each
completed paired root. `cadence` requires consecutive frame identities and
increasing timestamps. It measures the experimental frame cadence including
observer costs, not the native-only game's FPS or a sum of nested CPU timings.

`--native-camera` uses an explicitly packaged camera-only VS variant with the
original root signature and the game's descriptor heaps. The native uploader
allocates one 2048-byte camera buffer per MAIN depth group: MAIN remains the
unchanged prefix and the peer starts at byte 1024. The converted VS maps view 1
to MAIN and view 0 to the peer. Every selected draw must bind that exact CBV;
the complete bytes are checked twice before accepting its first use. The
original camera handle is restored at group exit, including failed probes.
Actual game replacement still uses the audited VS camera/material depth contract.

Build the required development package with `package_native_camera.py`.
`--camera-benchmark` adds a private-bank baseline before the native-camera
interval in one owned launch. Reports require one camera upload per completed
root, all selected draws on the native path, and zero private CBV arenas. This
is groundwork for preserving native texture tables in wider passes, not proof
that G-buffer or other texture-dependent passes can already be merged.

`package_native_camera.py --all-families` builds native-root VS/PS variants for
every compatible typed camera in the package, including PS-only camera use.
Its `nativeCamera` manifest entry records stage bits, MAIN-prefix layout and
validated binary hashes; legacy `nativeCameraVs` packages remain supported.
Other resource bindings are preserved. Camera-free stages only receive SM6.1
metadata. The VS/PS interface linker preserves clipping and matches generated
pixel inputs to the producer without changing non-interface instructions.
The live census verifies native-camera PSO/stage records against the manifest.
Creation is not permission to merge additional passes: non-camera resources,
per-eye depth, target arrays and native stage bindings still need validation.
The standalone textured/MRT GPU test covers both camera stages and includes a
MAIN-only pixel-camera negative control; it is not an in-game G-buffer test.

`--native-camera-stages 3` exercises scoped VS+PS binding in the existing depth
test. It verifies the native shadow layout, remembers each stage's original
handle, applies the MAIN-prefix buffer to both and checks restoration at group
exit. Later foreign bindings are preserved. Registered camera buffers use the
fresh matched native upload/bind handle and equal GPU address/size, not an
invalid CPU-map assumption; other handles require matching readable bytes.
Reports include requested/applied/restored/preserved masks and restore the
control to the default VS-only value 1. This tests binding/restoration only;
it does not enable merged textured or G-buffer game passes.

`--pass-targets` adds an all-geometry-pass target/submission census to an
`--early-depth-gpu` or `--early-depth-interval` launch. It records weak target
descriptions with resource-lifetime cookies and exact scope tags, without
retaining the native textures. The analyzer reports partial/unknown scopes,
list resets and submission relationships. It does not treat CPU recording order
or two lists in one ExecuteCommandLists call as proof of GPU completion.
The all-stage flag and queue-observation serialization are disabled again after
capture. Full evidence is in `preparation.json` and `preparation-pass-targets.bin`.
The observed G-buffer has both four-RT and three-RT subpasses; the standalone
`native_camera_retained_chain` test exercises depth -> four MRT -> three MRT on
the same two-layer arrays, including the R16G16_FLOAT fourth attachment.

`--metadata-benchmark` compares full/live/live/full descriptor metadata in
four 240-frame native-camera intervals within one launch, then checks the
unprofiled interval and cancellation. Live mode preserves current values and
copy ownership, but suspends capture-only range history and avoids interning
streaming CBVs. Full captures and pending submissions always retain version
tracking. `DescriptorLiveMetadata` defaults to 1 in the opt-in observer only;
the runner samples its actual active state and restores the control afterward.
This benchmark measures prototype overhead, not ordinary gameplay FPS.

`--upload-benchmark` alternates legacy/fast/fast/legacy upload lookup in four
240-frame native-camera intervals with live descriptor metadata held fixed.
The fast route uses one-use native API receipts and generation-checked weak
range hints only to avoid redundant resource queries/LRU lookups. Actual
constant reads still resolve the live owned registry entry and read fresh bytes.
The runner checks all six counters and their ABI, requires fast-path coverage,
rejects fast hits in the legacy baseline, and restores `ConstantUploadFastPath`
to its default 1. No ordinary launch installs these family-observation hooks.

`--depth-variants` adds a ten-degree simulator head turn, a whole-target exact
depth/stencil comparison in that pose, a comparison after returning, and a
forced-rejection no-reference run in the same owned launch. It checks movement
of both scene images, restores the original pose, and closes the game through
the normal crash-aware runner. It can be combined with `--early-depth-interval`.
The motion screenshots are smoke checks; exact equivalence comes from the
separate same-frame native depth/stencil references.

Continuous reports include inclusive CPU timings for membership preparation,
MAIN planning/emission and peer matching/emission. The last four stages overlap
the aggregate draw handler and must not be summed twice. Live membership reads
only geometry/instance input; its first frame cross-checks both eyes against the
full decoder. The report requires two successful projection checks and zero
mismatches. Shader, material and camera contracts are still checked at the actual
draws, and the double-read of geometry remains in place.

`--cpu-peer` prepares private early queries and visibility lists for both eyes, then compares
them with native results. It currently rejects changing scene inputs and does not replace
rendering. `--bounds-trace` additionally observes spatial-bound writes and particle-instance
preparation, using register-preserving native entry thunks. These extra hooks are not installed
in ordinary launches. The trace records entry and pre-publication state, not simulation-step
counts or completion fences. `--cpu-repeat N` repeats a successful CPU probe within the same
owned launch; a failed check still closes the game immediately.

`quickboot.json` controls `enabled` and the post-menu `delay` in seconds. Resume is armed only
after `SingleplayerMenuGameController` finishes menu initialization and receives its save list.
The CET hotkey
`CyberpunkVR QuickBoot: toggle` changes the enabled state for the next launch. Runtime state is
written to `quickboot_state.txt` and `quickboot_status.json` so a failed test can distinguish
"menu never appeared" from "Continue API changed."

The one-command script handles the normal `show_launcher=1` setting. For unattended runs without
it, set `show_launcher=0` in `bin/x64/vrport-launcher.ini`; the plugin then uses the saved
resolution, HMD type and runtime. Set the value back to `1` to show the resolution window again.
`launcher_delay_ms=1000` gives startup/resize messages one second to settle when the window is
hidden; accepted range is 0..10000 ms. QuickBoot waits 0.5 seconds after the first main menu
before requesting the save.

QuickBoot owns the first main-menu resume only. Launch with the game's
`-skipStartScreen` argument when automating startup; loading is confirmed from
the live player/menu state, not only the `resumed` status message. Close the
game after each completed test session to release CPU/GPU/VRAM resources.

This folder is part of CyberpunkVRPort and follows the repository license. It is deliberately
excluded from tester/release packages.
