# Sniper nest head following and HUD

Verified in the running q303 sniper sequence on 28 September 2026, PID 26464.
The controlled device was `SniperNest`, EntityID 14623291074227584262, with
`DeviceCameraControl6858` and `cameraComponent`. The attached weapon was
`Items.Sniper_Nest_Version_Tech_Sniper_Rifle`.

## Motor

The native controller uses the same input/update path as SurveillanceCamera,
but the port's explicit class check rejected SniperNest. Extend that check to
these two exact classes. Existing owner, lens, tracking, input-capture and
freshness guards remain. The original native controller still applies its
limits and animation; its automatic modifiers are filtered through the existing
same-update receipt, avoiding feedback from the VR view into the motor.

Sniper acquisition also aligns the physical barrel to the rendered base plus
HMD. Its original elevation and nonzero initial head yaw cannot remain as an
aim offset. Subsequent updates use the established head-delta model. This
initial alignment is sniper-specific; surveillance acquisition is unchanged.

In the old running build, the initial lens was pitched up about 73 degrees.
After enabling the controller and correcting the acquisition offset, measured
lens/view disagreement was 0.034 degrees. The user confirmed head following.
No shots were fired by the agent.

## HUD

The active `hudSniperNestController` is nested inside the existing
`briefing_sequence_player` entry, not a distinct sniper HUD entry. Its source
window and outer Root are 3840x2160 and already use the native separate-window
path. Keep that live controller and texture.

While the validated sniper motor publishes the current takeover identity,
route only `briefing_sequence_player` into the Surveillance HUD channel. This
channel follows the complete HMD pose immediately, with zero freelook cone,
independent of the normal HUD's follow setting. A changed/expired takeover
restores ordinary briefing behavior. Update existing slot masks' channel
atomically when the entry changes groups.

The Lua bounds bridge recognizes the nested sniper controller and uses its
existing outer briefing Root as the capture root. No widget is reparented or
duplicated, and the source layout retains its authored dimensions.

## Verification And Deployment

- 36 HUD tests passed, including a sniper-channel GPU test for both eyes,
  pitch/yaw/roll, sub-cone movement, independent textures and zero follow lag.
- Both surveillance tests passed, including native input ABI, frame rates,
  camera switching, tracking discontinuities, new sniper acquisition alignment
  and deferred alignment while a menu captures input.
- A lexical CET fixture passed for nested sniper bounds, resize, visibility and
  unrelated controllers. It did not access the live Game object.
- The session helper's filter ABI test passed before its two checked MinHook
  patches were installed. The original motor and HUD capture paths then ran for
  SniperNest; the user confirmed both changes worked visually.
- Release compilation succeeded. At the user's explicit request, the game was
  terminated and DLL, matching PDB and hud_panel.lua deployed. It was left closed.
  The temporary session helpers are not part of the installed mod.
- UserSettings.json and vrik_calibration.ini hashes remained unchanged.

Installed DLL SHA-256:
`BE8647B40314445A1572E20E3D6B388B758D5567F7EFCA973A3AD6AFBB201651`.
Installed Lua SHA-256:
`FEFE9C97E3843BB162FD6C2FC6F2FE4119D881087AB604C2E895EEB293EB7AE0`.
Backup and verified deployment receipt:
`build/sniper-head-hud-20260928/deploy-185012/manifest.json`.
