# WorldSpaceBlendCamera and scene pose mismatch

## Live evidence

PID19224, DLL `c6dbe322fa546cc0a8dac8d253768269d1278d6b4703cddacf07308954354244`.
Same prologue scene, normal game-menu pause. VRIK suspended at Tier4:
BodyYawFollow=0, realign=0, roomscale injected=0. The suspend change works.

The selected CameraDirector has two entries:

- Player `camera` (0x17695ee4c60), weight0.
- `WorldSpaceBlendCamera` (0x17696a79d70), weight1; owner is the same player.

The second name was resolved from CName c7a6f506e3048d15 through live Lua.
Its source pointer at component+1E0 is exactly the first component, saved in
`build/scene-menu-19224/table1-paused.bin`. This is pointer ancestry, not a
comparison of nearby transforms. There is no director override at this instant.

With x64dbg attached, Lua opened another authorized five-second window.
Automatic pause fired at5.002s; pause and visible menu were verified restored.
The trace includes325 captured frames:248 at Tier4,77 at Tier3. All248 Tier4
captures lack MAIN's image pose ID; VRCAM only lacks its first two menu-exit
labels. Tier3 restores both labels. The WorldSpaceBlendCamera output was
unlabelled before serialization: the outer CameraBlender cannot manufacture
an identity for it. This explains why the previous transport addition alone
did not restore MAIN in this scene.

## Verified native path

The component is gameWorldSpaceBlendCamera, size250h in the SDK, with an
entBaseCameraComponent base. Interface+40 points to EXE+E587D8. RCX is
component+120h, so its input camera at interface+C0 is component+1E0.

The native routine reads input+E0/F0, computes position and angular differences,
applies adaptive rates, and nlerps the previous/current quaternion twice. It
writes the filtered result to its own component+E0/F0, then calls vtable+240
at EXE+E58B5A to notify the transform change. This extra filter receives the
already composed HMD/eye pose of MAIN. VRCAM does not take this path.

This is a concrete asymmetric camera filter and source of pose divergence;
the old input receipt cannot truthfully label its filtered output as the
unmodified HMD sample. The trace's independently sampled camera blocks are
not used to claim an exact per-frame angular error.

## Change

Replace only this native notification call with a register-preserving thunk.
Immediately before the normal matrix/transform notification, copy the exact
completed MAIN transform into its WorldSpaceBlendCamera follower and transfer
that write's pose identity. This prevents a second camera lag from modifying
the VR view. MAIN's authored native camera motion is still in its source.
There is no new head locate, second HMD composition, extra IPD, guessed match,
or write to the VRCAM component.

The copy requires a current pose receipt from the exact g_camObjMain pointer,
view1, current tracking origin and the same current player owner on both
components. Menu, external-pose reset, device and braindance routes are excluded.
An unknown source stays native and unlabelled. FOV and the native filter's
private history remain untouched. The native notification and dirty-state call
run normally. The six-byte patch preserves the original return address.

Counters: WorldBlendPoseCalls/Applied/Missing/Changed, together with the existing
PoseMainReadMissing, PoseBlendDebug and per-eye capture/submit IDs, distinguish
successful copies from missing or concurrently changed sources on the next run.

## Verification

All19 pose identity/ABI tests pass, including the new notification thunk in the
register comparison with a callback that deliberately clobbers all volatile
GPRs, flags and XMM0..5. All three native read/store/call signatures match the
installed EXE. Release build succeeded in `build/world-space-blend-build-final.log`.
The initial build regenerated the source glob but MSBuild retained its old
project list; rerunning picked up WorldSpaceBlendCamera.cpp and linked correctly.

The first run above established the extra MAIN filter and the suspend behavior.
The subsequent run below verifies the corrected capture path. The user later
reported that the result appears correct and requested a commit.

## Deployment

PID19224 was closed through x64dbg after verifying the thread's owning process
and executable. Installed DLL SHA256:
`6d89f7cd84f32146b7db4297aa48e3a5fae2a302d0c0d1b73370e9d56f996498`.
Matching PDB: `cb13dfede308634f93ff0c3bf2214a93a6e50717e8da5901bebb68c41ffabde0`.
Archive: `build/world-space-blend-ready-20260922-220540`.
Backup/manifest: `build/roomscale-plugin-deploy-20260922-220700`.
Settings and calibration hashes are unchanged. Game not launched; no commit.

## Live verification: PID11884

The user relaunched the installed6d89f7cd DLL and paused at the same Tier4 scene.
Both camera hooks report successful installation. BodyYawFollow=0, realign=0,
and the roomscale diagnostic has zero injected movement.

Attached x64dbg and opened the same bounded Lua measurement window. Pause was
requested at5.014s; the timer restored the original Lua update function. Both
IsGamePaused and the visible PauseMenuGameController were verified afterwards.
Game remains running at its menu pause; DLL unchanged and no commit made.

Evidence: `build/scene-menu-11884/{active-trace,active-summary,result,findings}.json`.
288 captures were sampled. Two lack a pose label during menu exit, at0.0376s and
0.0486s. From0.1s onward:

| Tier | Captures | Missing MAIN ID | Missing VRCAM ID | Missing sampled submit ID |
|---|---:|---:|---:|---:|
| 4 | 198 | 0 | 0 | 0 |
| 3 | 85 | 0 | 0 | 0 |

WorldBlend counters during the window:202 calls,201 accepted copies,one missing
source at menu exit,zero concurrent-source changes. CameraDirector's blended
setup no longer adds missing IDs after startup. The override-setup counter is
global across other camera reads and still advances; it does not correspond to
lost labels on the captured images in this trace.

The two image IDs are equal in197/198 steady Tier4 captures and74/85 Tier3
captures. Do not describe this as one identical ID for every stereo pair:
separately rendered/cached images retain their own labels. The submitted-ID
snapshot is sampled independently of CapturedMonoFrame, not an atomic record
of the same submit transaction.

The HMD orientation was constant during this measurement, so the trace itself
does not verify the visual head-turn result. After receiving these results,
the user reported: "Отлично, вроде все ок" and requested a commit without a
Co-authored-by trailer. The observed MAIN identity loss is fixed in the measured
scene; the user has accepted the current build.
