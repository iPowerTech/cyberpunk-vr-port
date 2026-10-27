# Engine stereo world markers, game 2.5

Task: fix world-marker movement during head turns, inconsistent stereo placement,
and markers below their target. User additionally reproduced an NPC icon moving
up while the NPC walked. Game launches were authorized for this whole task.

## Findings and change

The old marker path carried inverse depth in flat canvas geometry and added IPD
disparity. It did not account for a change between the camera used by the native
UI update and the camera used to render the world. Live motion samples confirmed
different UI/render poses. The native UI projection was internally consistent.

`WorldMarkers.cpp` now captures the native UI source projection with each owned
marker primitive. Its source is the camera at `gameCameraSystem + 0x60`, reached
through the native UI view, matching the game's `WorldToScreen` implementation.
Vertices are lifted from that source canvas to world coordinates. The replacement
VS uses the recording worker's own `t_current_node_view` eye descriptor, rather
than the global MAIN pointer, to project them into the current eye. The eye's
DLSS jitter is removed because ink is composed after upscaling. Negative vertex
alpha tags world coordinates; the VS restores opacity and preserves UVs.

The 864-byte native ink camera prefix is preserved. An 80-byte eye packet is
appended within the existing 1024-byte CBV allocation. Missing packets clip tagged
geometry instead of treating world coordinates as canvas pixels. Offscreen UI
effects retain their native local rendering; unique marker ownership propagates
to the final composite. Mixed source poses fall back to the depth-only path.

Direct and batched marker text now both keep their marker identity until the VS.
Previously the direct CPU decode could clear that identity before the stereo VS,
allowing the engine's separate monitor disparity to be applied again.

The separate NPC height problem was its cached presentation pivot. In the measured
case the cache was 256 by 256 with native offset `(-128, -92)`. The native screen
anchor is therefore local `(128, 92)`, while the presentation pivot was `(128, 128)`.
The port's scale of 0.364053756 moved that anchor down by about 30.4 canvas pixels.
It now scales about `-cacheOffset / cacheSize`, recalculated from native cache data,
so changes in cached bounds do not move the anchor. The original pivot is retained
for restoring the live A/B switch. No arbitrary NPC height offset was added.

Mappin collection runs after the native update finalizes clamp state and uses the
controller's actual projection anchor (VT + 312). Clamped edge indicators retain
the existing flat path. Ordinary HUD vertices retain their native behavior.

## Cached NPC mappins and the nameplate

NPC stealth mappins switch between their live root (`controller+0x40`) and a cached picture of it
(`+0x170`, `+0x190` set). The presentation manager (`sub_14047FE70` / `sub_142214170`) builds the
picture with `sub_1420B1940`, assigns it (`sub_140480C84`) and re-applies the stored screen point
outside the mappin update (`sub_140480C58`). Two port defects came from that:

- the live root kept the port's 0.3276 scale, anchor pivot and world depth while it was the
  picture's source, so the cache drew it small, off its anchor and, for up to a second, with lifted
  vertices. `ReleaseCachedRoot` now returns the game's scale and pivot and drops the root's marker
  entry whenever the controller presents the picture (measured afterwards: 0.9 and 0.5, 0.5);
- the out-of-update screen point was collected without a source and drawn flat for one frame; it
  now uses the last UI camera (`CurrentUiSource`).

The remaining jumps on head turns were the nameplate. The single `NpcNameplateGameController`
follows the NPC next to the crosshair -- in the headset the head's aim -- and slots that NPC's mappin
roots into its `mappinSlot` (`UpdateSlotAttachment`), while `OnNameplate` switches their projection
off. The slot sits about 115 design units (~5 deg) below the icon's own anchor; a live recording
showed 9 swaps in 5 s of head turns next to one NPC. `vrport_nameplate_marker.reds` keeps the icons
out of the slot and their projection and distance scale on; the nameplate keeps its name and health
bar. Confirmed in the headset.

A native reset of the live root's scale to 0.9 (`sub_140549638` inside the mappin update, caught with
a hardware breakpoint) runs before the port's re-scale in the same update, so it never reaches paint.

## Jitter, head roll and marker effects

- Jitter: the screen setter `sub_14054A0F8` rounds the margin to whole design units (`sub_14262E468`;
  1 unit = 1.326 canvas px on the 2.5 HUD), and the quad emitter's vertex transform `sub_14028A6D0`
  snaps vertices to the canvas pixel. Lifted into the world, both stepped against it while the head
  moved. `ExactMargin` re-applies the unrounded margin through `sub_14054B3A4` (live root: screen
  point; cached picture: + cache offset + `sub_142232480`), only where the game's value rounds from
  it. `QuadSnap` turns the snap off for marker records that are lifted. Measured over 8 s of yaw:
  the lifted centre's offset from the marker's world point ranged 8.8-10.8 mm at 8.6 m before, 0 after.
- Roll: the UI source camera carries the head's roll (axes read live at +-0.3 rad). Canvas vertices
  are now lifted about the marker's own point (`MarkerAnchor`, `CanvasToWorldLevel`): the point stays
  exact, the pixels around it go out along the level right/up of the source's forward. Text uses the
  same axes. Measured: the icon's up tilted up to 17.14 deg from world vertical under +-17 deg roll
  before, 0.00 after.
- Effects: a flattened effect took its marker's projection only when every record in it had one;
  one ownerless record left the composite quad flat, at infinity in each eye (a blurred double, left
  in the left eye and right in the right one). Records of one marker now decide unless the ownerless
  ones outnumber them. Not yet seen live: no marker effect occurred at AutoSave-8 (26060 composites
  in 8 s, none with marker records), and no marker vertex stayed on the canvas there.

Live A/B exports (all 1): `CyberpunkVR_WorldMarkerLevel`, `...ExactMargin`, `...Unsnap`,
`...CompositeLoose`. Diagnostics (with `CyberpunkVR_RuntimeDiagnostics`): `...CompositeStats[8]`,
`...CompositeMixed[4]`, `...Unlifted[5]` / `...UnliftedRoot[5]` (vertices left on the canvas by
reason), `...DetachedPaints` / `...DetachedPaint[4]` (top-level paints of a marker's descendant).
The mirror window shows no HUD; marker visuals need the headset or a capture.

## One-eye UI, the edge clamp and the HUD composite

- Size: ink sized every layer to the window, two eyes wide, so the HUD design came out 1.92x the
  one-eye size. The layers each eye's UI pass draws (`inkHUDLayer`, `inkGameNotificationsLayer`)
  are now one eye wide (`sub_14132654C`, `sub_140399094`; `src/Stereo/EngineStereo.cpp`). Their
  tick (`sub_14060787C`) gets the layer's own size and a view of the UI camera narrowed to the
  layer. That view's world-to-NDC (slots 13/16) takes x2 across, its NDC-to-world (slot 14) /2.
  Menus, the loading screen, the intro and world layers keep the window and stay one picture on the
  menu panel. The loading screen counts as one while `inkLoadingLayer` is ticked and active (+0xB0).
- Edge clamp: off for world mappins (`sub_14054A0B4` -> false; `CyberpunkVR_WorldMarkerEdgeClamp`,
  1 = the game's). In the headset the screen's edge is one eye's canvas, so a clamped marker stood at
  the edge of one eye only. Unclamped, the update puts the icon on its point's projection with no
  behind-the-camera handling (`sub_14054A890`), so:
  - a point in front is lifted to the world even off the canvas, and each eye shows it where it sees
    the point;
  - a point behind the head is not painted.

  Left flat off the canvas, a marker took the engine's 3D-monitor stereo term (2.2 NDC on the
  teleport marker, `markeroutline.rdc` event 94960) and showed in one eye only.
- HUD composite (the compute pass after the HUD blur pyramid, `markeroutline.rdc` event 95215,
  cb 432 bytes): its per-eye terms run about each eye's own centre.
  - `r3.zw` is the drop-shadow offset (0.0008 UV): the black outline right of and below every icon.
  - `r7` and `r8.xy` are the halo's tap offsets, radial from the eye's centre: the breach/ripperdoc
    ghost, on the left in the left eye and on the right in the right eye.
  - `r3.xy` is the bend.

  `FlattenHudComposite` (`src/Stereo/Grading.cpp`) now also takes the 432-byte block under engine
  stereo: it zeroes the bend and the halo taps and scales the shadow by `CyberpunkVR_HudShadowOffset`
  (default 0).

## Evidence and validation

Original capture: `worldspacemarkerscheck.rdc`. Complete native decompilations,
capture exports and live data are under `scratch/worldspace-markers-20261024/`.
Key 2.5 RVAs: mappin update `548FC4`, screen setter `54A0F8`, source projection
`54AD6C` / `54AE08`, projection update `2A4B18`, quad emission `28A064`, cached
presentation adjustment `2232480`. Source and metadata exports are saved locally.

The live pivot A/B/A changed the NPC cache centre's vertical displacement from
48.29 to 17.86 and back to 47.01 canvas pixels. Accounting for the cache's actual
anchor, the final DLL's 88 fresh walking samples had a mean vertical error of
-0.111 pixels, standard deviation 0.655 pixels, and range -3.454 to 2.091 pixels.
The native UI and render samples are asynchronous, so this is not a claim of
subpixel image-space accuracy in every rendered frame.

Three temporary fixed world markers were tested through yaw and pitch motion:
240 live samples, each containing those markers. Their source geometry and saved
camera remained consistent (horizontal residual SD 0.36–0.38 canvas pixels;
vertical SD 0.80–0.83, including the marker's authored offset/animation).
Both eye packet counters advanced together; world sprite and text counters also
advanced. Temporary markers were unregistered after the check.

All 13 CPU/WARP tests pass, including world sprites/text under changing eye poses,
unjittering, cache-pivot stability across sizes/scales, the 2.5 text-hook ABI, all
six native VS variants in mono/both eyes, and linkage to native pixel shaders.
`tools/world_marker_tests/extract_fixtures.py` creates those 2.5 fixtures from the
local game cache. Fixtures are kept in ignored build output.

Built with the 2.5 SDK target `cyberpunkvrport_stereo`, installed DLL SHA256:
`1981EF6CC73A7FFFD7D2353BA6A5EF46161BC3BAEDDEBE330E6E2D426918B881`.
`CyberpunkVR_WorldMarkerReproject` defaults to 1; marker probes and runtime
diagnostics default to 0. Original DLL/PDB and affected pre-task sources are in
`scratch/worldspace-markers-20261024/before/`.

DLSS Transformer and the preceding bloom, particles, light, LOD and PSO fixes were
retained. The earlier fog investigation is separate and remains unresolved.
No RenderDoc replay was open during these game launches. A final launch needed
manual confirmation of the game's modded-save dialog after the launch helper
timed out; the same game instance then completed loading and the checks above.
