# Steam Frame, Reverb G2, and performance defaults

## Defaults

- `CyberpunkVR_LocalShadowReuseMode` already initializes to 0. Keep it at 0,
  and write `xr_local_shadow_reuse=0` explicitly into newly created vrport.ini.
- Reflex now starts Off (0), including the missing-key fallback, overlay state,
  new vrport.ini, and the shipped UserSettings.json value/index. Previously the
  code followed the game (-1) and the preset selected Enabled (1).
- Explicit saved Reflex modes (-1/0/1/2) are still honored. The hook only changes
  the latency mode while an XR session runs; native limits/markers stay intact.
- The installed runtime configuration already contains both keys at 0.
  Existing runtime settings and first_launch=0 are not rewritten or reset.

## Headsets

Append Steam Frame as hmd_type=13 and HP Reverb G2 as hmd_type=14, preserving
all existing headset IDs. Reverb G2 references the exact Pico 4 resolution list,
including order and labels, as requested. Steam Frame uses the fork's square
2160, 1920, 2048, 2560, 3072, 4096, 5000, and 6000 px choices.

All these sizes already have authored VRCAM components and textures. The asset
generator's dry run reports 60 unique launcher sizes and zero new components or
textures for the four V player entities and three story replacers. No archive
or projection/FOV changes are needed for these presets.

## Steam Frame Input

Adapted from Crazymoniker's
[87c14877](https://github.com/Crazymoniker/cyberpunk-vr-port-frame/commit/87c14877552ac36286aad73b3adef7e30aeddf3b).
All 24 native binding paths and action types were checked against
[Valve's input documentation](https://partner.steamgames.com/doc/steamhardware/steamframe/input).

Enable `XR_VALVE_frame_controller_interaction` only when advertised, with profile
`/interaction_profiles/valve/frame_controller_valve`. Preserve the existing Touch
fallback and pose-only mode. Do not select a controller profile from the launcher's
headset ID: the active runtime and connected controllers decide it.

Right A/B/X/Y, left D-pad, left View, right Menu, and independent left/right bumpers
reach the corresponding XInput bits. Sticks, triggers, grips, grip poses, and aim
poses use the existing pipeline. Physical bumpers do not repurpose squeeze/grip
gestures. New actions are cleared with the action set during shutdown.

Unlike a direct copy of the older fork, extra physical buttons are merged before
the current F10 input-capture gate, so they cannot bypass the overlay. Menu/Start
keeps the existing escape behavior. Other runtime, rendering, and FOV behavior is
unchanged; no eye-tracking/foveation feature is implied by this controller port.

## Verification

- Native launcher test: both new entries, preserved IDs, G2/Pico list equality,
  Steam Frame labels, and save/reopen for both headsets. Existing window/input,
  closure, failure, and reentry checks still pass on an unshown test desktop.
- Frame input test: action names/types/scopes, all 128 combinations of additional
  global buttons, inactive/null actions, and cleared handles after shutdown.
- Reflex ABI/default test and settings merge/preset test pass.
- QuickBoot (1), VR overlay (16), framegen (17), analog input (7), story attention
  (4), and render parity (5) regressions pass. Total with the four tests above: 54.
- Source binding audit: 24 distinct paths, all matching Valve's documented types.
- `git diff --check` passes.

Steam Frame and Reverb G2 hardware are not available locally. These are compiled
implementation and automated checks, not an on-device confirmation.

## Deployment

Release build succeeded. Installed DLL, matching PDB, and the updated shipped
preset while the game was closed. Verified hashes of vrport.ini, launcher config,
the player's UserSettings.json, and calibration are unchanged. No game launch
or first-launch reset was performed.

DLL SHA-256:
`75D3C12786C26585EBF70C5DF7600D947AE40B74BDDDAE5F5CFD7EB810C175E5`.
Backup and receipt: `build/headsets-defaults-20260928/deploy-195826/`.
