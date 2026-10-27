# Screen effects without picture distortion, 2026-10-18

Request: the screen effects (healing, Sandevistan, Berserk and the rest) distort and tear the picture,
which breaks the view in the headset. Follow-up: keep each effect's own look, only take out what breaks VR.

Each eye draws these effects as its own full-screen pass. Under the asymmetric eye windows the middle of
each eye's screen is a different world direction, so anything that MOVES scene pixels around the screen
middle lands differently in the two eyes: zoom and radial blur, tearing offsets, refraction, spherical warp,
chromatic aberration. Colour does not: tints, overlays, glitch lines and artifacts, grids, vignettes,
exposure and grain look the same in both eyes. The fix keeps every effect drawing and sets only its
displacement to the neutral value.

## Assets (cyberpunkvrport.archive)

Tools: `build/distortion-fx-20261018/`. The pipeline:
`stage.py` -> WolvenKit cr2w_to_json -> `edit_distort.py` -> json_to_cr2w -> `assemble.py` -> pack_archive.

- 633 player, camera, perk, cyberware and quest on-screen `.effect`, `.particle` and `.mi` files from base
  and EP1 (the EP1 copy wins where both exist). 372 files changed.
- What draws is the emitter's `localMaterialInstance`. `material` is used only when there is none:
  194 emitters name particles_generic in `material` and draw screen_glitch.
- A `.effect` plays the particle systems embedded in it (EmbeddedFiles), so those are edited like the
  `.particle` files.

The neutral parameters, per shader template:

| template | set to neutral |
|---|---|
| zoom.mt | MotionIntensity 0, BlurAmount 0, AberationAmount 0 |
| reflex_buster.mt (incl. p_damage_pain.mi) | Max/MinDistortMulitiplier 0, ZoomMultiplier 1, BlurAmount 0, AberationAmount 0 |
| radial_blur.mt | AberationAmount 0, BlurAmount 0 |
| screen_glitch(_notxaa, _vision), ep1 weapon_screen_glitch (incl. p_damage_glitch*.mi) | Offset 0, GlitchDepth 0, BlurredIntensity 0 |
| screen_fast_travel_glitch, screen_artifacts(_vision) | Disturbance 0, BendScreen 0 |
| damage_indicator.mt | RefractionOffsetStrength 0, DoubleDistortWithNoise 0 |
| _shaders\screen_wave.mt | DistortAmount 0 |
| noctovision_mode.mt | DistortionOffset 0, DistortionSpeed 0 |
| cybermask_fullscreen_v2.mt | ScreenSphericalDistortion 0, HexWarpIntensity 0 |
| _shaders\low_health.mt | MaximumDistortion 0, MaximumSliding 0, OffsetAmount 0 |
| ep1 blackwall_onscreen(_assembly).mt | SphericalDistortionPower 0, MotionJitterIntensity 0 |
| refraction shaders, only when drawn on the screen (CParticleDrawerScreen) | their refraction strength 0 |

Effect track items removed: ChromaticAberration, MotionBlurScale, FOV (zoom kicks) and DataMosh (tearing).

## Sandevistan (engine shader)

The Sandevistan has no screen asset. Its look is the GameplayPostFX node's static technique
`m_postfx_CPSandevistan` in `engine\staticshader_final.cache`.

How the cache is laid out: each entry of its technique table ENDS with the technique's name. The entry's
stage keys sit after the previous entry's name: a u32, then 8-byte keys, VS in slot 0 and PS in slot 1.
The records come after the table: key, u32 size, and the container as it is handed to D3D12.

What the pixel shader (FNV-1a `52f74d75736ab8b0`) does:

- 20 samples at `uv + offset + k_i * d * |d|^2`, with `k_i = -0.0325 * I * i` and `d = uv - 0.5`. This is a
  barrel smear from the screen middle that grows towards the edges.
- Random blocks get an offset: the tearing.
- The look: a teal tint, a cyan grid on moving things (motion vectors, dark areas only), brightening with a
  vignette, and a saturation boost.

The twin (`tools/asym_tests/screen_fx_shaders.py`, output `src/Shaders/screen_fx/`) sets the barrel step and
both block offsets to 0. All 20 samples then read the pixel itself, and the rest of the shader is unchanged.
The plugin loads it from `screen_fx\` as fixed group 7, with the switch `CyberpunkVR_ScreenFxPatch`
(Render/HiddenAreaShaders.cpp).

## Measurements

Tools: `build/monowire-housing-20261016/probe_distort.py` (effects started by name, head still) and
`analyze_distort.py`. The analysis compares the left eye with the baseline in 64 px tiles using phase
correlation:
- `moved`: share of tiles shifted by at least 1.5 px.
- `peak`: correlation peak. The baseline is about 0.9.
- `look`: mean colour change, i.e. whether the effect still draws.

| effect | before: moved / peak / look | after: moved / peak / look |
|---|---|---|
| perk_edgerunner | 84 % (8 px) / 0.09 / 51 | 8 % (0-1 px) / 0.35 / 46 |
| reflex_buster | 37 % / 0.10 / 82 | 7 % / 0.31 / 52 |
| berserk | 24 % / 0.23 / 24 | 10 % / 0.35 / 24 |
| perk_overclock | 14 % / 0.42 / 29 | 6 % / 0.50 / 28 |
| sandevistan (twin) | 28 % / 0.16 / sharpness 0.37 | 7 % / 0.73 / sharpness 0.82, same mean colour (41/70/72) |

Healing (`splinter_buff`) never distorted: it is a cyan tint (peak 0.78, nothing moved).

Open: the Cyberspace postFX (`m_postfx_CPCyberspaceDataMoshing` / `Pixelsort`, quest scenes only) still tears.
