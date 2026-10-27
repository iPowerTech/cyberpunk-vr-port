# Subtitles and interaction/dialog HUD panels

The user clarified that "input" means world interaction choices such as
"Стул / Сесть", not button reminders such as "Перемотать". Interaction choices
and dialogue replies belong to the same native `interactions_root` controller and
therefore share one independently configured XR panel.

Live read-only inspection through WolvenKit, PID26372 (game kept paused at ESC):

- `SubtitlesGameController`: direct child of HUDMiddleWidget, displayed "Садитесь.".
  Authored `inkHudEntryInfo` bounds1900x300, offset(-350,0); root1200x100.
- `gameuiInteractionsHubGameController`: direct HUDMiddleWidget child, root3840x2160,
  no authored bounds. Its `dialogWidgetGameController` child displayed "Стул".
- `gameuiInputHintManagerGameController`: existing separate window, displayed
  "Перемотать"; it remains in the ordinary HUD.

WolvenKit read `base/gameplay/gui/prototype_hud.inkhud` from
`basegame_1_engine.archive`. It confirms `subtitles` and `interactions_root` have
useSeparateWindow=false, while `input_hint` already has it enabled. The existing,
signature-verified entry-copy hook now enables separate windows for those two
direct roots before spawn, as it already does for phone/holocall roots. World
markers and crosshairs retain their existing projected path. The existing assembly
at `engine_re/dumps/HUD_entry_window_asm_20260923.md` verifies the separate-window
flag/handle and widget bounds path; no new engine detour address was introduced.

The producer and XR consumer have separate Main and Interaction channels, each
with its own source snapshot, texture leases, fenced canvas pool, serial, readiness
acknowledgement and yaw anchor. Native masking is applied per channel only after
that channel has a working XR replacement. Disabling/failing the interaction
consumer restores its native slot and does not hide the ordinary HUD.

Subtitles use the main HUD cone/body policy, depth and styling. Interaction/dialog
choices use a head cone of90 degrees by default and **never use the three-second
rest catch-up**. Only exceeding their cone starts smooth follow. The HUD tab has
an INTERACTIONS / DIALOGS section with enable, cone, distance and FOV. Keys:
`xr_interaction_panel`, `xr_interaction_follow_deg`, `xr_interaction_distance`,
`xr_interaction_fov`. The element editor also has Subtitles and
Dialogs / interactions for position, scale, opacity and visibility.

The Lua bounds updater prepares the converted fullscreen interaction root like
the existing fullscreen phone sources. It keeps authored subtitle crop dimensions
as a baseline but includes the actual mainPanel bounds (see the confirmed fix
below). It refreshes the source window; original game controllers continue to own text,
selection and visibility. Source-root opacity is not used as a mask.

Validation:32 HUD CTests passed, including channel routing, independent textures,
readiness isolation, interaction body-mode independence, remaining still for more
than three seconds at80deg with a90deg cone, then catching up beyond90deg. Lua
bounds and cold-start fixtures passed through WolvenKit with lexical mocks. Live
post-install rendering still needs verification after the game creates the new
separate windows on a fresh launch.

Installed first candidate DLL:
`19dcbaf0dd77b043e5356bacdaf20e0ddc1ace36a330e7586d4f6e0dcc612c8c`.
Reports: `build/roomscale-plugin-deploy-20260923-233743/deployment.json` and
`build/vr-overlay-deploy-20260923-233743/bridge-deployment.json`; settings/calibration
preserved. The user reported that subtitles and prompts were not visible.

Post-install PID27576: both controllers now have inkVirtualWindow parents; both
XR HUD layers were created. Read-only source inspection found valid native
textures/slots: subtitles1266x200, interactions2560x1440. Channel layout status
was available and the final native slots were masked. This is pipeline evidence,
not visual acceptance. At the inspected instant the subtitle root was hidden by
the game, UIInteractions had no dialogue hubs and InteractionChoiceHub.active
was false; recursive effective visibility found no text. An active interaction
is needed to distinguish a rendering failure from empty native content.

The user subsequently confirmed the chair interaction is visible. WolvenKit then
read "Стул" through the effectively visible native widget tree. Interaction
output is accepted; subtitle output remains unverified.

## Diagnostic crash — do not repeat the config query

PID27576 crashed at23:47:32 after the assistant queried
`UserSettings.new():GetVar('/subtitles','Cinematic')`. The REDengine report is
`Cyberpunk2077-20260923-234732-27576-14672`: assertion
`it != m_groups.End()`, message `Could not find group '' in in game config system`,
`inGameConfigRegistry.cpp(97)`, exception0x80000003 at EXE+0x7D9345. The stack scan
contains CET execution frames; this is not evidence of a HUD/D3D12 crash.
Archived matching DLL/PDB and report summary:
`build/hud-dialog-crash-27576/summary.txt`.

The old local script reference uses `/subtitles`; the installed user config has
`/accessibility/subtitles` and Cinematic=true. Read the on-disk config to discover
groups before any native lookup. An unknown group can assert in native code;
Lua pcall does not make that query safe. No such query is part of the shipped mod.
The CETBridge command queue was inspected after the crash and was empty.

A temporary injected subtitle test was not visually observed. Do not count it as
proof of broken capture: the test did not establish a visible source line. Its
callbacks existed only in that dead process, not in deployed files. Further
verification should use a normal active game subtitle. If temporary lines are
ever used again, remove the exact test CRUID with HideDialogLine, not
HideDialogLinesByData (the latter can remove other ownerless lines).

## Confirmed subtitle crop fix, September24

PID10068, ordinary active line "Может быть. Посмотрим.". Both the UIGameData
ShowDialogLine blackboard (Regular, CRUID25) and the visible widget tree confirmed
the real line. Native GetChildPosition/GetChildSize read mainPanel at(0,-255),
size1200x85, inside a1200x100 root. The authored capture rectangle was
offset(-350,0), size1900x300, so the entire visible subtitle lay above its top.
Changing the virtual window size did not address that negative local position;
the temporary resize probe was restored and is not in the fix.

The production updater now includes mainPanel's actual rectangle with32px
padding, retaining the authored horizontal coverage/minimum dimensions. This
case becomes offset(-350,-287), size1900x300. It preserves the native virtual
window layout, adapts to larger/multiline text, tolerates transient empty layout,
and retains original bounds through controller rescans to avoid accumulated
growth. After applying it live and leaving ESC, the user confirmed subtitles
appear in the world-space HUD. Native source/bridge errors were absent.

Saved module SHA256:
`b08298b78d9818690d5351a56d2513dac66703eb4395602dfd3103530b869ff9`.
Backup/report: `build/hud-subtitle-crop-20260924-000333/deployment.json`.
DLL remains19dcbaf0...; this fix required no restart or DLL replacement.
The updated bounds fixture passes negative-Y, larger-font/multiline, empty-layout
and rescan cases. Interactions were already visually confirmed by the user.

## September24: separate loot follow cone

After removal of the obsolete LootUi script, the user requested a10-degree
free-look cone for loot. Live inspection in PID18948 found the active loot plate
under `interactions_root/Root/topWidgets/looting` and its item description under
`Root/tooltipsWrapper/tooltipsContainer`. Both already render into the same
interaction texture, which previously always used the90-degree dialogue cone.

The UI-thread Lua updater now checks the actual loot widget and its two parents
for visibility/opacity. It passes that context to `VRHudPanelUpdate`, which carries
it through the immutable snapshot and captured frame. The XR consumer selects
`xr_loot_follow_deg` (default10) while loot is visible, and otherwise retains
`xr_interaction_follow_deg`. Opening/closing loot preserves the yaw anchor, so a
smaller cone starts a smooth catch-up rather than snapping the panel. Both keep
the existing policy without delayed catch-up at rest. Depth/FOV and the texture
remain shared; no third canvas or XR swapchain is allocated.

The HUD overlay exposes **Loot free-look cone** alongside the interaction cone.
The new value supports INI hot reload, UI changes and persistence. Missing or
out-of-range values fall back to10 degrees.

Live Codeware access must use separate `topWidgets` and `looting` lookups:
`GetWidgetByPathName` with a slash/dot path returned the root instead. Repeated
parent reads also produce distinct CET wrappers for the same native widget, so
Lua `==` is unsuitable for detecting this ancestor. Production detection checks
the two resolved names and each widget's visibility directly; the current loot
reported visible with that exact logic.

All33 HUD CTests passed, including a new capture-to-XR test for the10-degree
threshold, switching back to90, and smooth reopening. Both lexical Lua fixtures
passed inside CET, covering hidden/transparent parents, child replacement and
Codeware's root fallback without changing live game widgets. Evidence and build
logs are in `build/loot-cone-20260924/`. Release deployment is recorded in
`build/roomscale-plugin-deploy-20260924-164219/deployment.json`; the matching Lua
backup/report is `build/loot-cone-20260924/hud-deployment.json`. DLL SHA256 is
`6278fbf17df6f066e1e332235825adee4bbabebfe019445defb9c114d31c33aa`.
Settings/calibration were preserved. The user restarted into PID4636 and confirmed
the loot behavior is working.
