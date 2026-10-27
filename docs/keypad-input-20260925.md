# Numeric device cursor input

The Phantom Liberty phone uses `FactInvoker` / `TerminalInkGameControllerBase`
with a visible `KeypadDeviceController`, not the phone-contacts menu. Numeric
door panels use the same keypad controller. Identify it through the player's
`IsUIZoomDevice` / `UIZoomDeviceID`, the device's `cameraZoomActive`, interaction
permission and visible widget ancestry. Looking away must not change ownership.

## Native cursor path (Cyberpunk 2077 2.31)

Verified in the running game and the matching executable
`a7de82945c03e041fc7339fcf9066224d98db2f5d80fea50f7947bb350a60991`:

- `inkWorldLayer + 0x1A8`: world-widget hit tester.
- Tester `+0xA8`: selected DeviceZoom EntityID; `+0x40`: cached real hit.
- RVA `0x8BB5B0`: raw world hit query. A 56-byte result contains weak window and
  component handles, local picker coordinates, authored cursor coordinates and
  screen coordinates. The original constructs the result before the detour
  modifies it; SDK weak-handle assignments preserve reference counts.
- RVAs `0x2446050` (mesh) and `0x24468B8` (plane): local coordinates equal cursor
  coordinates multiplied by `window.renderTransform.scale`.
- RVA `0x8BBA94`: native widget picking, hover routing and cursor placement.
  This path, the normal X confirmation and the game's keypad action stay intact.

The scoped hook retains only weak handles to a previously hit keypad window and
component. Stick input moves continuously in panel pixels, including when the
head ray misses. Valid head hits still drive head-relative movement; reacquiring
the panel does not jump the held cursor. Full stick travel covers 75% of the
panel's shorter side per second, with the configured left-stick deadzone and a
proportional curve. The step is capped at 50 ms and position is clamped to the
panel, with no accumulated travel beyond an edge.

XInput consumes the left axes for this path so native screen-space acceleration
cannot move the cursor a second time. Other axes/buttons retain ordinary UI
bindings. The ownership heartbeat expires after 350 ms; the stick sample expires
after 100 ms. Menus, the VR overlay, hidden widgets, changed/closed devices and
dead weak handles release the cursor. Hook installation verifies the native
prologue and a mismatch leaves ordinary input in place. Diagnostics obey the
runtime debug gate; no polling log or GPU allocation is added.

## Validation

- Seven C++ test groups, including 30/45/60/90/120/144 FPS, looking away, return
  of head tracking, stick release, edge reversal, hitches, NaNs and reset.
- Lua scope fixture checks exact owner/controller, gaze independence, hidden
  ancestors, blocked interactions, menus, overlay, loading, shutdown and zero
  game delta. Executed against lexical mocks through CET.
- CET cold-start fixture passes twice with the production modules.
- Release DLL builds and is installed (SHA256
  `6d0140e61020a6a5b6601300c3905d20da2c5c8f13ec8ded555f509c6cbec41b`).
- After restarting, the user tested the phone in game and confirmed the fix
  works on 2026-09-25. Other devices sharing `KeypadDeviceController` are covered
  by the same scope; they were not individually tested in game.

Earlier experiments were restored: notification-layer `SetFocus` did not fix
world picking, and changing the authored projection-plane size did not affect
the cached native properties. Neither experiment is part of this implementation.
