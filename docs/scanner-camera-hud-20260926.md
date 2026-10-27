# CCTV and scanner in the texture HUD

The user requested the current "Data" panel and the surveillance camera HUD.
The final instruction includes the camera circle/frame and active scanner
reticle, all following the full HMD pose with a zero-degree cone. It supersedes
the earlier five-degree cone and request to leave the central circle native.

## Live identification

Read-only inspection in PID45148 identified these exact HUD entries:

- `scanner_details`, `scannerDetailsGameController`,
  `base/gameplay/gui/widgets/scanning/scandetails.inkwidget`. Its3840x2160 root
  contains the Data tab, device information and the other native detail tabs.
- `camera_hud`, `hudCameraController`,
  `base/gameplay/gui/widgets/camera_hud/cctv_hud.inkwidget`. The3840x3840 root
  contains pitch/yaw rulers, camera name, time, side indicators, `hairlines`
  and `tile_safe` (the central circle/frame).
- `scanner`, `gameuiScannerGameController`,
  `base/gameplay/gui/widgets/scanning/scanning.inkwidget`. When the user enabled
  scanning, its1920x1080 root contained `border/scanner_overlay`3840x2160 at
  (-960,-540), with circles, crosshair, progress and loading indicators.

All three were authored with `useSeparateWindow=false`. The native spawn hook
now converts only those exact entries using the existing separate-window path.
Their original controllers, input, visibility and animations remain in charge;
there are no duplicated controllers or reparented widgets.

## Rendering and bounds

Added the `Surveillance` texture/quad channel for all three entries. It uses
the same full-orientation head lock as the Basilisk HUD: yaw, pitch and roll
follow immediately, independent of the ordinary HUD cone or body-follow mode.
Capture, both-eye submission and resource shutdown include this channel.
The layout catalogue appends Scanner data, Surveillance camera HUD and Scanner
reticle/progress without changing existing indices or persisted keys.

Lua supplies full-root bounds for the Data and CCTV roots. The scanner capture
includes its overflowing child rectangle with a32px border; the native root
and source window keep their authored dimensions. An empty child during a
transition keeps the last valid crop; resolution changes recompute it from
fresh geometry instead of accumulating the previous bounds.

## Verification

All35 HUD tests pass. The new GPU/quad test checks full HMD orientation,
sub-cone micro-movements, distinct channel textures and both eyes while the
ordinary HUD is configured to follow the body with a90-degree cone.
The production Lua module passes a lexical fixture through CET, including
the negative scanner bounds, missing child, resize, CCTV/data dimensions and
the existing subtitle/phone/loot regressions. The fixture only uses local
mocks and does not modify the live game's widgets.

Artifacts are in `build/scanner-data-hud-20260925`. The game was kept open for
inspection as requested; the user subsequently authorized closing it and
deploying after the build finishes. Visual acceptance follows deployment.

## Deployment

Release build succeeded. After the user's instruction to close and deploy,
PID45148 was terminated and the DLL/PDB plus `modules/hud_panel.lua` installed.
Installed DLL SHA256:
`3f67f6cc65410fbfdc2b82ce28f80f0dcb39ec58a4c25014a2a0f52da2148835`.
Installed Lua SHA256:
`10b7133565b90c80a6c4e5dbce4a9ad5f8256399d6f22389e2100fe57ea04da3`.
Both match their source files. INI, UserSettings and calibration hashes are
unchanged. Backup/manifest:
`build/scanner-data-hud-20260925/deploy-20260926-001032/`.
The game was not relaunched automatically. The new HUD elements still require
visual acceptance after the next launch.

The user then launched PID20652. Live inspection reports20 captured HUD
elements and no HUD/Lua errors. Scanner and Data roots are `inkVirtualWindow`
children; scanner bounds are(-992,-572,3904,2224) with its1920x1080 root
unchanged, and Data bounds are(0,0,3840,2160). These controls were hidden in
that snapshot and CCTV was not yet spawned. Native capture setup is verified;
visual headset acceptance of the active camera/scanner remains with the user.

## Scanner centre correction

The user reported the active scanner off the gaze centre. Live layout showed
that its source root uses Centered anchoring with anchorPoint(0,0), putting
the root's top-left at(960,540) inside the1920x1080 window. The crop calculation
had only summed descendants' offsets, although `inkHudEntryInfo` crops window
coordinates. This shifted the captured circle by approximately that amount.

The scanner crop now also includes `window:GetChildPosition(root)` and unions
the root/overlay rectangles in that same coordinate space. The active crop
becomes(-32,-32,3904,2224). Native widget positions and dimensions are retained.
The Lua fixture covers the nonzero window origin and verifies the recorded
circle centre maps to within one pixel of(1280,1280), including resizing and
empty-child transitions.

Installed and hot-reloaded only `hud_panel.lua` in PID20652; the game stayed
running. The user confirmed that the scanner is now centred on the gaze.
Lua SHA256 `c5af6c0441cc7ab554b4e5273fb4fb260b74d033a21bdf05e5215959a56f0ec1`;
backup `build/scanner-data-hud-20260925/hud_panel-before-center-001729.lua`.
The installed source also applies this fix on subsequent launches.

## Data panel crop correction

In the same PID the user reported that Scanner details was invisible. The
controller and its `scanning` panel were visible and captured, but its source
root also had Centered anchoring with anchorPoint(0,0): root origin(1920,1080),
size3840x2160. The panel at local X2790 therefore started at window X4710,
entirely outside the previous(0,0,3840,2160) crop.

The details capture now starts at the measured window-relative root origin.
Live bounds are(1920,1080,3840,2160). The scanner circle retains its corrected
(-32,-32,3904,2224) crop and the CCTV root remains at(0,0,3840,3840).
The production-module fixture checks the captured panel's real4710..5510
window-space extent. It passed, then the Lua file was installed and
hot-reloaded without restarting the game. The user confirmed Data and the
object information appeared in the HUD.

Final Lua SHA256:
`6cb8cdd2c9196e8fcd1e22cb6d340e88e0c27f8f2f29a5619f9ec7013b9f9fc7`.
Backup: `build/scanner-data-hud-20260925/hud_panel-before-details-002410.lua`.
