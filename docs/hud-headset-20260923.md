# Physical headset: duplicate HUD layers

The user switched from OpenXR Simulator to a physical headset and saw two
horizontally separated HUD copies even with one eye closed. This was monocular
duplication, not merely a binocular convergence mismatch.

## Current launch and evidence

PID28152, started2026-09-23 16:01:46. The game log identifies VirtualDesktopXR
1.0.10. Installed plugin SHA256 remains
`2a516851e86db6f0522decf623406a0ef890a291525196c2f7bd7746a3d42c96`.
Read-only symbols came from the matching DLL/PDB snapshot in
`build/world-markers-re/accepted-2a516851/`, not the newer default-shadow build.

Before the change, the installed INI had `xr_hud_stereo_depth=0`. HudQuad.cpp
therefore submitted LEFT-only and RIGHT-only quads at distinct eye-offset poses.
`build/world-markers-re/hud-28152-two-layers.json` confirms two HUD layers,
valid tracking and zero translation-cancellation error. Native source inspection
reported21 panel entries; the minimap and quest slots were hidden with opacity1.
They were not additional visible native copies of those elements.

Only `xr_hud_stereo_depth` changed from0 to1 using the existing INI hot reload.
The selected distance remains1.5m. This mode submits one shared BOTH-eye quad.
`hud-28152-one-layer.json` confirms one HUD layer with valid tracking. The user
then confirmed that the duplicate HUD disappeared in the physical headset.

Keep the installed `xr_hud_stereo_depth=1`. The INI backup before this change is
`build/world-markers-re/hud-28152-vrport-before.ini`. No DLL deployment, game
restart, runtime modification or pose command was needed. Local-shadow reuse
remains0. The source's optional angular mode and its defaults are unchanged.

## Interpretation and limits

The split into per-eye quad layers did not produce the intended visible result
on this headset route. A single shared quad is verified here. The prior simulator
acceptance only covered its own compositor and does not validate this runtime's
handling of the two per-eye layers.

Do not infer that every VirtualDesktopXR version ignores eyeVisibility.
[Current upstream quad handling](https://github.com/mbucchia/VirtualDesktop-OpenXR/blob/main/virtualdesktop-openxr/frame.cpp)
maps it to DisableLeft/DisableRight on one backend and logs it as unimplemented
on its Oculus backend. The exact branch and underlying compositor behavior of
this installed1.0.10 session were not established. The live single-layer result
does not require that assumption.
