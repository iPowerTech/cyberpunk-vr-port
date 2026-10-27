# VRCAM notification lifetime (PID44908)

Found while checking the latest report queue after the RAM/VRAM discussion:
`Cyberpunk2077-20260925-003652-44908-43132`. This is a CPU access violation,
distinct from the pre-menu GPU MMU faults documented separately.

Matching image/PDB:framegen-tpp-20260925/candidate-01,
SHA2567759840628a0f8211bb54488b2a2677f32c9c7a4cc6e71abc07c17961036eaac.
Dump fault:CyberpunkVR_Stereo+38C20,ReadPtrSafe; caller+5F7D1,
NotifyExternalVrcam. The stack continues through PublishExternalMain and
MainRead. The cached component's vtable value was noncanonical
C5E2D09DC591D001; the attempted read was that value+240. An atomic cached
component address was incorrectly treated as a lifetime guarantee.

Fix: MAIN publishes only its value snapshot and exact pose label. It no longer
dereferences or calls a cached VRCAM object. The native RttRefresh callback owns
the component passed to it; only there do we notify, using that exact argument
rather than loading a possibly replaced global again. This retains the user's
MAIN-to-VRCAM data handoff while restricting engine calls to their owner scope.

Release built. Needs installed-build validation of camera timing and framegen.
Evidence:build/framegen-tpp-20260925/dump-44908.txt.

## Timing check and final ownership design

PID39464 on owner-callback-only build696b8bb1 confirmed45real+45generated FPS,
matching depth/MV and ~479.3MB tracked FG resources. However, the per-native-frame
camera constants showed VRCAM always used the previous MAIN pose (e.g.frame13301:
MAIN6624 / VRCAM6623). Merely delaying notification until the next owned callback
is memory-safe but adds a frame of eye latency.

The engine component has an initialized native weak self-reference at+08
(live check: instance matched the component, strong31/weak8; enabled, owner non-null).
The new implementation copies that weak reference only inside an engine-owned
RTT callback. MAIN can then lock it into a temporary strong handle, verify the
current selection and enabled/attached state, and notify immediately while that
handle keeps the component alive. No handle is constructed from the cached raw
address. The persistent slot holds only a weak reference, not camera/GPU ownership.

Five standalone tests passed: freed target cannot be locked, object remains live
through a notification scope, replacement/clear, mismatched identity, and2000
concurrent release/lock races. SDK WeakHandle.Lock uses the same increment-if-not-
zero lifetime rule. Installed-build timing/reload validation follows.

Installed the weak-reference build on 2026-09-25 at 11:42 MSK.
SHA256: `46177304443531510421da4df5555f9f5a1f8e4cdcbc45a67c71fba7a8b6ec75`.
DLL/PDB checkpoint: `build/framegen-tpp-20260925/candidate-03-weak`.
Backup and deployment report: `build/roomscale-plugin-deploy-20260925-114254`.
Settings and calibration hashes are unchanged. The previous process was stopped;
the user launches the game manually for the live check.

## Weak-reference live checks (PID28636)

Nine HMD pose cases (yaw/pitch/roll, three translations, combined) and four
slow/fast yaw ramps with 2 mm positional jitter passed: 893 + 2,199 distinct
paired native frames had identical pose/origin IDs. Eye distance stayed within
63.873–64.129 mm; maximum forward-vector disagreement was 0.000013 degrees.
No missing final TPP identities, unavailable handoffs, expired/detached targets,
or framegen input skips were observed. Head pose and diagnostic gate restored.
Framegen remained active: the longer yaw series measured 43–44.5 real FPS,
42.5–44.5 generated FPS and 85.5–89 output FPS (1–4.5 repeated FPS).
No claim of a constant 90 unique FPS is made for this run.

The native weak count stayed at nine across 30 samples (one above the original
eight). Strong counts varied with engine tasks rather than growing per notify.
Unit tests establish that the temporary strong reference ends at call scope;
these live samples alone are not a long-duration leak proof.

The FPP transition correctly disabled external steering but exposed a separate
existing gap: the scene/editor component rewrite did not preserve VRCAM's exact
pose receipt. VRCAM camera constants stopped advancing and framegen waited for
matching inputs. TPP restored exact receipts and generation. The next build
extends the same completed-MAIN descriptor handoff to the already-enabled
scene/editor MAIN-following route (BdEditorAlign), preserving its FPP ownership
flag. The Basilisk steering reader still accepts only external/TPP receipts.
Device cameras and actual braindance playback retain their existing routes.

Scene handoff build installed at 11:55 MSK:
`189c4251d137de11c8461fb4f48ddf3f9ec92d36449ca58da90b601220c9f797`.
Checkpoint: `build/framegen-tpp-20260925/candidate-04-scene`.
Rollback/report: `build/roomscale-plugin-deploy-20260925-115553`.
Release build, ten camera tests and nineteen pose/ABI tests passed.
Settings and calibration unchanged; runtime validation follows.

## Scene handoff live check (PID36064)

The first post-load camera-manager read reported FPP, but the scene subsequently
selected TPPFar before the pose trial. That first trial is therefore recorded as
`tpp-poses-scene-36064.json`, not as FPP evidence. Its 904 paired native frames
matched, with 1,077 exact final-camera matches per eye and zero misses.

An explicit `vehicleRequestCameraPerspectiveEvent` to the player then selected
FPP; the camera manager confirmed FPP before and after the complete motion trial.
`fpp-inputs-explicit-scene-36064.json` shows advancing, equal per-eye native frame,
pose and origin IDs; both submitted depth/MV inputs are complete at 1485x1485.
`fpp-poses-explicit-scene-36064.json` covers the same nine HMD poses: 894 distinct
paired frames match, 1,061/1,062 exact final identities, zero misses, zero TPP
steering calls. Generation stays active throughout (43–45 real FPS, 43–44.5
generated FPS, 86.5–89 output FPS). There were no framegen input skips.

The old generic-serializer MAIN debug record is intentionally dormant in FPP.
Its raw "last_distance" of 6.42 m compares the previous TPP source to the current
FPP VRCAM and is not a stereo measurement. The paired render-camera records show
63.925–64.050 mm separation and at most 0.000015 degrees of orientation mismatch.
The probe now requires matching pose/origin IDs before comparing those debug
records. The native-frame ring remains the source of the stereo results above.
After the FPP trial, TPPFar was restored for the final yaw/jitter validation.

The final TPP yaw/jitter run checked another 2,184 paired native frames with no
pose/origin mismatches, final-identity misses, or unavailable handoffs. All
3,026 Basilisk steering calls applied the current external target. Across the
three final-build motion runs, 3,982 distinct native stereo frames were checked.
Generation remained active in every in-run report, with no skipped inputs.
The long TPP run ranged from 80.5 to 89.5 output FPS (median 87); occasional
repeated frames remained, so the result is not a locked 90 unique FPS.

The final stationary probe measured 44.5 real + 45 generated = 89.5 output FPS,
0.5 repeated FPS and 4.072 ms generation cost. Completed depth/MV inputs matched
both camera receipts (e.g. native frame 22888, pose 19858 in both eyes).
TPPFar and the neutral simulator pose were restored. Every probe restored the
diagnostic gate, which was zero outside the runs. No new crash reports appeared.
Runtime evidence and derived analyses are under
`build/framegen-tpp-20260925/*scene-36064*.json`.
