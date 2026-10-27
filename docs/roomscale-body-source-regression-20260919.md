# IPD body jitter after the first centre correction

## Observed on the installed57a66866 build

User reported that the original body offset remained and that the body now
alternated left/right by IPD. Simulator Reset View was reported as apparently
working. The current process was **PID10132**, checked afresh before inspection.

Read-only observations:

- `BodyCentreMatch=17752`, `BodyCentreMiss=0`: exact centre lookup itself was
  not failing in this interval.
- `DebugVrikNativePairUsed=16426`.
- `DebugVrikLuaPairFallback=5015`.
- `DebugVrikNativePairPhaseMiss=3625`.
- Runtime reset bridge install and completed reset notifications were present
  in the log.

The source exposed the missed path: `VRIK_ComputeCamModel` falls back from the
native pair to the Lua pair. The earlier correction centred only the native
publication. Lua reads MAIN's `GetLocalToWorld`, passes its position through
`SetVRPlayerYaw`, and therefore still publishes an eye-bearing reference.
Switching sources changes the reference point even when both reads succeed.

This establishes an inconsistency and explains the new half-IPD source switch;
it does **not** certify that every original HMD-turn symptom has this one cause.

## Limited source correction prepared offline

1. `EyeCentreLedger::ReadWorld` resolves Lua float-coordinate snapshots against
   recorded eye/centre pairs in that same float representation. It also accepts
   an already-centred input without applying correction twice.
2. Conflicting float aliases are rejected rather than resolved by nearest
   distance. An unresolved Lua push cannot replace the last complete pair with
   a raw-eye position. The retained snapshot has a250ms lifetime bound.
3. Lua normalization happens **before** its existing pair filter, so an eye
   offset does not enter filter history and get subtracted using a later pose.
4. LocatedCameraFrame marks whether a centre is actually known. The native
   pair does not label an unknown position as a valid centred reference.
5. The enabled native-pair path does not fall through to unframed legacy scalar
   mirrors when no coherent pair is available.

No new Head/Hips/root transform writes were added. The existing native animation,
recoil, jump/sprint recovery and rotation paths were not replaced or disabled.
The review against the baseline found no assistant-added direct root/head
mutation to restore; the correction is limited to reference-data provenance.

## Validation and user-controlled trial

**Follow-up2026-09-20:** user reported the HMD-only offset unchanged on this
candidate. The [matched mouse/HMD comparison](roomscale-hmd-mouse-frame-20260920.md)
identified a different frame error in the model-space eye-bake. The test pass
below did not establish a fix for the visual complaint.

- New offline regression alternates native and Lua camera sources through a
  turn, covers missing publications, already-centred inputs and ambiguous
  float quantization. **13/13 tests passed**.
- Release build succeeded after adding the missing declaration include.
- No live code/variable patches, root experiments or simulated motions were
  used during this diagnosis.
- User explicitly approved replacing only the DLL and performing the next
  visual test manually.
- PID10132 closed. Only the plugin DLL replaced; INI unchanged.
- SHA-256: `7c4f953ce79f85c50a6a77bf86b44b30b6d35b428ab153e035e7fbeb919f0719`.
- Size: **2,480,640 bytes**.
- Backup/report: `build/roomscale-plugin-deploy-20260919-163057`.

**Pending manual check:** stationary HMD first, then slow left/right HMD turns
without locomotion. Inspect centre/fallback/rejection counters on the new run.
No live success claim is made for this candidate yet.
