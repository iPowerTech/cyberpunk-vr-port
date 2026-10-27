# Hidden-area mask (2026-10-09)

Goal: stop shading the parts of each eye's render that the lens never shows (Valve's hidden-area
mesh), without changing what is visible. The user chose a user-sized mask (no runtime
`XR_KHR_visibility_mask`, no face-gasket term).

## Result

City roof, same session and scene, `view_node_profile.py` (build/single-pass-20261008/ham-860):

| radius | hidden pixels | GPU frame (median) | SSR | light integration |
|---|---|---|---|---|
| off | 0% | 11.99 ms | 1.18 ms | 0.74 ms |
| 100% | 18.6% | 11.61 ms | 0.97 ms | 0.61 ms |
| 85% | about 40% | 11.11 ms | 0.76 ms | 0.48 ms |
| 70% | 59.2% | 11.03 ms | 0.46 ms | 0.37 ms |

How much is hidden decides the gain. At 100% (the ellipse touches the panel edges) only the corners
go, and they are cheap pixels: sky on top, floor and body below. Another session at 100% measured
no frame change at all (ham-856), so the 100% figure moves with the scene. Which radius is really
invisible on a Pico 4 can only be judged in the headset.

## Mechanism

- `src/Render/HiddenArea.cpp`, `src/Render/HiddenAreaSettings.cpp`, `include/Render/HiddenArea.hpp`.
- Visible area: an ellipse in the eye's tangent space, normalised per quadrant to the runtime eye FOV
  (100% touches the panel edges, about 141% masks nothing inside the panel). Everything outside the
  runtime FOV rectangle is hidden as well (matters for canted headsets, none on the Pico 4 preset).
- Settings: `xr_hidden_area=0/1`, `xr_hidden_area_radius=70..145` (default 115), overlay
  General > HIDDEN AREA MASK (checkbox, "Visible radius" slider, masked share per view). Skipped in
  menus. Off by default.

### Near plane while geometry is drawn

Right after the engine's native scene-depth clear, a second `ClearDepthStencilView` restricted to
rectangles writes the reverse-Z near plane (1.0) into the hidden cells (8 px cells, one cell of
margin), so no geometry lands there. Only the clear in `SetRenderTargetsGBuffer` (node 0x20C3AC:
1485x1485 D32S8, depth+stencil, value 0) is masked, both views (`CyberpunkVR_HiddenAreaNode`,
default 0x20C3AC). A live log of all native depth clears (`CyberpunkVR_HiddenAreaLog=1`,
`CyberpunkVR_HiddenAreaClearLog`) showed the other candidates, which must NOT be masked:
`SetHologramDepthBuffer` 0x7728E8 (1485x1485 D32), `ScreenSpacePlanarReflections` 0x78A6B8
(372x372 D32), `RenderScreenSpaceWaterDepth` 0x378AD8 (256x255 D32) and the D16 shadow maps.

The engine clears COPIES of its depth-stencil descriptors, so the descriptor -> target map follows
`CreateDepthStencilView` and DSV-heap `CopyDescriptors` (the map in Hooks/DepthCapture.cpp only
knows created descriptors and never matched a clear).

This alone saves nothing: the dominant G-buffer material PSOs (about 1,730 draws,
docs/gpu-capture-audit-20260930.md) `discard` and export `SV_Depth`, so their pixel shaders still run
and are rejected afterwards, and the passes after the G-buffer do not test depth that way.

### Far plane while the scene is read (`CyberpunkVR_HiddenAreaSky`, default 2)

The scene depth leaves and re-enters DEPTH_WRITE six times per view and frame (barrier log,
`CyberpunkVR_HiddenAreaBarrierLog`): it enters in SetRenderTargetsGBuffer x3,
SetRenderTargetsGBufferWithVelocityBuffer 0x20B294, SetRenderTargetsMain 0x775ACC and
RenderBackground 0x782EB8; it leaves in ScreenSpaceReflections, Synchronize 0x58DA9C and four times
between nodes (no view is current there; the view is taken from the clear that registered the
resource).

- Before every barrier that ends depth writes, the hidden cells are cleared to the far plane (0):
  every reader then sees sky there, and the engine's own sky tests skip them. This is where the
  gain above comes from (SSR, light integration, shadow mask). It also keeps screen-space rays and
  AO samples that cross the hidden area from hitting a wall at the near plane.
- After a barrier into DEPTH_WRITE in the two G-buffer nodes, the cells go back to the near plane.
  Forward and sky phases keep the far plane, so the sky fills the hidden cells and no stale colour
  stays there for bloom to spread.
- The clear needs its own DSV per recording thread (a barrier names a resource, not a descriptor),
  created right before it.

### NVIDIA driver limit: at most 130 rectangles per clear

One `ClearDepthStencilView` with more than 130 rectangles terminates the process with
`0xC0000409` inside the driver (RTX 5070 Ti, driver 596.49): no crash report, no WER event, no DRED
line. The game died twice that way (radius 80: 177 rectangles, radius 100: 205); 127 ran for
minutes. `tools/hidden_area_tests/gpu.cpp` reproduces it standalone (`hidden_area_gpu single N`:
130 passes, 131 fails) and checks the shipped path (`batched`, 64 per call) pixel-exactly: 411,081
of 2,205,225 pixels masked (18.6%), 0 wrong.

### Hidden share (Pico 4 preset, 1485x1485 DLSS input, 104 deg)

| radius | 70 | 80 | 90 | 100 | 110 | 115 | 120 | 130 | 140 |
|---|---|---|---|---|---|---|---|---|---|
| hidden | 59.2% | 47.1% | 33.4% | 18.6% | 9.5% | 6.3% | 3.9% | 0.9% | 0% |
| rectangles | 155 | 177 | 197 | 205 | 152 | 127 | 100 | 47 | 0 |

## Shader patches

For the passes that do not test depth for sky, the shaders themselves have to skip the hidden
pixels. Two recipes are shipped (ConeAO and the fog overlay, see "Patched shaders" below); the
gain is small.

- Census (`src/Render/HiddenAreaShaders.cpp`): with `cvr_shader_census.txt` next to the game
  executable at startup, the plugin keeps every compute and pixel shader it sees created;
  `CyberpunkVR_ShaderCensus=1` then logs node, view, kind, dispatch size and shader per command and
  dumps each shader once to `bin/x64/cvr_shaders/<FNV-1a>.dxil`. Run 857 dumped 145 shaders of the
  screen-space nodes (build/single-pass-20261008/ham-857/census.json and shaders/). Remove the
  marker afterwards: it keeps all shader bytecode in memory.
- Generator (`tools/hidden_area_tests/ham_shaders.py`): one recipe per shader edits the dxc
  disassembly, inserts the shared visibility test, assembles and validates with
  `build/single-pass-tests/Release/dxil_tool.exe`, and writes `<FNV-1a>.dxil` plus a trailer with
  the bit offsets of four placeholder constants.
- Runtime: the plugin loads `red4ext/plugins/CyberpunkVR_Stereo/hidden_area/*.dxil`; a pipeline
  created with a patched shader is registered with a function that rebuilds it from the engine's own
  description, and `SetPipelineState` binds the twin while the mask is on
  (`CyberpunkVR_HiddenAreaPatch`, on by default). The twin's placeholders are rewritten in place
  for the view's ellipse (`src/Render/DxilPatch.cpp`): a float in [1, 2) is always six VBR6 chunks
  in the bitcode, so the bits are overwritten and the container hash (DXBC MD5 variant, checked on
  all 145 dumped shaders) is recomputed. A view's ellipse is republished only after it held for 30
  frames, so the slider does not build a twin per step (verified: moving the radius from 100% to
  85% rebuilt the twins). The engine frees its pipeline descriptions after creation, so compute and
  graphics descriptions, classic or stream, are copied with everything they point at
  (`OwnPipelineStream`/`OwnGraphicsDesc` in src/Render/ViewInstancedPipeline.cpp).
- Shared test, NDC with y down: `visible = a*x*x + b*y*y <= 1`, `a` and `b` per quadrant from the eye
  FOV, the render frustum and the radius plus 2% margin.

SSR's tile classifier (0x78cc00b0064fdcc3, 47x47 groups, one coarse ray per 4x4 pixels; tiles
without a hit go to a clear list instead of the ray march) was patched first. With a wrong NDC
mapping that skipped about 80% of the screen, SSR fell from 1.08 to 0.18 ms, which proved the
mechanism. With the right mapping it saved nothing beyond what the far-plane reset already does, at
100% and at 70%, so the recipe was removed.

Passes that still spend time on the hidden area (off vs 70%, ham-860): ApplyBloomAndTonemapping
0.40 ms (no change), CompositionPostProcess 0.40 (-0.06), DrawConeAO 0.39 (-0.05),
VolumetricFog 0.66 (-0.09), RenderShadowmask 0.19 (-0.07), ComputeShadingRateImage 0.19 (none),
ApplyContrastAdaptiveSharpening 0.13 (-0.02). DLSS (3.2 ms) cannot use a mask.

### Per-pipeline cost (both eyes, mask at 100%, ham-861)

`node_pso_profile.py` joined with the census (which records each command's pipeline):

| shader | pass | ms | notes |
|---|---|---|---|
| b0f3224a0a7fb8ad | RenderLightsIntegrate PS | 0.42 | already shrinks with the radius (sky) |
| d1a868349a06a24c | VolumetricFog 6x6 dispatch | 0.33 | froxel volume, not screen pixels |
| 87e161f9ebf460e9 | CompositionPostProcess 20x20 | 0.21 | tile classifier on atomics; a skipped tile would leave its output unwritten, about 0.04 ms to gain |
| 34d5c22bdeded25c | DrawConeAO cone trace | 0.20 | no sky early-out: patched |
| b8eda65631ccbf3c | ComputeShadingRateImage | 0.18 | FidelityFX VRS with group reductions, 0.03-0.07 ms to gain: not patched |
| ec08e969bf38010f | final tonemap PS | 0.18 | patched, no measurable saving: removed |
| 4cd91d4bbe85c03c | CAS | 0.13 | patched, 0.02-0.03 ms SLOWER: removed |
| 0f7d6dab2e706d58 | RenderShadowmask PS | 0.11 | has a depth early-out already |
| cfb4cd58a4c45e19 | RenderFogOverlay PS | 0.08 | patched (discard, it blends) |
| 252cff2238558b8a, 4e24cc93af8c79ff | Present (MAIN only) | 0.21 + 0.18 | draw the backbuffer the left eye is copied from; not DXIL-disassemblable |

### Patched shaders (shipped)

- DrawConeAO cone trace: hidden pixels write no occlusion and a straight bent normal (AO = 1), so
  what a later blur pulls across the edge can only lighten. -0.047 ms at 100%, -0.064 ms at 85%.
- RenderFogOverlay: hidden pixels discard (the pass blends over the scene). About 0 at 100%,
  -0.019 ms at 85%.

Visual check (ham-862): with the patches, the only differences inside the visible ellipse are the
animated hands and the debug overlay's numbers.

### Desktop Present

Skipping the DXGI Present to the monitor (and the desktop overlay) was measured on request:
FPS fell from about 83 to 65 and GPU use from about 93% to 72% (the frame is paced by that Present),
with unchanged node times. Not kept. The Present node's GPU work is the backbuffer the left eye is
captured from, so it cannot be dropped either.

## Tests

`tools/hidden_area_tests` (`cmake -S tools/hidden_area_tests -B build/hidden-area-tests -A x64`,
build, `ctest -C Release`):

- `hidden_area_settings`: settings round trip, `Hidden`, rectangle exactness/no-overlap/bounds
  across the whole slider at the live size.
- `hidden_area_gpu batched`: rectangle clears on the real adapter, pixel-exact readback.
- `hidden_area_dxil_patch`: compiles a compute shader with two placeholders (Windows SDK dxcompiler),
  checks the container hash, rewrites both constants, re-signs, runs it on the device
  (out = 2 * 1.5 + 1.75 = 4.75) and checks that the device refuses the rewritten container with the
  old hash.
