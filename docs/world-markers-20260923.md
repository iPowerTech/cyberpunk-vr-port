# World marker stereo investigation

Task: loot, quest and other projected markers must stay on their world targets in
both eyes, with MAIN/right-eye placement as the reference. The user's clarification
was that one marker is visible per eye and the left-eye marker misses the loot.
Equal image coordinates alone are therefore not acceptance.

## Capture evidence

`world_space_markers.rdc` under the game's `render-doc-capture` directory was
replayed with the game closed. Replay was closed before PID24564 was examined.
No images were submitted to the chat renderer (the preceding task had repeatedly
failed with Bad Request on image input). Pixel analysis uses local PNG/BMP data.

* VRCAM/MAIN pairs: loot 42892/89114, quest ring 42868/89090, quest icon 42880/89102.
* All three pairs have identical post-VS positions. The ring's animated alpha
  differs, not its position. The VS is ResourceId::841, hash
  ced01c35cdb4e6890a872d63f152242f, an ordinary 2D projection with z=0.
* Both viewports are (0,0,2560,2560). Both use HUD ResourceId::222603.
* Native composition at 43175 and 89270 uses identical HUD warp parameters;
  the completed targets were saved after 43179 and 89274. Loot icon component
  bounds match exactly in the output: e.g. (1263,1622)-(1273,1634) and
  (1271,1665)-(1281,1677). The quest icon bounds also match.
* World camera positions differ by about 64 mm:
  VRCAM (-1830.375,-2354.670410,31.023346),
  MAIN (-1830.316284,-2354.695801,31.023346).
* This establishes a shared flat marker projection over a genuinely stereo world.
  Substituting MAIN coordinates again cannot make it follow the left-eye object.
* The XR textures in this RDC are identical, so they cannot validate the live
  stereo submit: the previous captured-run log reports second-eye fallback.

Data: `build/world-markers-re/` (draw/shader/cbuffer exports, post-composition
textures, numeric pixel measurements, and `live-preview-24564.bmp`).

## Live observation (PID24564; never reuse these addresses)

Simulator is running, user remains at the test position. Plugin globals were read
through an exactly matching PDB/DLL with read-only process access:
MainIsRightEye=1, HudDistanceM=0, FlatDistanceM=0, StereoEyeSubmits increasing.
`xr_hud_to_second_eye=0`, `xr_comp_lend_set=1`: native composition runs in both
views. The legacy ColorBlit HUD offset is not the active cause.

Visible WorldMappinsContainerController root has 94 children, four visible:

| Kind | World anchor | Distance to player |
| --- | --- | --- |
| Quest | -1833.567993,-2346.168213,26.267834 | 9.7288 m |
| Loot | -1828.197510,-2354.154053,30.279823 | 2.6492 m |
| Loot | -1829.594971,-2352.967285,30.277031 | 2.3904 m |
| Loot | -1829.642700,-2353.118652,30.259834 | 2.2421 m |

These are radial player distances, not camera-space forward depths. Do not use
them directly for IPD/z. Read world positions with controller:GetMappin():GetWorldPosition().
Controllers come from each visible child of the world-mappin root, via GetController().
Position/size helpers are parent:GetChildPosition(widget)/GetChildSize(widget).

## Static investigation

`engine_re/scripts/re_world_markers.py` opens the existing cp2077.i64 without
analysis, verifies the executable hash and closes without saving. It only reads
assembly/xrefs/data, and never invokes Hex-Rays. Dumps are in build/world-markers-re.
The production implementation and deployment status are recorded below.

* inkScreenProjection: size 0x120; data at +0x80, world offset +0xC0,
  user-data weak handle +0xD0, distance +0x104, previous/current/UV positions
  +0x108/+0x110/+0x118. Native update RVA0x4E7E24 receives projection, camera
  interface, screen size, float dt; computes target and projection. Called from
  0x4E7BD8. This projection is already flattened before the renderer sees it.
* RegisterScreenProjection script thunk 0x92C8F4 -> 0x92C828; constructor0x92C504.
* BaseMappinBaseController SetProjectToScreenSpace writes +0xE3. Position setter
  0x2E37AC reads it and writes root margins; false just skips placement, not a 3D
  render mode. Called by update0x2E2228 and other mappin paths.
* DrawComposition 0x20A264 -> 0x1ECFDC -> 0x1ECBE0 walks 0x1D0 composition
  elements and emits keys. It consumes prebuilt UI data; do not reintroduce the
  removed DrawComposition detour/provider calls (ViewReuse.cpp explains crashes).
* Window draw preparation 0x28A444 -> 0x28B750 -> 0x28B860 schedules the
  UI/Window/WidgetDraw job through builder0x28B938. Job callback0x28B664 retains
  its window and calls 0x2EE510(window, drawContext). Next investigate the widget
  paint recursion and where local geometry is flattened, to preserve per-marker
  depth/ownership before batching. Do not use nearest-rectangle vertex matching:
  overlapping markers and unrelated UI would be ambiguous.

Further verified UI path:

* Widget paint `0x2EE510(widget, context)` recurses through compounds via
  `0x2EE33C`. It calls the widget's virtual +0x230. This is an exact per-widget
  scope usable for identifying the projected root and its descendants.
* Image vtable0x2AEC5F0 +0x230 ->0x2EC9C4 ->0x2EC124 ->0x2EBBD8 ->0x2ECBA4.
* `0x2ECBA4` reserves six vertices/indices through0x2F20A8, allocates an individual
  quad record through0x2ECC64, fills its layout via0x2ED03C, sets kind at+0x48 to0,
  fills style at+0x50 via0x2ECB58. Do not use +0x48 as depth: it is a primitive kind.
* Quad record size is 0x130. Allocator0x2ECC64 returns pool.data + index*0x130
  and stores the allocated index in the current draw batch's index array.
  Base layout: affine2D at+0..0x14, sizes/position+0x18..0x24,
  clip rect+0x28, computed bounds+0x38, kind+0x48, style+0x50.
* `0x2F20A8` batches primitives by material/texture/etc. Batch stride0xB0.
* Existing native uploader hooks in Stereo/Grading.cpp are BufUpload(idx,size,src)
  at0x1F088C and CbUpload(size,src) at0x1EE3CC. They already safely pass temporary
  source copies to the synchronous original uploader, using `t_view_side`.
* UITransform is strictly 2D. Matrix helper0x4F74B4 produces4x4 only for certain
  widget/effect paths; ordinary image quads flatten through the 0x130 record path.
  A shader-only change cannot recover an anchor which is absent from its inputs.

## Implementation

`WorldMarkers.cpp` records each native mappin/screen-projection root's forward
depth, retaining a weak root identity. Painting propagates ownership through the
widget subtree. The native primitive allocator is tracked by pool/index, including
pool growth and index reuse. The quad/triangle emitters at0x2F2708/0x2F2E40 preserve
that depth in otherwise-zero vertex z before the UI geometry is shared by views.
Two exact native ink VS fingerprints decode it, leave MAIN unchanged, and use a
per-view flat-camera coefficient for VRCAM parallax. Untagged shader outputs match
the captured native shaders in WARP stream-output comparisons.

Text uses the separate native text geometry path0x2F3B10. A CALL replaces the LEA
at0x2F3D58, captures the depth in its matrix before native publication and performs
the original LEA before returning. The native synchronous constant uploaders
decode that matrix for MAIN and apply the corresponding VRCAM translation. The
call preserves volatile registers, XMM0..5 and flags, and has valid Windows unwind
metadata. A stack-trace test failed with a mid-function JMP and passes with CALL.

Hook signatures guard installation. The vertex shaders are compiled as SM6 by
Windows SDK DXC at build time and embedded into the DLL; no shader file or runtime
compiler is deployed. Both native ink substitutions must succeed before tagging
starts. The diagnostics are exported
as `CyberpunkVR_WorldMarker*`; `WorldMarkerStereo=0` disables new tagging/parallax
while shader/matrix decoding remains in place for already cached geometry.
Minimap/map controllers and clamped world mappins are excluded. HUD panel placement
and the simulator runtime are unchanged.

All eight tests in `tools/world_marker_tests` passed,
including GPU sprite/procedural/text projection, native shader output parity,
native pixel-shader linking, invalid-camera rejection and the ASM register/stack
test. The native pair-link test caught the SM5/SM6 mismatch in the runtime FXC
prototype; SM6 fixes it. This is software D3D12
verification; native hook counters and visual world attachment were checked in
the subsequent launches recorded below.

Installed DLL SHA256:
`2a516851e86db6f0522decf623406a0ef890a291525196c2f7bd7746a3d42c96`.
The complete Release rebuild passed. Final deployment report:
`build/roomscale-plugin-deploy-20260923-154039/deployment.json`.
Restore the original known-working DLL from
`build/roomscale-plugin-deploy-20260923-145020/CyberpunkVR_Stereo.before.dll`
(SHA256 `0135c967a8047546dd8e5286361ad5cb44d0a29cb2fdb7e0b0a6c5286b747230`).
Settings/calibration hashes are unchanged. PID24564 was closed for deployment;
the user starts the next launch. RenderDoc replay and the later CPU-only capture
parser are both closed. No image inputs have been sent to the conversation.

Fresh pre-deploy evidence: `build/world-markers-re/live-before-24564.json` and
`before-deploy-24564.bmp` (14:47:34 local time). The user remained at the same
position. Next: resolve a fresh PID, run the read-only `live_probe.py`, check both
shader-install messages and advancing root/vertex/camera/text counters, then check
attachment to the loot in the left eye. Do not claim visual acceptance from
matching image coordinates or from the WARP tests alone.

## First native launch: collector rejection fixed

The user reported unchanged placement in PID26788. The loaded d115... DLL and
both SM6 vertex substitutions were verified. `live-26788-first.json` recorded
advancing MAIN/VRCAM flat-camera uploads (about24 each over0.5s), but zero roots,
quads, text records and text uploads. This was an inactive collector, not an
incorrect parallax amount.

Live CET inspection found the WorldMappinsContainerController's game controller
separately from its widget logic controller. Its Root, HUDMiddleWidget, HUD Root
and Base Window all have `GetController()==nil`. The old ancestor-logic test
therefore rejected all four visible markers. A live eligibility check returned
visible=4, eligible=4, ancestorControllers=0.

The collector now walks the actual MappinBaseController's RTTI parent chain,
rejecting BaseMinimapMappinController and BaseWorldMapMappinController. It still
requires inkHUDLayer. Collection no longer depends on visibility being assigned
before the native position callback; the native paint path handles visibility.
Seven mappin-stage counters and five direct-projection-stage counters were added
to distinguish future collection failures from rendering failures. Release build
and the eight tests pass. PID26788 was closed for DLL-only deployment; the user's
next launch was checked as follows.

## Collector verified in PID10760

The fresh launch at 15:43 loaded the matching 2a516... DLL. Both native ink VS
variants and the ownership hooks installed. The read-only sample in
`build/world-markers-re/live-10760-collector-fixed.json` reports 61 roots,
33279 -> 33466 tagged quads and 27 camera uploads in each eye over about 0.5 s.
All seven mappin collection stages advance. The user reported "Ну вроде работает"
for marker placement in the current loot scene.

The direct screen-projection and text counters remain zero in this sample;
their in-game paths are not validated by this scene. The eight software tests
remain the evidence for those paths.

The user subsequently noticed a motion trail only in the left eye and clarified
that it appears to be the flashlight's shadow, rather than the marker icon; MAIN
does not show it. This is an open, separate rendering observation, not a confirmed
regression from marker reprojection. The existing local-shadow reuse switch was
confirmed live at 1, then temporarily changed to 0 through its existing INI hot
reload for comparison. Skip counters stopped advancing while both camera upload
counters continued. Evidence: `shadow-10760-reuse-before.json` and
`shadow-10760-reuse-disabled.json`; original INI:
`build/world-markers-re/shadow-10760-vrport-before.ini`.
The user subsequently confirmed the ghosting disappeared completely. The installed
key remains0, and native local-shadow updates are now the source default. See
`docs/local-shadow-motion-20260923.md` for the prior reuse experiments and the
new evidence; exact GPU timing and performance cost remain unmeasured.

## September24: television/NPC chatter text restored

The user reported a visible backing frame without text for projected subtitles
at a television and some NPCs. These lines belong to `ChattersGameController`,
not the separate-window `SubtitlesGameController` used for ordinary dialogue.
WolvenKit inspection in PID15980 found a visible, fully opaque `text_normal`
containing the actual Russian television line, with measured layout731x80.

With diagnostics temporarily enabled, native text tagging advanced to3556 while
both direct text-upload counters remained0. Disabling `WorldMarkerStereo` and
invalidating the chatter widgets restored the text in both simulator eyes.
The pause and both diagnostic/control flags were restored after the comparison.

The native composition path copies the text affine matrix into draw records and
publishes transposed3x4 matrices through its batch at EXE+0x1ECBE0. It bypasses
the per-object112-byte upload hooks. Consequently, the encoded depth remained
in the matrix Z translation and the vertex shader clipped the text. The saved
font VS disassembly matches the already replaced `shader_92_2.dxbc` (DXIL hash
29e41fdc6d6d3e8fa9fa79e1c948100b).

Both replacement ink shaders now decode the reserved matrix tag directly,
restore the native Z calculation before projection, and apply the same signed
per-eye depth correction used for marker sprites. Existing direct-upload
decoding remains compatible: a decoded matrix is not corrected twice.
This adds no textures, allocations or new native detours.

The new `world_marker_text_batched` WARP test sends the tagged affine matrix
straight to the GPU. It failed on the old shaders with
`batched text depth tag clips MAIN text`, then passed after the fix. Coverage
includes both shader variants, three aspect ratios, four depths and either
MAIN-eye convention; clip depth, UVs and colors must remain unchanged.
All9 world-marker CTests, including native shader parity/linking and thunk ABI,
and the Release plugin build passed.

The obsolete `CyberpunkVRPort_LootUi` redscript was also removed from the source
and installed scripts. Texture HUD rendering now receives the game's original
loot layout without the old0.70 scaling, translation and tooltip reparenting.
Full deployment backs up and removes the retired file; packaged upgrade
instructions name it explicitly.

Installed DLL SHA256:
`94fdf3f8a5e200b9efa65643083c7b69a7a35004bd9fa7b14eb8d1b4822f57f9`.
Deployment: `build/roomscale-plugin-deploy-20260924-162701/deployment.json`.
Settings and calibration hashes were preserved. After the user's restart,
PID18948 installed both VS variants and ownership hooks; redscript compilation
completed successfully and the old LootUI file was absent. The user confirmed
the fix works. No separate NPC scene or headset-motion test was performed.

Evidence, screenshots, native assembly, test/build logs and the removed script
backup are in `build/world-chatter-20260924/`.

## September25: marker scale 0.7

World-mappin roots now use 0.7 of the game's authored scale, increased from 0.5
at the user's request. The existing ownership guard still prevents repeated
position updates or reappearance from compounding the scale. Minimap/world-map
icons and projected chatter subtitles keep their existing sizing.
