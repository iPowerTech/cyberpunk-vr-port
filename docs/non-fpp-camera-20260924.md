# CameraDirector external views (2026-09-24)

The game can select a camera outside the player's FPP component, while the
named VRCAM render texture remains attached to the player. This produces two
different viewpoints in story/vehicle third person.

## Native evidence and reference review

In PID 26864, seated in `Vehicle.panam_panzer`, the game reported `TPPClose`,
vehicle state 1 and scene tier 2. CameraDirector selected one generic camera
with weight 1, serializer RVA `0x7FFBD0`, at approximately
`(3283.7615, -720.9890, 111.0108)`. MAIN's FPP component and VRCAM still used
serializer `0x127F58` and different positions. This is a real camera selection,
not something that should be inferred from distance to the player.

The 0.1.6 TE6 testing reference's `GenericNonFppCamera.cpp` and
`GenericVrcamHandoff.cpp` implement the right principle: compose HMD tracking
on the game-selected camera and derive the other eye from that camera.
However, they install a second CameraDirector hook, use older pose lookup,
and include a completed-MAIN distance/rotation heuristic. Copying those files
would conflict with the current pose ancestry hooks. Their temporary placed
component overwrite is also unnecessary when replacing the RTT descriptor
before its matrices are constructed.

## Implementation

This section describes candidate01. The subsequent user-requested MAIN snapshot
handoff supersedes the RTT camera-table selector below; see
`basilisk-20260924.md` and `startup-gpu-crash-20260924.md` for the current code.

- Extend the existing CameraPoseTransport director scope with the generic
  serializer. Compose into its temporary setup, leaving the native orbit source
  unchanged. Physical translation uses the source camera's yaw frame.
- Select VRCAM's base from the director's current weighted camera table, under
  its native reader lock. Use the common FPP pose only when FPP is an active blend
  input; otherwise acquire the current XR camera sample.
- Override only the selected VRCAM's native descriptor at the verified RTT call
  site. The original builder constructs matrices/culling from this pose. Add the
  opposite eye displacement once, and use the runtime vertical FOV for both eyes.
- Request a native RTT refresh at its refresh boundary for a detached camera,
  which can move independently of the player attachment.
- Keep exact pose identity ancestry through generic serialization, blending,
  descriptor construction and final render. Diagnostic snapshots are gated by
  `CyberpunkVR_RuntimeDiagnostics` and require no logging when disabled.
- Existing device-lens and valid BD playback paths retain ownership. An inactive
  XR session and a director with no selected generic camera use their existing
  paths. No TPP distance threshold or forced perspective event is used.
- The legacy ForceFPP folder becomes a camera state bridge. Remove its hold
  checkbox, restriction application and perspective events. Once per player
  attachment, remove the old saved VehicleFPP restriction. Do not repeatedly
  remove restrictions that the game adds later.

All native hook/lock/descriptor sites are byte checked against the installed
2.31 executable SHA256
`a7de82945c03e041fc7339fcf9066224d98db2f5d80fea50f7947bb350a60991`.

## Validation so far

- Release build succeeded.
- 8 camera math tests passed: neutral pose, head rotation/translation, eye swap
  and world scale, blend transitions, invalid inputs, fixed-point precision and
  overflow, 10,000 repeated compositions, unchanged native source.
- 19 pose identity/ABI tests passed, including all 20 native byte signatures,
  exact label ancestry, stale inputs, resets, blends and leaf register handling.
- Actual bridge source compiled and passed an isolated LuaJIT fixture through
  WolvenKit/CET: migration, no repeated restriction removal, player reattachment,
  retry after unavailable systems, and absence of the hold checkbox. Fixture
  uses lexical mocks and does not register real callbacks or change game state.

Candidate 01 installed after closing the game; settings/calibration hashes
unchanged. DLL SHA256:
`27152749f42ed63d07cb6394584cbc2adee68329017faef32881f4eb9cbe2078`.
Evidence and rollback files: `build/non-fpp-20260924/` and
`build/roomscale-plugin-deploy-20260924-224034/`.

## Live validation (PID 17828)

- CameraPoseTransport installed successfully. Game remained `TPPClose`, and the
  legacy VehicleFPP restriction was absent.
- Idle: 94 labelled final frames per eye, zero misses. Eye separation 63.998 mm;
  equal orientation and FOV 108.54 degrees.
- Nine simulator cases plus returns: yaw left/right, pitch down/up, roll, lean
  right/forward/up, combined translation/rotation. 1,121 MAIN / 1,122 VRCAM final
  frames, zero pose misses and zero rejected director blends. Original simulator
  pose and diagnostic gate restored. 551 sampled eye pairs had matching XR pose
  IDs; those are the proper comparison for eye geometry. Sequential reads of the
  native moving tank camera are not a same-frame submillimetre reference.
- Native FPP request accepted: external writes stopped; 136/137 final frames
  stayed labelled with zero misses. Restored the original TPPClose via the game's
  camera event: external writes resumed, 138/138 final frames with zero misses,
  separation 64.000 mm. No game movement input or focus changes.
- User confirmed visually that both eyes now show the same external viewpoint.

Surveillance, turret and braindance scenes have not been replayed in this change.
