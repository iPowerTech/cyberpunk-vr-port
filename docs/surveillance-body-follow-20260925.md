# Controlled surveillance camera follows HMD rotation

The controlled camera's housing already turns with native mouse input. VR
previously rotated the rendered view without driving that housing. In the
observed scene the device is `SurveillanceCamera`, EntityID
`15679263223816289012`, using `surveillance_camera_1.ent`.

`TakeOverControlSystem:IsInputLockedFromQuest()` is true in this scene, but
mouse rotation still works. That script flag is not a camera rotation gate
and is deliberately not used by this implementation.

## Native integration

Cyberpunk 2.31's `DeviceCameraControlComponent` update at EXE RVA `2594B8C`
calls its six-argument input reader (`4C6730`) at `2594C32`. The reader fills
EulerAngles in degrees. This specific call is redirected through a checked
relay and an unwindable MASM thunk. The thunk forwards all six arguments,
calls the original reader, then adds HMD yaw/pitch deltas to its output. It
preserves the original volatile registers, XMM0-5 and flags after that call.
No shared input-reader entry is detoured.

The original update applies camera limits and its pole guard and submits the
animation once. The driver accepts only the active controlled
`SurveillanceCamera`, with the native controller's lens matching the selected
takeover lens. Turrets, vehicle cameras and unrelated devices retain their
existing paths. Native mouse input is retained; no OS input is generated.

Rendering uses a separate yaw base: the original lens yaw relative to its
owner, plus native manual yaw, transported by the owner's current yaw. The
HMD rotation is applied once to this base. The physical motor's HMD rotation
does not feed back into MAIN/VRCAM composition. Manual yaw accumulation stops
at the native yaw limit. Pitch remains on the existing level-horizon VR path.

New source/entity, tracking-origin change and expired controller state reseed
the model. Repeated pose samples add no motor movement. Menu/VR-overlay
capture and tracking gaps reseed the head reference without replaying missed
motion. Debug counters and the nine-float snapshot are behind the existing
runtime diagnostic gate; normal use adds no per-frame log.

## Validation

`tools/surveillance_tests` covers 30/45/60/90/144 FPS, four turn rates, pitch,
moving camera owners, native motor limits, simultaneous mouse input, repeated
samples, yaw wrap, pause/resume, recenter, camera switch, expiry/reacquisition
and invalid tracking. The ABI probe uses a callback that clobbers volatile
registers and verifies six-argument forwarding, live RDI component identity,
Euler-buffer edits and preservation of the original reader's outputs.

Both tests pass. Release compilation and diff whitespace checks pass.
The user confirmed the final motor and framegen fixes in the real headset;
the live investigation and acceptance results are recorded below.
Investigation and build artifacts are in `build/surveillance-body-20260925`.

## First headset run and takeover frame generation

The first DLL (`63c906bf2376...`) installed successfully. The user reported a
short rotation burst on entering the camera and no generated frames. In
PID24328 an eight-second capture recorded 482 motor callbacks and 482 HMD
delta injections. Framegen was enabled, but neither eye carried a final pose
identity: final misses were 482/481 and input-stage counters stopped before
the node-pose stage. The status was waiting for matching depth/MV; this was
not a menu gate or a disabled generator.

The takeover route writes the serialized MAIN setup in LocateCamera and
pushes VRCAM with `BdPushTransformOnce`. Neither write previously published
the address-based pose receipt used by native framegen. The component
serializer also overwrote any destination receipt with its original source
receipt, even when Locate had rewritten that destination.

The correction carries the exact shared composition's pose ID and HMD sample
into both writes, accepts it only for the selected lens's own serialization
and matching translation sample, and publishes VRCAM before its native
notification. Serialization preserves a newly published, still matching
destination receipt; the destination is invalidated before the call, so an
old occupant cannot satisfy this check. The regular component-copy path is
unchanged. Nineteen pose-ledger/ABI tests and seven camera-heading tests pass.

DLL `03018c4c9d00...` was deployed with settings/calibration hashes unchanged.
Live verification is pending. Rotation captures so far do not establish the
startup cause: initial native motor/HMD deltas were small, and the subsequent
render capture did not contain another takeover transition. A collector now
starts when the next game process loads the DLL and waits for an explicit
stop after the user's reproduction, rather than ending before the transition.

## Confirmed rotation burst and native modifier fix

The user paused during the burst in PID45360. The complete capture is
`fg-label-launch.json`. HMD yaw deltas stayed below one degree while the native
motor accelerated to 43.7 degrees per update and hit pitch -70. The independent
render yaw base remained -60 degrees; this was not accumulating HMD deltas.

Attached x64dbg and stopped at EXE RVA `2594E96`, immediately after the native
CameraSystem modifiers at virtual slots `2F0` and `2E8` (implementations
`6FC124` and `701C58`). At that exact stop, the input buffer at caller RSP+60
contained pitch 32.0903 and yaw 37.1494 degrees, while the alternative rotation
at RSP+40 was zero. These values were added after our input callback. The
breakpoint was removed, execution resumed, the temporary CET timer restored
the original pause, and the debugger detached.

A second verified local hook replays the original `movss xmm0,[rsp+44]` while
preserving native registers/flags. A thread-local receipt matches both the
component and the exact input-buffer address from the same native update.
Only the active HMD-controlled SurveillanceCamera restores its mouse+HMD
Euler input after those automatic modifiers. Their native calls still run;
handle cleanup and internal state remain intact. Subsequent native angle
limits and pole handling remain in force. Inactive, menu, unrelated component,
wrong-buffer and already-consumed receipts do not filter native rotation.
Both patch sites share a code page and are protected/restored together.

The regression uses the captured 32/37-degree contamination, and the MASM
probe checks the native stack offsets, instruction replay, output memory,
volatile GPRs, XMM0-5 and flags. Both surveillance tests pass after this fix.
The correction was subsequently verified after restart (results below).

Framegen is confirmed on the preceding DLL: the active surveillance run
published 291 final identities per eye and reached about 47 real + 41 generated
= 88 output FPS. It correctly paused generation when the user opened ESC.

The user subsequently confirmed that everything works with DLL
`71208d3a97811e1037d7c91512ad3d2fa2cf917cf37003725fcaf8a218926d6b`.
The 314-second capture (`auto-filter-launch.json`, PID45148) contains 8,447
filtered native updates, 8,223 final identities per eye and continued native
frame generation. The diagnostic gate was restored to zero after capture.
