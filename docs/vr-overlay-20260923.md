# VR settings panel

Current style follows the user's Cyberpunk pause-menu screenshots: dark navy/red
panels, red text/rules, yellow hover/selection, cyan pointer, angular corners.
Game actions are always visible on the left, without a GAME heading or its
vertical spacing; VR settings categories are uppercase on the right. All action labels are English
independently of the game's language. There is no Game tab. The earlier rounded/green demo reference was
superseded. The supported ImGui/D3D12 implementation remains, with Segoe UI for
Cyrillic; no reference demo binaries or image assets are imported.

The settings panel is a separate 1440x1080 XR quad visible to both eyes. It is not
drawn into the world images or framegen inputs. The desktop mirror is drawn after
`OpenXRManager::OnPresent` captures the clean game backbuffer. Mouse coordinates
use the same fitted rectangle. Once the VR ray reaches the panel, passive mouse
motion cannot take its cursor; explicit desktop clicks/wheel can operate the
mirror, and controller trigger/scroll/grip reclaim VR input.
The pinned DX12 backend ignores FramebufferScale, so the mirror transforms and
restores draw vertices/scissors explicitly. Three leased canvases and fenced
XR copies prevent reuse while either producer or consumer owns a texture. The XR
wait-timeout path retains the acquired image until it can be legally released.
Closing retires the canvases and XR swapchain once their GPU work completes.

Controls:

- Hold both physical stick clicks for one second to toggle. One hold toggles once.
  F10/Insert also requests the same panel; Escape and Resume close it.
- Opening does not pause simulation or enter an ESC menu scenario. Gameplay and
  stereo remain active without menu blur/bars. If opened from ESC, the bridge
  first closes that native presentation. Only selecting a game action such as
  Settings/Load enters the native menu and its normal pause.
- Aim with the selected hand; trigger selects, stick scrolls. A thin XR ray and
  panel reticle show the hit. Left, right and automatic hand modes are available.
- Grip while pointing at the panel starts a translation drag; hold grip and push
  the same stick up to move away, down to move closer. Distance is clamped.
- Default distance1.4m, minimum0.6m, maximum3m; default free-look cone60deg, with
  the existing >10deg/3-second rest catch-up. Tracking loss cancels a drag and
  releases pointer buttons. Origin reset reanchors the panel.
- The Overlay section edits distance bounds, size, text size, hand, free look,
  cone and depth speed. Changes and completed drags use normal INI persistence.

The CET module uses English action labels, retaining native item identity and
localized labels only for matching. On selection it opens the native pause scenario, matches the selected
action to the live item by event/action identity, then invokes
`PauseMenuGameController.OnMenuItemActivated` with that item's index/controller.
This preserves game save restrictions and confirmation flows. It never calls
PauseGame/UnpauseGame directly. Input returns to native menus after the
trigger/grip is released.
The observed current list is presented as CONTINUE, SAVE GAME, LOAD GAME,
SETTINGS, CREDITS, MAIN MENU and QUIT GAME.

Input capture suppresses merged pad input and shared gesture slots; the two-stick
chord does not emit deferred L3 sprint on release. A release latch prevents the
closing trigger from reaching gameplay. The UI thread handles all game menu
objects; the XR thread handles tracking and the render thread handles ImGui.

Validation includes interaction-policy tests (hold timing, ray geometry, drag,
distance bounds, follow, tracking loss, settings, click release and menu command
handoff), WARP rendering of the production shell and XR copy/lifetime checks with
the D3D12 debug layer. All11 overlay tests pass, including desktop fitting and
restoration of XR draw data;30 existing HUD tests also passed. Rendered layouts are in
`build/vr-overlay-tests/vr-{menu,settings,desktop}.png`. Lua bridge/cold-start fixtures run
through WolvenKit with lexical mocks, without modifying live game globals.

The native Settings action was also invoked in the current live game via
WolvenKit: it entered `SettingsMainGameController`, kept `IsGamePaused()==true`,
and returned to `PauseMenuGameController`. Computer-use was not used after the
user requested WolvenKit-only game interaction. Headset usability still requires
the newly installed build to be run with actual controllers.

Live PID23844: the replacement Lua bridge was tested via WolvenKit in the user's
active overlay session. With the overlay active, `IsGamePaused()==false`, only
the in-game/cursor controllers remain in the menu layer, and VRCAM selection is
still enabled. Closing and reopening both leave simulation unpaused. Native
overlay visibility continues through the existing bridge ABI.

Final deployment after removing GAME and its spacing: DLL SHA256
`0cbe3af4a4ef43f57f22a6eb0e552d7e3322d7fd8269858545805e83e437ef68`,
3,917,824 bytes. Plugin backup/report:
`build/roomscale-plugin-deploy-20260923-232024/deployment.json`; CET bridge backup:
`build/vr-overlay-deploy-20260923-232024/bridge-deployment.json`.
INI and calibration hashes were preserved. The game was closed to replace the
DLL, and is left for the user to start. The final desktop mirror/theme were
verified with the WARP renderer, not yet in the restarted game.
The final layout removal passed a fresh Release build and the GPU render test.

## Reopening after idle — September24

The user reported that holding both stick clicks closed the panel but did not
open it in gameplay. The Lua bridge deliberately only polls commands while idle,
leaving its last status timestamp unchanged. UpdateTracking incorrectly applied
the1.5-second active-panel heartbeat timeout to a newly issued opening request,
cancelling wanted before Lua could acknowledge it. PID10100 showed a healthy
idle Lua module with no error, an old bridge timestamp, and an already consumed
open request.

Heartbeat timeout now applies only while visible. A fresh opening retains the
existing3-second acknowledgement window. The integration regression reproduced
the cancellation before the fix, then passed after it: close, wait1.6seconds,
hold both sticks1second, receive acknowledgement, remain open through continued
hold, release and hold again to close. A separate test confirms an unresponsive
visible bridge still times out. All13 overlay tests and the Release build pass.
Installed DLL SHA256:
`3a9979a98807bcf9b6539d00a688ec77867c038a1f3e625cc9d8ffc4b7a81ab7`.
Deployment: `build/roomscale-plugin-deploy-20260924-001538/deployment.json`.
Only the DLL was replaced; settings/calibration were preserved. The game was
closed for replacement and is left for the user to launch.

## Stick scrolling backlog — September24

The controller produced interleaved MousePos/MouseWheel events at XR cadence.
ImGui1.90's input trickling deliberately separates wheel and pointer movement
across frames, so the queue grew faster than the rendered UI consumed it. This
delayed clicks and kept scrolling after the stick returned to neutral.

The pointer feeder now coalesces continuous motion, preserves trigger edges and
their positions, and applies the analog scroll delta directly to the current
frame's MouseWheel field. Event trickling remains enabled for short clicks.
Neutral/outside-panel input cancels scroll; a stalled frame's scroll history is
bounded. Grip movement and trigger dragging do not generate scroll, and pointer
release clears stale pending samples.

A regression using the actual pinned ImGui core reproduces the old queue growth
with90Hz input,30Hz rendering and hand jitter, then passes with the fix. It checks
bounded event queues, scroll speed, immediate stop, short click/release delivery,
stall clamping and an off-panel pointer.

The same iteration narrows game action buttons341->311px, reduces their height
70->60px and vertical gaps12->8px, narrows the left column365->335px and scrollbar
30->26px. CLOSE now uses a drawn cross icon instead of the literal bracketed X.
The production shell was rendered and visually checked after the adjustments.
All14 overlay tests pass, including actual hovered-pane displacement and no
movement after stick release. Release DLL SHA256
`baf5d62da7075d49cd827dc740a8e16683ff9cf1812fc10e9d910598e6fc4744`
was installed; report: `build/roomscale-plugin-deploy-20260924-002704/deployment.json`.
Settings/calibration unchanged. The game was closed for replacement; physical
controller validation of this build is still for the next user launch.

## VR cursor ownership — September24

Every WM_MOUSEMOVE previously refreshed a1.8-second desktop priority timer,
including repeated messages and coordinates outside the fitted panel. The XR
ray stayed valid while ImGui used the desktop position, so the cyan cursor could
freeze elsewhere or disappear. Mouse leave/non-client events could also feed
unmapped coordinates directly to ImGui.

The render thread now selects one pointer source from a mutex-protected mailbox.
After first VR pointing, passive mouse messages cannot steal ownership, even
during temporary tracking loss. An explicit desktop click/wheel can claim the
mirror; identical mouse positions do not renew the idle timer, a held desktop
drag retains ownership, and leaving the panel returns control to VR. Trigger,
stick scroll and grip reclaim VR input. Switching cancels any held button away
from widgets before applying the new source. Both desktop and controller short
click edges are preserved. Mouse leave/non-client messages use the mailbox,
and reopening clears the old ownership state.

The actual ImGui regression now checks180 frames of noisy desktop motion/leave
against a moving VR ray, loss/recovery, actual button activation for each source,
idle expiry, held-drag handoff without an accidental click, wheel/grip reclaim
and reopening. All14 overlay tests pass; the Release plugin builds successfully.
Headset validation of this change remains for the user's next launch.
Installed DLL SHA256:
`6e21504d6fcd1162348b1b7e3521c1d8500a055a5d66454b82783219ff9296fd`.
Deployment: `build/roomscale-plugin-deploy-20260924-004459/deployment.json`.
Only the DLL was replaced; settings/calibration were preserved. The game was
already closed and was not launched.
