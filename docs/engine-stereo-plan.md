# Engine stereo (2.5 Odyssey 3D path) as the port's second eye — plan (2026-10-23)

## Why

Game 2.5 renders two eyes in one frame for the Samsung Odyssey 3D monitor: one scene traversal, shared shadows,
per-eye prepass / G-buffer / decals / transparents / UI, both eyes side by side in one target (render feature 98).
Measured on a city save, one launch: the port's own stereo (MAIN + VRCAM RTT view) ~28-42 fps, MAIN alone with
the engine's two eyes ~78 fps (stand-in frame timer, 2 eye queries per frame = feature 98 on). Engine frame
generation (features 88/89/75) is switched off by stereo, the port's own FG is not affected.

## What the engine does (IDA, 2.5)

- Gate, per frame: `sub_14227E5D0` sets feature 98 when `[Odyssey3D]` cvars say stereo AND the Samsung SDK
  wrapper (platform+0x4680) is loaded AND its `Available()` answers true. The plugin is that SDK itself
  (src/Stereo/EngineStereoSdk.cpp, 2026-10-24): the wrapper's constructor `sub_142282E60` is followed and handed
  the plugin's factory and interface, and the cvars OverrideEnabled / StereoEnabled / SBSDebug (.data RVAs
  0x3929C98 / 0x3929CD0 / 0x3929D08, value byte +0x30) are set in memory. No Samsung SDK, no HKCU key, no
  `[Odyssey3D]` ini (tools/odyssey3d_fake_sdk and `engine/config/platform/pc/zz_cvr_odyssey3d_test.ini` are no
  longer used; `install.py --remove` takes an old key away, a key the plugin finds anyway is let go).
- Stereo starts at the main menu: `Available()` answers false and StereoEnabled stays off until
  `SingleplayerMenuGameController.OnInitialize` (mods/redscript/CyberpunkVRPort_EngineStereo, native
  `VREngineStereoMenuShown`). Splash screens, intro videos and the first loading render mono and are shown as one
  picture on the menu panel (EngineStereo.hpp `OnePicture`). A save's loading screen is drawn once over the window
  while the frame is mono and, once the world under it renders both eyes (feature 98, the screen waiting for its
  button), by each eye's UI pass into its half squeezed to half width: the capture then takes one half as it is
  (`LoadingSideBySide`) instead of squeezing the window (it showed two narrow copies side by side). Not while the
  main menu is up (`MainMenuUp`, the redscript also wraps `OnUninitialize`): the loading layer stays active under
  it, and the menu is drawn once over the window -- one half of it showed the menu shifted. The port's menu mode
  is no guide here: it is on at the Continue screen in some launches and off in others.
- Layout: one target W x H, left eye x in [0, W-W/2), right eye [W-W/2, W); per-eye passes re-send each draw
  batch with the half viewport; `_RightView` targets are half width.
- Eye cameras: MAIN's camera build (`sub_1402135EC` -> `sub_14206EC30`) makes camera+0x3D0 (left) and +0x3D8
  (right) at centre -/+ 0.5 * sep * right (camera+0x250). sep = platform vfunc +1088 (`sub_14226F600`),
  convergence = +1104 (`sub_14226F380`). Per frame `sub_1404240DC` refreshes eye fields (+0x3B0 eye pos,
  +0x39C/+0x3A0 display size, +0x3C0 sep, +0x3C4 conv); `sub_140296EA0` builds the eye projection as the
  symmetric one with the off-axis terms P20/P21 from `sub_1422CC5B0(sep/2, conv)`.
- Upscaling: one `dlss.Evaluate` over the whole side-by-side frame.

## M1 — headset shows a correct picture (port mode `xr_engine_stereo=1`, default 0)

1. Second view off: VRCAM never requested in this mode.
2. Resolution: MAIN target = 2 x eye width (VrCore forced render / window width).
3. Cameras: MAIN at the head centre (no eye offset in the port's pose chain: `CyberpunkVR_IpdInWorldPos` and
   the FinalCamera / LocateCamera eye paths); hooks on +1088 -> IPD from the runtime's eye views, +1104 ->
   parallel (convergence far away).
4. FOV: one symmetric frustum covering both eye windows (`CyberpunkVR_AsymProjection` 0), each eye submitted
   with the frustum actually rendered.
5. Capture/submit: the presented backbuffer is split, left box -> eye 0 snapshot, right box -> eye 1 slot;
   no VRCAM composite for eye 1 (the engine already drew HUD and vision per eye).
6. Off in this mode: hidden-area mask, VRS / foveated DLSS, inner-edge fill, asym shader twins, wide-merge
   SSR/AO, exposure twins.
7. Check on the simulator + RenderDoc (eye positions +-IPD/2, parallel, FOV = submitted), then the headset.

### M1 status (2026-10-23): works on the simulator

Implemented as `xr_engine_stereo` (vrport.ini, read once at start): src/Stereo/EngineStereo.cpp (switch,
detours on the SDK wrapper getters sub_142284990 / sub_1422849D0 / sub_142284950 -> sep = 2 * GetDesiredHalfIpd(),
weapon sep = sep, conv = 1e6 and the weapon convergence cvar dword_143D31790 = 1e6, feature-98 watch from the
view producer), window / swapchain / settings width x2 (Core/VrCore.cpp), `CyberpunkVR_IpdInWorldPos = 0`,
both eyes the union window (AsymmetricProjection.hpp Windows(): guard 1), VRCAM never requested
(VrcamConfig.hpp), backbuffer split in CaptureMonoPresentedFrame (main eye's half -> snapshot, the other half ->
second-eye slot, no composite, both labelled with MAIN's image pose), eye swapchains half the backbuffer width
(OpenXRPresent.cpp), hidden-area mask / fixed VRS / foveated DLSS / SSR+SSPR+AO wide merge off (the sun
contact-shadow merge too since 2026-10-23; it had no gate). The fixed shader twins stay bound in this mode: fog,
SSR/SSPR centre, particle facing, view_dir, screen_fx read the eye's own camera block; the cone AO and exposure
centres are written from MAIN's window (t_view_side 0), i.e. the centre's, not each eye's -- not checked live.

Simulator run (Quest 3 profile, launcher 2648x2864 per eye -> window 5296x2864, city save): feature 98 on,
both eyes from the engine frame, VR hands, the engine's HUD per eye (in the projection layer, with widget depth),
hands ~25 px disparity in the preview (= IPD 64 mm at ~0.55 m), far buildings 0, no vertical offset, log clean.
Known: the union window is 108 deg wide, so square pixels grow it vertically by 20.9 % (rows outside the lens)
-> M2 per-eye windows. One crash on game exit (exe+0x1957B3, refcount release in teardown; the port's
WorkerThread address on the stack is a thread-proc pointer, not a frame) -- not investigated.

## M2

### Per-eye windows (2026-10-23): done, HUD open

Detour on the matrix builder sub_140296EA0 (src/Stereo/EngineStereo.cpp): an eye camera (+0x399 bit 1, kind +0x398
1/2) gets its own eye's aspect and NDC shift before every build; MAIN (the centre) gets the horizontal union of the
eye windows at their one vertical span (AsymProjection.cpp EngineStereoCentre). Export
`CyberpunkVR_EngineStereoEyeWindows` (1) switches it live; 0 = both eyes the union (M1). Simulator, Quest 3 profile:
eyes aspect 0.9246, shift x +-0.2425, shift y 0.1774, built P20 = +-0.2425 (off-axis term ~0), square pixels, window
grown only 1.7 % (ladder). Each eye shows its own lens window.

HUD: the engine composites the HUD per eye from a 5296x2864 surface laid out over the CENTRE's window (DrawHUD
sub_14033E388 hands feature 98 to the HUD constants, v327), so with the union centre the HUD spans 108 deg and its
outer elements reach one eye only (minimap missing on the left, health bar cut on the right). The VRCAM mode has the
same effect at 94 deg (MAIN's window). Fix candidates: lay the HUD out over the eyes' overlap (find what the stereo
HUD shader takes as its window), or keep the union for both eyes (EyeWindows 0) until then.

One GPU page fault (garbage VA, main menu, 27 s) on the first per-eye launch; the next launch was clean -- not
reproduced, cause unknown.

Per-eye windows vs the engine's stereo passes (2026-10-23). Lighting on a wall differed between the eyes and the
difference flipped with head yaw; with `CyberpunkVR_EngineStereoEyeWindows` 0 the eyes matched (same patch,
registered: mean RGB 77.8/86.3/46.3 vs 77.3/85.5/46.2; per-eye windows: 54.7/69.0/21.7 vs 71.3/81.1/36.1).
The 2.5 stereo branches were written for the Odyssey's eyes, which differ from the centre by a small convergence
term; the port's eyes differ by P20 +-0.2425. Checked in SBS_M2.rdc:
- deferred light tile culling (65107): per-eye projections and positions in its b6 (rows 2-5 right, 11-14 left
  with P20 -+0.2425) -- correct; lit buffers agree between the eyes (block-registered on the albedo).
- volumetric fog scattering (65472): froxels from each eye's inverse projection -- correct; but the fog's own
  light tiles (48x26x16, thread 0 per 4x4x4 group) are cut from the CENTRE's projection and position (b6 rows
  0-3, 7) with each eye's tiles folded onto it. Fixed in the fog twin (tools/asym_tests/asym_fog_shaders.py,
  second recipe, stereo branch only; 2.31 output byte-identical). A/B live: no visible change on that wall.
- the port's asym twins (fog, SSR, AO, shadow) patch the flag==0 path; in engine stereo the engine's own
  stereo branch runs, so they are inert there (not harmful).
- auto-exposure: one value for the whole side-by-side frame (2.5 metering shaders read no stereo flag).
- what still differs on that wall: the projector's green beams and haze over the wall (strong in the left eye,
  weak in the right, identical with the union window). Its shader is not in SBS_M2.rdc (another scene); a
  pixel-shader scan for ndc * camera tangent found no camera-tangent hits (the stereo branches fold x through a
  select the scanner does not follow).
- shadowcheckSBS.rdc (the wall, per-eye windows): the eyes agree in the scene colour up to 42469 and differ from
  the composite 42752 on (99 of 537 registered blocks > 0.25 log2), unchanged through fog, transparents and DLSS
  -- the difference is made before the upscaler. Its source is the ambient pass 42632/42646 (one draw per eye,
  writes 193036). Read first as SSR (its inputs differ at the inner edges), but WRONG: with SSR and local shadows
  off in the settings the wall still differs, and the SSR merge below changes nothing there.
- THE WALL IS THE CONE AO (2026-10-23; the user: gone with Ambient Occlusion off). The engine runs DrawConeAO over
  the side-by-side frame with the node's own two-halves mode off (block row 14.y = 0, read live from
  CyberpunkVR_DebugAsymAoBlock): one picture at half resolution (2648 x 1432) with the centre's tangents (row 0:
  1.3764 / 1.1981), each eye's picture spread over half of the union frustum and both inner edges meeting at the
  seam, where the cone steps read the other eye's depth. The engine does fill the eyes' tangents (rows 12 / 13 =
  1 / P00, 1 / P11 = 1.1077 / 1.1981), but its two-halves mode builds positions with row 0, has no window shift,
  and a step past an eye's inner edge lands in the other half and is rebuilt as if at its own far edge.
  Fix: Stereo/Grading.cpp (ao_centre_write, CyberpunkVR_EngineStereoAoHalves) turns the mode on, row 0 = the eyes'
  tangents, 12.z / 13.w the left / right eye's centre (shift.x, -shift.y: +-0.2425, -0.1774), 12.w STEREO_MAGIC,
  14.w the separation; the asym_ao twins' stereo recipe (asym_ao_shaders.py cross_steps) centres every position and
  projection on the pixel's eye and continues a step that leaves its window in the other eye (the ray at the
  pixel's depth, moved by the separation, projected there, the depth gathered, the point moved back). Live A/B on
  the wall, log2 R/L by band at the right eye's inner edge: off +0.83 +0.77 +0.90 +0.16, on -0.05 -0.05 0.00 +0.01.
- CONTACT SHADOWS (2026-10-23). The engine draws RenderShadowmask once per eye (viewport 768 of the 1536-wide
  checkerboard target in shadowcheckSBS.rdc, events 41776 / 41782) with its two-halves mode on (b0 row 32.x = 1),
  and both contact marches (local lights, sun) test the eye-local uv against [0, 1)^2 and fade towards the eye's
  edges. b12 (2512 B, the global view block) holds both eyes' world->clip (rows 93-96 left = b1 of the left draw,
  113-116 right; only the x row differs) and the first-person ones (125-128 / 129-132). asym_shadow_shaders.py
  stereo_cross continues a sample past the inner edge in the other eye (screen parameter t -> clip parameter
  mu = t w0 / (w1 - t (w1 - w0)), the ends projected with the other eye's x / y rows; the depth row is shared),
  reads that half, takes the hit's material there and the smaller of the two eyes' edge fades. Checked against a
  direct projection with the captured matrices (4 decimals). Live switch: CyberpunkVR_AsymShadowPatch.
- SSR (2026-10-23). The indirect traces (2.5 b86f8871454997e6 / c827b32cf54f25fb / 8661cc86756e2ecc, 24 / 64 /
  128 steps) run per eye (b6 row 40.w = 2 right) with the two-halves mode on; the march is linear in the eye's
  (u, v, depth) and stops at |uv - 0.5| < 0.5, and hits fade by saturate(10 - 20 |uv - 0.5|). asym_ssr_shaders.py
  stereo_cross maps the march line to homogeneous world points with the eye's inverse (b12 rows 81-84 / 101-104,
  input (u W, v H, depth, 1) as a row vector, u eye-local, W the full side-by-side width = b0 row 2.x) and to the
  other eye's clip with its rows 93 / 113 -- affine in the step parameter, so a sample is three multiply-adds; a
  crossed sample reads the other half, the march goes on while inside either, a crossed hit takes motion, colour
  and material from the other half, and every hit's fade is the better eye's. First-person pixels keep the
  engine's march. The Low trace (acaedc11005a92d0) maps only its pixel, not the march, to the eye's half (its
  right eye marches through the left half: engine bug) and is left alone. With RayTracedReflections on, check
  CyberpunkVR_AsymSsrPatchBinds: the traces may not run at all.
- The autosave crash (exe+0x269E06A, call [rax+18h] -> 1): the C++ entry detour on the capture-size query
  (Hooks/PhotoModeCapture.cpp) called GetForcedWindowWidth -> engine_stereo::Enabled(), whose epilogue left a
  stack address in R11, which the savegame thumbnail's caller keeps a zero in across the call. Now a MASM thunk
  (PhotoCaptureClampThunk.asm CvrPhotoSizeDetour) calls the native query, saves every volatile register, the flags
  and xmm0-5 as it returned them, runs the policy, restores them; and the policy returns before any VR size code
  unless the request is Photo Mode's own (type 1). Two autosaves after the fix wrote 180 / 192 KB thumbnails.
- The SSR side (kept, stereo branch only): the march stops at its own eye's window, so at an inner edge the
  confidence drops; the ambient-pass twins (stereo_merge\, tools/asym_tests/stereo_ssr_merge_shaders.py) take
  what an eye lost from the same surface in the other eye's half (b12 rows 81/101 eye inverse <-> 93/113
  world->clip). Modelled on the capture: eye-to-eye confidence difference 0.077 -> 0.027 mean.
- Clouds (2026-10-23, CPCHECKSKYLIGHTFOG.rdc: dark sharp clouds in the left eye, soft and nearly gone in the right).
  The volumetric clouds march at half resolution in ONE dispatch over the side-by-side frame (event 42315,
  1536x830 colour + depth), composited by one tiled draw (42351). The march takes its ray from the pixel's half
  (rows 89-92 / 109-112 of b12 by x > W/2) -- the low-res halves match in shape (offset 186 px) -- but its
  aerial-perspective lookup into the froxel fog builds u from the CAMERA's eye (b1 +848, kind == 2 -> + W) and
  clamps it into that half; the dispatch's block is the right eye's (P20 -0.2428), so the left eye's clouds read
  the right half of the fog at their eye-local position. Right/left low-res cloud luminance 1.25 median (up to
  1.9 on the dark parts); the composite changed the left eye's sky 0.0034, the right's 0.0006. Fog itself is equal
  per eye (+0.0773 / +0.0770 on the sky). Twins (stereo_eye\, tools/asym_tests/stereo_eye_shaders.py, switch
  CyberpunkVR_StereoEyePatch): compare the pixel's eye (the shader's own phi, 1 outside the two-halves branch)
  instead of the camera's kind; both march qualities (8a6c46c528ce5160, c944329b340f33c2). 82462a16ccf6765c has
  the same lookup but takes its eye from the camera block itself (dispatched per eye) and is left alone.
  Originals: extract_shader_cache.py --kind cs over staticshader_final.cache (build/patchday/shaders-2.5/all_cs);
  RenderDoc's "shader hash" is the DXIL HASH part, not the container hash.
- Fog chain per eye (checked on both captures): injection stereo branch uses b6 rows 24/29-31 L, 37/42-44 R (eye
  position, inverse projection with the shift), history clamped to the froxel's own half, light tiles from the
  centre rows 0-3/7 -> asym_fog eye_tiles twin (bound in the second capture); integration and apply two-halves aware.
- Weapon plane: plane 2 of the producer takes its separation from getter +1096 and its convergence from the cvar
  dword_143D31790 (Odyssey3D/WeaponConvergenceDistance, 0.5 m default), both overridden; the engine's own reduction
  (Odyssey3D_WeaponStrength setting, DynamicStereoController sub_142285780 fading separation to 0 over
  Odyssey3D/Dynamic3DSettings/AimIpdCooldownMS = 150 ms while aiming) feeds the getters we replace. User report
  2026-10-23: hands with a weapon look mono, without one stereo. Cause (IDA): the clones are made in the producer
  loop (sub_14042579C -> sub_14206EC30, centre -/+ sep/2 via sub_1426A7B28), then a plane with an override
  (view+0x2470 + i*0x10; plane 2) goes through sub_140422D2C -> sub_140422AA4, which writes the CENTRE's position
  (+0/+8, float +0x280) and orientation into both clones (+0x3D0/+0x3D8, when +0x399 bit 0) and builds them. The
  matrix builder reads sep/conv (+0x3C0/+0x3C4) only for the off-axis P20, so the weapon plane was one viewpoint
  plus the convergence shift: the monitor's flat weapon at 0.5 m, at infinity with our parallel convergence.
  Fix: the plane detour (AsymProjection.cpp Detour_PlaneOverride) keeps the clones' positions and rebuilds them
  there after the original (EngineStereo.cpp KeepEyePlaces / RestoreEyePlaces, counter
  CyberpunkVR_DebugEngineStereoPlaneEyes). Simulator 2026-10-23: counter +1 per eye per frame, a katana and the
  hands 40-45 px more disparity (crossed) than the far sign = ~0.6-0.7 m.
- Particles at the right eye's outer edge (checkparticles.rdc, user: white angular shards in smoke there, centre
  fine). Checked and per eye: decoupled particle lighting (tile list built once with the CENTRE camera, union
  window, 56453; lit per eye into its own slice of the atlas 85772, slice = kind - 1, clamped to the eye's own
  screen, 56467-56497 left / 56506-56536 right), the particle facing twin (eye position b1 row 36), the soft-
  particle depth load (half-mapped by kind, draws are per eye). Engine bug found, not active in that frame: the
  particle PS (32916 family) samples t63, a 256x138 depth of the WHOLE side-by-side frame, at the eye-local uv
  (b0 row 31.y != 0 branch) -- the right eye reads it at u instead of (1 + u) / 2. t63 was all zeros there. The
  bright puff with a flat bottom edge at (5055, 897) is draw 59847 (9 quads, planar, eye-facing).
  Resolved with ParticleTriangles.rdc: allocator 42607 (original e4c84ae0e5616f91, staticshader_final.cache)
  requires BOTH eye Hi-Z tests to pass. Particle 109 fails in the left eye (57.09 m vs occluder 52.31 m),
  so the right eye is never tested; metadata is rejected LOD 6 / tile 0. Right-eye draw 45413 still draws
  it, and the VS decodes LOD 6 as a 128x128 tile at the atlas origin, reading other particles' lighting.
  Fix: the stereo loop allocates on the first visible eye and rejects only after both tests fail. Group 11,
  CyberpunkVR_StereoParticleLightingPatch, tools/asym_tests/stereo_particle_lighting_shaders.py. Replay:
  the three bad smoke particles (105/109/110) receive valid tiles, 49 particles recovered, none lost;
  bright blocks disappear. Vertex positions, material varyings and indices are byte-identical. Mono path,
  union-frustum test and LOD formula unchanged. Game not launched for this fix; GPU replay verified.
- Shadow-like stripes + squares near the fans (checkSHADOWbug.rdc, DLSS / clouds / contact shadows / SSR off;
  user: in BOTH eyes, they move with head turns). Eye reprojection diff (scratchpad eyediff.py: b12 81-84 / 101-104
  eye inverse, input (u W, v H, d, 1) row vector; 93-96 / 113-116 world->clip column style; round trip 0.56 px)
  shows no eye-to-eye difference there. Light-tile culling (53786, b6 COM: view + proj per eye) has the eyes'
  own lens windows (P00 0.9027, P20 +-0.2427, P21 0.1774), 32 px tiles with the right eye's columns after the
  left's; the shadow mask twin (_sm_) is bound in the lighting PS 54335. Visible: a vertical lighting edge on the
  awning under the second fan and a very blocky local-light shadow on the white NICOLA panel.
  Cornice borders resolved 2026-10-24 with checkShadowBordersOrWhatisThis.rdc and its YAWRIGHT capture:
  the lighting discontinuities land exactly at 32-pixel cell boundaries (x=2624 / 2592). The native light
  culler 37851ec24d58e673 uses per-eye projection matrices for planes, but COM row 1 (union-camera scale)
  for sphere/AABB bounds: 1.37638 horizontally vs 1/P00 = 1.10774 for either eye. Fix: derive the stereo
  bounds' scale from the same per-eye projection; mono retains COM row 1. Group 12, StereoLightCullPatch,
  tools/asym_tests/stereo_light_cull_shaders.py. GPU replacement removes the borders in both captures;
  no shadow filtering/atlas setting or SSR change. No conclusion about the separate NICOLA shadow's
  resolution from these tests. Game not launched; verified through capture replay.
- Lamp flicker with DLSS only (user: fine with DLSS off). The TAA jitter is in pixels of a size the camera
  builder turns into NDC with (sub_140C28EC0: 2 j / size, descriptor +0x370 jitter, +0x378 size); the view
  prepare hands the view's size to the centre and both eye clones (sub_140424D08). Measured live: the eye
  clones' size is 3072x1661 -- both eyes wide -- so an eye's samples move by j/2 horizontally while its DLSS was
  told j (DLSS and camera jitter ranges identical, +-0.499). Fix: the per-eye evaluate scales the jitter by eye
  input size / that size (EngineStereoDlss.cpp, CyberpunkVR_EngineStereoDlssEyeJitter, live; scale in
  CyberpunkVR_DebugEngineStereoDlssJitter[4..5]; sizes in CyberpunkVR_DebugEngineStereoEyeJitter).

FixLOD (2026-10-24): the old primary AOB silently hit plane override +422C7A on 2.5 instead of MAIN.
New stereo TEST/MOV instructions broke the contiguous patterns at MAIN +425A44 and constructor +213911.
LodFovPatch.hpp now guards the explicit 2.5 MAIN entry and the constructor / plane override / SetFov
tails (+213911 / +422C90 / +206F789). Only the LOD term is substituted; native stereo flags are preserved.
CyberpunkVR_DebugFixLodCoverage=0xF confirms all four sites. Tests execute the production tail trampoline
with copied native instructions (84 on/off cases, flags/FOV/RAX checks, no game launch needed).
Live A/B PID19720 at the objectsdissapear capture location: distant building storeys disappear with
xr_fix_lod=0 and return with 1, twice at the same camera pose. All four MAIN plane descriptors and both
eye clones read 2.437959 without the fix and 1.0 with it; rendered FOV stays 100.299553. Near position also
checked. Evidence: scratch/fixlod-fix-20261024/live/lod-before-after.png and *-ab.json.

Menus (2026-10-23): ink draws a menu once over the whole 5296-wide window, so the eye split gave both eyes the
same left half of it. With a menu open the capture now copies the window and squeezes it into the eye-sized
snapshot (ColorBlit, OpenXRCapture.cpp), both eyes get it, and the menu quad takes the window's aspect at the
menu width it had in one eye's window (OpenXRFrameLoop.cpp). Feature 98 stays on in menus (the world behind a
pause menu is still side by side); the SDK getter sub_142284DC0 has three more callers, so it is not detoured.

### HUD and world markers (2026-10-23): in progress

- 2.5 grew CameraShaderConsts to 852 bytes: the eye the pass draws (camera kind, 0 / 1 left / 2 right) at byte 848,
  uploaded as 864 (sub_14228C9A0 -> sub_140334328, 0x360). The port's 848-byte camera checks never match on 2.5;
  the marker path now takes 864 (Render/WorldMarkerProjection.hpp). Still 848: jitter fix, MonoCascades,
  NativeEarlyDepth, Grading camera lends.
- 2.5 replaced every flat ink VS (engine\shader_final.cache): plain sprite (3042), procedural (3442) and four
  stereo variants that add a per-eye offset from a material slot. The marker replacements (Render/WorldMarkers.hlsl)
  cover all six; before that VertexShadersReady() was never true on 2.5 -- no depth tags, no parallax, any mode.
- The UI pass runs once per eye, each with its own camera block, over a canvas the size of the window (5296 x 2864).
  A mappin's position (MappinPosition's argument) is in the HUD's 3840 x 2160 design units, 901 per tangent: the
  centred frustum with the eyes' vertical span at 16:9, which the HUD layer scales by 1.326 into the canvas -- a
  canvas pixel is an eye pixel. Per eye the canvas camera gets squeeze 2 and the eye's window shift, and +-IPD/2
  parallax (WorldMarkers.cpp InkShift / EngineEyeParallax); measured: predicted x 2741 = 2741.0.
- The HUD layer fits its design into the two-eyes root by the height (1.326 per unit, one eye: 0.690), 1.92x the
  usual size. The HUD panel puts each window on one eye's root with one scale (0.52) about the centre
  (Natives/HudPanel.cpp ToEyeRoot); keeping distances from the root's edges put it 690 px too high. Mappins get the
  same ratio on their 0.7 shrink.
- The stereo ink variants add the engine's own per-eye offset after the camera (material slot, +x in the left eye,
  -x in the right): the 3D monitor's disparity. On a quest marker 46 m away it was -0.083 NDC per eye -- the
  marker 110 px outside its car in each eye, uncrossed past infinity -- unchanged with the port's parallax
  (`CyberpunkVR_WorldMarkerStereo` 0) or its canvas move (`CyberpunkVR_AsymHudShift` 0) switched off live. The
  replacement shaders now leave it out for anything carrying the port's depth tag (vertex z or glyph matrix):
  those are at their point's depth already. Untagged HUD keeps it (its widget depth, ~2.8 m in the headset).
  Simulator, same save: the marker (icon + distance) on its car in both eyes, the rest of the HUD unmoved.
  The other seven 2.5 ink VS are not replaced: five are world-space (camera rows 26/28-31), one is a clipped flat
  sprite (73869067, SV_ClipDistance), one a procedural shape with no eye read (245b66be).
- Open: on one screenshot a small cyan dot (crosshair or mappin) shows in the left eye at its predicted place and
  not in the right one -- needs a capture of the HUD target's two halves. Main-thread DEP at 0x1 after ~8-9 min
  in this mode, twice, stack above RSP holds minidump-like records, no return address -- unattributed.

- Per-eye asymmetric FOV: the matrix builder sub_140296EA0 applies the camera's own NDC shift (+0x30/+0x34)
  AFTER the stereo branch, and the eye cameras carry their kind at +0x398 (1 left, 2 right). So a detour on it
  that, for kind 1/2, writes the eye's own window (fov / aspect / shift, scaled to the plane's own span like
  CascadeUnion) before the build gives each eye its lens window; the centre keeps the union for culling and
  cascades. Removes the 20.9 % vertical growth.
- DLSS per eye (two evaluates on sub-rects; UpscalerViews contexts), FG inputs per eye.
  DONE 2026-10-23 (Stereo/EngineStereoDlss.cpp, CyberpunkVR_EngineStereoDlssPerEye live): one Streamline viewport
  per eye, tags cut to the eye's half, mvecScale.x doubled, options at half the output width. DLSS (sl.dlss 2.14,
  which does pass DLSS.Output.Subrect.Base) reads inputs at an offset but writes NOTHING at an output offset: the
  right eye aimed at x = 2648 of the 5296 target left that half black while every call returned eOk; aimed at the
  left half (diagnostic mode 2) it drew the right eye. So the right eye upscales into its own 2648x2864 target (the
  engine target's format, here 26 = R11G11B10) and is copied into its half on the evaluate's command list
  (UAV -> COPY_DEST / COPY_SOURCE and back). Per eye vs one evaluate over both: 0.8 mean difference, frame noise.
  Diagnostics: CyberpunkVR_EngineStereoDlssMode (1 order, 2 right eye into the left half, 4 left only, 8 local
  tags, 16 right eye straight into its half, 32 right eye inputs read at an offset), CyberpunkVR_DebugEngineStereoDlssResults.
  The right eye then shimmered and swam (user): DLSS does not take motion / depth at an input offset either. Now
  the right eye's inputs are copied too (colour 26, motion 34, bias 61 directly; depth R32_TYPELESS + DS flag
  cannot be copied in part, so whole into a staging copy, then its half -> R32_FLOAT) and read at (0, 0).
  Head-turn bursts (motion_burst.py, 48 frames, 20 deg/s), right / left Laplacian variance: one evaluate 1.07,
  copies 1.09, inputs at an offset 0.90 (the right eye smeared).
- HUD: engine per-eye HUD vs the port's HUD quad; weapon / aim comfort cvars off.
- Ray tracing with stereo (Prepare/MergeStereoRayTracing) — capture with RT on.

## M3 — shipping

- Gate without the registry (in-process wrapper or the port's installer writing the key), the cvar ini in the
  package, patchday guards for the new RVAs (`sub_14226F600`, `sub_14226F380`, `sub_142284DC0`, ...).
