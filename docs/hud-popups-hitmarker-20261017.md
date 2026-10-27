# Popups double the picture; hit/kill markers on their own head-locked window (2026-10-17)

## 1. Phone, radio, vehicle list: doubled picture under the asymmetric projection

Symptom: with the vehicle list (or the phone, the radio) open, the far scene doubles.

Measured on the QuickBoot save, player on a bike, no teleport
(`build/monowire-housing-20261016/probe_*.py`, eye correlation on the bottom 10 % of the
simulator capture):

| state | L~R | left eye vs base left |
|---|---|---|
| base | -0.21 | 1.0 |
| vehicle list open (before) | 1.0 | -0.22 (both eyes = MAIN) |
| list open, `CyberpunkVR_CompLendSet=0` | 1.0 | -0.22 |
| list open, `UI_System.CircularBlurEnabled=false` | 1.0 | - |
| list open, `RestorePreviousVisualState(inkModalPopupState)` | -0.22 | 0.96 |
| no popup, `RequestNewVisualState(inkModalPopupState)` | black frame in both eyes | |
| list open, fix | -0.22 | 0.97-0.99 |

Cause: the visual state `inkModalPopupState`, requested by the phone, the dialer, the vehicle list,
the vehicle/pocket radio, message popups, shards and the car colour pick
(`UISystem.RequestNewVisualState`, a scripted method that queues `VisualStateChangeEvent`). It lays
MAIN's picture over the frame; the second view draws MAIN's composition (lend), so both eyes show
MAIN and the two different eye frusta double it. It is also what raised the engine's menu flag for
these popups on 2.31 (vehicle list: 1 before, 0 with the fix).

Fix: in VR the state is not entered.
- `mods/redscript/CyberpunkVRPort_PopupVisualState/vrport_popup_visual_state.reds` wraps
  `UISystem.RequestNewVisualState` / `RestorePreviousVisualState`.
- `VRPopupVisualState(state, request)` (`src/Natives/UiPopup.cpp`) holds back `inkModalPopupState`
  while the OpenXR session runs and swallows only as many matching restores as it held back, so the
  engine's state stack stays balanced when VR starts or stops with a popup up.
- Knob `CyberpunkVR_PopupVisualStateFilter` (1), counters `CyberpunkVR_PopupVisualStateStats`
  [held, restores swallowed, passed]. Verified: list open (1,0,0), closed (1,1,0).
- The popups keep their widgets, game context and time dilation; only the full-screen state picture
  is gone.

The earlier attempt (a presentation-only menu flag that ignored the flag while `g_uiPopupOpen`) was
reverted: it did not stop the doubling, and `g_uiPopupOpen` is raised on a bike by the radio title, so
a real pause there would have been shown as gameplay.

Engine notes: native script function table `qword_1433A6E50`, class functions at
`regIndex(+0xB8) + 0x8000` (ctor `sub_1403CDF14`, dispatch `sub_14014619C`); a 9-entry list of
in-game visual states (`inkGameState` ... `inkModalPopupState`) is built in `sub_140CF5520`.

## 2. Hit / kill markers follow the head (free-look cone 0)

The markers are the HUD entry `hit marker` (`target_hit_indicator.inkwidget`,
`TargetHitIndicatorGameController`, children Damage / Defeat / Kill / Bonus, anims
`anim_*_marker`). The entry now gets a separate window before spawn and its own channel
`Channel::HitMarker` (`include/Runtimes/HudChannels.hpp`), submitted as a head-locked quad like the
Basilisk and surveillance channels: complete HMD pose, no cone, no catch-up. Distance and FOV are the
main HUD's, so the marker sits where the main HUD's centre would be, straight ahead. The native slot
is masked only once the channel's consumer is ready, as for every channel. The crosshair stays on its
projected path.

HUD unit tests: 36/36 (`build/hud-tests`, new routing checks; the test target needed a stub for
`cvr::optics::probe::Capture`).

In the game the markers then vanished everywhere, in the eye and on the quad: the channel had no slot
(slots 0, sprites 0). The entry's root (`inkCanvasWidget3`, not `Root`) is a 1x1 canvas, Centered with
anchorPoint (0,0); the markers are spawned around it (200x200 holders, kill animation scale 1.5, bonus 2)
and `UpdateWidgetPosition` moves it by its margin with weapon sway. Without an `inkHudEntryInfo` crop the
separate window captured that 1x1 root. Fix (`mods/cet/CyberpunkVRPort_Stereo/modules/hud_panel.lua`):
`TargetHitIndicatorGameController` is handed to the window upkeep like the scanner details, with the
window-origin flag and padding 160, i.e. a 321x321 crop around the root's place in its window, following
the sway. Probe (`probe_hitmarker4.py`, kill marker looped through `anim_kill_marker`): slots 1 sprites 1,
quad submitted; the marker sits at the same place in both eyes with the head level and pitched down 30 deg
(left eye 537/535 px, right 327/320 px of 820), while the subtitles and the rest of the main HUD move.
With no marker playing the window is empty and skipped (`CyberpunkVR_HudSkipEmpty`), so it costs no draw.

## 3. Quest marker over an NPC sat off its chevron

Symptom: the quest icon over the save's NPC sat right of and low against the chevron under it.

The port shrinks world mappin roots to 0.7 (`MarkerScale`) with the engine's `SetScale`, which
scales about the root's render pivot. The widgets (`probe_mappintree.py`, CET dump of the HUD layer):

| root | size | anchor | authored pivot | content |
|---|---|---|---|---|
| `QuestMappinController` / `GameplayMappinController` own root | 75x75 | 0,0 | 0.5,0.5 | hangs from the origin (zero-size canvas at 0,0) |
| presentation widget (controller +0x170), the chevron image | 256x256 | 0,0 | 0.5,0.5 | fills the box; the game centres it on its target |

Scaled about the centre, the icon moved (1-0.7)*37.5 units right and down while the chevron stayed
on its centre. Fix (`src/Hooks/WorldMarkers.cpp`, `Remember(..., pinToAnchor)`): a controller's own
root gets its pivot set to its anchor point, presentation widgets keep theirs. Setting the pivot on
the presentation widget too moved the chevron up-left by 0.3*128 units, so that is excluded.

Verified on the QuickBoot save: chevron x = icon x in both eyes (523.0/523.1, 286.7/286.8 in the
1920x946 capture), the icon's horizontal eye offset equals the NPC's (-236 px).

## 4. The same marker sat lower in the second view: the engine's HUD bend

After section 3 the icon was still about 2 capture px lower in the second view than in MAIN, while
the scene around it had no vertical offset. Ruled out, each measured:

- the flat ink camera: all 16 terms identical in both views apart from the x shift and parallax;
- the HUD raster: viewport and target 2648x2864 in both views;
- time and depth: the same offset with time frozen and with the marker parallax off.

With the head pitched so the marker crossed the screen, the offset grew with the distance from the
centre and changed sign (MAIN minus second view: -6 px at height 75, -2 at 293, +2 at 585). That is
the engine's HUD composite (both eyes use it; `RecordHudComposite` is not in that path): it bends the
HUD about each image's own centre, y by 2*cx^2*cy*k.y with k = 0.084242 (b6 r3.y). The eye windows
are mirror images, so the marker sits at cx -0.21 in MAIN and +0.05 in the second view and only
MAIN's copy moves. The formula with the engine's own k gives -6.1 / -2.2 / +1.6 px.

Fix (`src/Stereo/Grading.cpp`, `FlattenHudComposite`): under the asymmetric projection the 512-byte
composite block (found by the existing fingerprint, `hud_cb_block_plausible`) is uploaded with r3.xy
= 0, in both constant uploaders. The engine's values are kept in `CyberpunkVR_HudEngineCurvature`;
the HUD panel's composite takes them when the block carries the zero (`HudPanel.cpp`, `ColorBlit.cpp`),
so the panel keeps its bend. The fingerprint accepts the zeroed block. Knob `CyberpunkVR_HudFlatInEye`
(1), counter `CyberpunkVR_HudFlatInEyeBlocks`.

Live A/B, time frozen, marker at height 76: flattening on 0 px vertical / -237 px horizontal (the
scene's), off -7 / -239, on again 0 / -237. Between the states only the in-eye HUD changed (markers,
the FPS counter); the panel did not.

## 5. A second quest marker on a looted body doubled in the second view

Seen live (game paused over a body): two visible `QuestMappinController` roots at the same world point
(-474.14, 389.80, 132.65) and the same margins; in MAIN one icon, in the second view two, 3 capture px
apart. Hiding each in turn: [43] sat at the NPC's parallax, [41] at infinity. Stage counters
(`CyberpunkVR_WorldMarkerMappinStages`, diagnostics on for 2 s): 11 roots remembered per frame, 9 with a
depth. The position hook required the controller's `SetProjectToScreenSpace` flag (+0xE3) for a depth
and, with it off, marked the root "inherit" -- from the world container, which has no depth.

Fix (`src/Hooks/WorldMarkers.cpp`): the world point is taken whatever +0xE3 says (a clamped edge icon
still gets none), and `Paint` lets an inheriting root keep an enclosing projected root's depth (a
nameplate) but paints it at its own point when there is none. Check on QuickBoot, time frozen,
`SetProjectToScreenSpace(false)` set live on the NPC's quest mappin: eye offset -202 px with the flag on,
off and on again, the same as the NPC below it.

## 6. The second view's HUD a frame ahead for single frames in head turns

Measured with simulator frame bursts (`burst_command.json`, 48-64 consecutive frames; the simulator's
head pose angles are RADIANS) during a +-0.2 rad head turn: per eye, the quest marker against the wall
behind it. Both eyes lag their scene by the same ~0.8 px in the fast part of the turn (the flat game's
own HUD lag). Once in 48 frames the second view's marker moved 5.5 px while its scene moved 3.1:
its HUD was the next frame's.

Cause: `vd_lend_composition` runs on every node the second view dispatches and lent the newest object
MAIN had been assigned. The game thread assigns the next frame's while this frame's render is still
running, so the view switched objects midway -- 20 switches in one 64-frame burst
(`CyberpunkVR_CompLendFrameStats[6]` vs `[1]`); when a switch landed before the composition node, that
frame drew the next frame's HUD. Every lend went into an empty block, so the view's block is fresh per
frame.

Fix (`src/Stereo/ViewReuse.cpp`, `CyberpunkVR_CompLendFrameLatch` = 1): the block keeps the object its
first node received while that object is still one of the two the port holds a count on. Stats [6]
lends into an empty block, [7] later assigns the block kept, [8] kept objects no longer held (0 in
every burst). Bursts: latch on 0 replaced objects and 287 / 1396 kept assigns; second-view minus MAIN
deviation max 0.41 / 0.34 px with the latch, 1.04 px without it.
