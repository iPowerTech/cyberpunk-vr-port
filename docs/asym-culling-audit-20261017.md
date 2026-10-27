# Culling and LOD under the asymmetric projection (audit, 2026-10-17)

Question: do frustum culling, occlusion, light culling and LOD follow each eye's off-centre window, or
does any of them still assume the symmetric frustum? Answer: they follow the window. Nothing to change.

## Why it is right by construction

The port does not fake the off-centre frustum after the fact. `Stereo/AsymProjection.cpp` writes the
window (vertical fov, aspect, NDC shift) into each eye view's camera INPUT block (view+0x1E40) on the way
into the view producer `sub_1404E4AFC`, so the engine builds all four camera descriptors, their matrices,
the jittered rebuild and the history from it. Everything downstream that asks the descriptor for its
projection gets the shifted one: the visibility query (`DoCulling` 0xB2BEFC -> `GatherCtxInit` 0x623FD8
reads the descriptor at view+0x70), the software occlusion raster of both views (VRCAM's own via
`CyberpunkVR_OcclusionGateForce`), the clustered light lists, and the cascade fit (whose union is the only
place the port deliberately widens the frustum, `CyberpunkVR_AsymCascadeUnion`).

LOD: the detail threshold is `max(tan^2(fov/2) * 1.6984, 1)` at the descriptor builder (exe+0x4E4269);
`LodFov.cpp` clamps any fov above 75 deg to the floor, so both eyes get the engine's flat-screen threshold
1.0 whatever window they have. The screen-size side uses the descriptor's vertical fov, which the window
code sets so that tan(vfov/2) = (u - d)/2 -- the pixel density per tangent the eye really has. The shift
does not enter either term.

## Measured (simulator, garage save, bind 4)

Mode 2 (`CyberpunkVR_AsymTest`) shifts one eye by 0.6 NDC at the engine's own fov, so the strip
NDC [-1, -0.4] (or [0.4, 1]) shows directions OUTSIDE the unshifted frustum (|tan| 1.38..2.2, up to 65 deg
off-axis, against 54 deg for the unshifted edge). The other eye is left unshifted with aspect x1.8, a
symmetric frustum that covers the strip, and is resampled onto the shifted eye through tangent
coordinates (`probe_asymcull2.py` / `probe_asymcull3.py`, analysis in the job tmp `cullcmp*.py`).

| shifted eye, scene | luminance ratio, strip bands | ratio, inside bands |
| --- | --- | --- |
| MAIN +0.6, yaw 0 | 0.98 0.96 1.01 | 0.82..1.06 |
| MAIN -0.6, yaw 0 | 1.05 1.06 | 0.79..1.06 |
| MAIN +0.6, yaw 1.2 | 0.94 0.98 | 0.84..1.05 |
| VRCAM +0.6, yaw 0 | 0.95 0.96 0.92 | 0.78..1.08 |
| VRCAM -0.6, yaw 1.2 | 1.02 0.98 | 0.84..1.15 |

The strip agrees with the reference as well as the inside does; the per-pixel difference maps show only
parallax of near objects (hands, pipe) and lamp glow. Small objects that exist only in the strip -- an
electrical box and a pipe 62 deg off-axis -- are drawn and lit. A cull, occlusion or light list built on the
unshifted frustum would have left that strip empty or unlit with an edge at the red line of the maps.

One difference is a test artefact, not culling: a volumetric light band beside the neon sign appears only
in the x1.8-aspect reference eye, whichever eye that is; mode 0 with the sign in the centre of a normal
square-pixel frustum (`probe_asymcull4.py`, yaw 1.8) shows no band either.

Not covered: the RT-inflated cull with ray tracing on (its frustum is inflated beyond the view anyway).
