# Story attention and cyberware input — 26 September 2026

Base: `0cc06d12`. Selective port from Ajson44 V39, without replacing our camera,
hand interaction, ForceFPP or vehicle-input implementations.

## Scope and provenance

- [Pacifica stage](https://github.com/Ajson44/cyberpunk-vr-port/commit/a7ddea59e7b0761436aa5aa0fd62329bf57de076):
  read `q110_02_camera_scan_start > 0` and `q110_02_scanning_done <= 0`.
  There are no quest-fact writes.
- Final V39 workspot rule (`28a73447`, after reverting the broad authored-scene
  override): locomotion 9 and tier >= 2 or actual workspot. Walking authored
  scenes keep native attention, including Judy's doorway. An active choice hub
  in vehicle state 3/4/7 keeps its native ray.
- [Manual Ofrenda clue](https://github.com/Ajson44/cyberpunk-vr-port/commit/886aa90b395b75741e66823e3f9c0e333461bf6c):
  exact `q110_haitian_book_a` component token, complete scan, available/enabled
  clue, not inspected, not auto-inspect. Revalidate before native
  `SetClueState(index, true, true, true, false)`.

Our controls use an explicit **X interaction press** for the clue, rather than
adding the fork's grip-to-interact system. The physical press is captured before
scanner remaps and swallowed until release: revealing Take cannot also take the
book with the same press. A held button on startup/resume, changed target,
expired request, overlay or menu cannot inspect it. Eligibility is checked every
50 ms and the native input lease lasts 250 ms. No cached game object is kept in
the Lua module.

## Native gaze boundary

Only the nine bytes after the source transform call at EXE RVA `3F90F7` are
patched; all 23 surrounding signature bytes must match. RBX is the targeting
user and `[RBP-50]` is its Vector4 + Quaternion. The MASM callback preserves
volatile registers, XMM0–5 and flags, then replays both displaced instructions.

Native `D1F2E0` locks the weak owner at user +10/+18 before queueing its event;
user +8 is the EntityID. All three values must match the freshly published
player, as must the current roomscale player identity. A nearby NPC does not
qualify merely by distance. Context expires after 750 ms without publication.

The pose comes from `ReadMainAimPose`, centred between the eyes, with its existing
100 ms freshness, owner, origin/reset and VR-session guards. This does not use
VRIK's possibly suspended view globals. Takeover and BD playback retain their
existing camera ownership; this port does not broaden the aim-reader eligibility
or import the fork's general scanner/hand-targeting path.

## Cyberware: L3 + left grip for 0.5 seconds

The recognizer consumes **raw** XR L3, before its existing D-pad/deferred-sprint
translation. It claims the grip and D-pad output for the chord and suppresses
the deferred sprint click. After 500 ms it emits one request; both inputs must
be released before another use. Grip thresholds are .7 press/.4 release.
Tracking gaps, menus and an overlapping R3 cancel the hold. L3+R3 overlay keeps
priority and continues to see raw buttons.

Input Loader appends private key F18 to the existing `IconicCyberware_Button`
mapping. No shoulder combo is synthesized: those shoulders can individually
trigger quick melee/grenades under different controller schemes. Native game
contexts, restrictions and cooldowns remain in charge. The key is dispatched
from the game input poll only while Cyberpunk owns foreground focus; a stale
request is discarded after 200 ms. It does not depend on the user's E binding.
The combination is listed in CONTROLS > CURRENT BINDINGS.

## Verification

- Release DLL build passed.
- The new REDscript file compiled against a **copy** of this game's base cache,
  with output written under `build/story-attention-20260926/cache`.
- `tools/story_attention_tests`: 57 attention/owner/clue-input assertions;
  native thunk ABI test (including stack alignment and shadow space);
  898 cyberware timing, polling-rate, hysteresis and cancellation assertions.
- Production CET module ran under LuaJIT 2.1 with 16 context/interaction checks,
  including zero-valued quest facts, no implicit inspection, menu, overlay,
  missing player, errors and same-EntityID reload.
- Input XML parsed successfully; installed Input Loader documentation confirms
  that `append="true"` preserves existing mapping children.

These are code/ABI/bridge tests, not a claim that the three quests were completed
in-game on this build. The matching quest scenes and actual cyberware activation
still need gameplay verification after launch. Runtime counters are debug-gated.

Installed six files at 11:03 Moscow time while the game was closed. Backups and
verified hashes: `build/story-attention-20260926/deploy-110347/manifest.json`.
Installed DLL SHA-256:
`C5E480143A3007340A79EBE6C5C739C80E277510B96E36725D5E1DF500ABE7E9`.

Build/test commands:

```powershell
cmake --build build --config Release --parallel 8
cmake -S tools/story_attention_tests -B build/story-attention-tests -A x64
cmake --build build/story-attention-tests --config Release
ctest --test-dir build/story-attention-tests -C Release --output-on-failure
python -B tools/story_attention_tests/run_bridge.py # needs lupa.luajit21
```
