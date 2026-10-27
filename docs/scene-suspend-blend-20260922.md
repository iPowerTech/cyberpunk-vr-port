# Scene suspend and MAIN pose ancestry

## Live measurement

PID27076, installed DLL SHA256
`548b0e656c4e5b1e832929219d22adb76a20a8d5d9952fcd3525895d5a73559f`.
Attached x64dbg through its local MCP server. The user authorized a five-second
window, opened through WolvenKit/CET Lua. A temporary VrcamSel update wrapper
requested pause at 5.007 s and restored the original function. Native movement
updates stopped at 5.002 s of the memory trace; the phase difference comes from
sampling. `IsGamePaused()` was verified true. `OnRequestPauseMenu` did not reopen
the menu, so the handler fallback paused gameplay and `OnOpenPauseMenu` restored
the visible menu afterwards. No further gameplay window was opened.

Evidence: `build/scene-menu-27076/{active-trace,active-summary,findings}.json`.
The sampler continued after gameplay paused: its nine-second menu-free interval
is NOT nine seconds of gameplay. Compare the roomscale publication sequence.

With MainIsRightEye=1, the first 5.02 seconds contain:

| Tier | Captures | MAIN missing ID | VRCAM missing ID |
|---|---:|---:|---:|
| 4 | 144 | 144 | 3 at menu exit |
| 3 | 197 | 0 | 0 |

Tier4 changed to Tier3 about 2.216 seconds after menu exit. The body follower
mirror remained enabled with -39.19849 degrees of realign throughout, although
the VRIK suspension policy blocked new body steps. This is a confirmed stale
state defect. HMD rotation only varied by 0.033 degrees during the window;
this trace cannot establish whether a visible head-turn glitch disappeared.

## Changes

- BodyYawFollower owns offset, cone catch-up and timestamps under one mutex.
  Camera, movement and animation consumers refresh actual VRIK eligibility;
  they no longer consume an unconditionally retained exported yaw value.
  Suspending VRIK releases the offset even when OnFootDeltaHead stops running.
  A normal pause menu preserves the on-foot offset. Roomscale keeps its
  consumed-distance ledger, preventing distance from being applied twice.
- CameraDirector has a multi-camera path at EXE+12752C which uses weighted
  positions and quaternion nlerp, bypassing SetupCopy. The existing pose
  ancestry hooks did not publish its output. A narrow scope observes the
  actual serialized entries immediately before their array is freed at
  EXE+127A58 ->127A98. Output labels are propagated only if every positive
  weight has the same HMD sample, origin and local head pose, source
  generations remain valid, and multi-input weights sum to one.
- Unknown or mixed samples remain unlabelled. No closest-quaternion guess,
  current-head substitution, scene pose write or render-time filter was added.
  Exported PoseBlendDebug and PoseMainReadMissing counters distinguish missing
  blend ancestry from a separate override camera on the next live run.

The missing blend transport is established from the native code. The old live
trace did not record the director's inputs, so it does not yet prove that this
path explains every Tier4 miss or the rare wrong-eye image. A new live scene
run is still required; this is not a claim that the headset symptom is fixed.

## Validation

42 roomscale/camera tests and 19 pose identity/ABI tests pass. New cases cover
the measured -39.2 degree suspend/resume state, common-sample blends with
numeric transform changes, different/unknown samples, input reuse and a
non-unit weight sum. Hook signatures and the finalizer call site match the
installed game EXE. Release build: `build/scene-suspend-blend-build.log`.

## Deployment

PID27076 was closed through x64dbg after verifying the current thread's owning
process and executable. DLL and matching PDB installed; game not restarted.
DLL SHA256: `c6dbe322fa546cc0a8dac8d253768269d1278d6b4703cddacf07308954354244`.
PDB SHA256: `7afcf72c7dbd3cccf622f2c08cf0e7ed0ba33ebfc252b3272b2f59bf50112bce`.
Ready archive: `build/scene-suspend-blend-ready-20260922-214551`.
Backup and manifest: `build/roomscale-plugin-deploy-20260922-214646`.
INI and VRIK calibration hashes are unchanged. No commit was requested.
