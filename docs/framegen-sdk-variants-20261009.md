# Frame generation variants: native DLSS-G, FidelityFX without optical flow (2026-10-09)

Request: improve frame generation as far as possible and add variants -- native DLSS Frame Generation
(as in elliotttate's OFXR fork, branch native-dlss-fg) and the latest OFXR (djules75) ideas, done
natively in the game with the engine's depth and motion vectors, aiming for quality, low latency, low
VRAM and low generation time. Steering during the work: use the vendors' SDKs, which guarantee their
result; do not build what the engine's vectors cannot support.

## What ships

| Setting (`xr_framegen_backend`) | Pipeline |
|---|---|
| 0 FidelityFX | FidelityFX Frame Interpolation (SDK 1.1.4). `xr_framegen_flow_scale=0` runs it on the engine's vectors alone; 50/75/100 adds FidelityFX optical flow at that resolution |
| 1 NVIDIA OFA + FidelityFX | NVIDIA optical flow (OFA) + FidelityFX Frame Interpolation; with flow 0 it is the FidelityFX one |
| 2 NVIDIA DLSS-G | NVIDIA DLSS Frame Generation through NGX (DLSS SDK 310.9.1, the game's nvngx_dlssg.dll 310.1), no Streamline, no swapchain |

New defaults (fresh settings only; an existing vrport.ini keeps its values): DLSS-G, optical flow off.
DLSS-G falls back to FidelityFX where NGX reports frame generation unavailable (not RTX 40/50, old
driver, SDK not in the build), and the UI says why.

The FidelityFX version question: the port uses FidelityFX SDK 1.1.4 (statically linked Optical Flow +
Frame Interpolation). OFXR uses the same 1.1.4, but only its Optical Flow, composing frames with its own
shader. The game ships FidelityFX API 1.0.1 (older). Newer is SDK 2.x, a DLL-only FidelityFX API whose
ML frame generation needs an RDNA 4 card.

## Native DLSS-G (src/Framegen/DlssFrameGen.cpp)

- NGX initialised once per device with NVSDK_NGX_D3D12_Init_with_ProjectID (custom engine), feature
  search paths = the game's bin\x64 (nvngx_dlssg.dll) and the plugin folder. Never shut down: NGX is one
  instance per adapter per process, shared with the game's own DLSS (NVSDK_NGX_D3D12_Shutdown1 would
  take those features down too). Coexists with Streamline's NGX: verified live.
- Capability check: FrameGeneration.Available (this RTX 5070 Ti: available, MultiFrameCountMax 3).
- One feature per eye (NGX_D3D12_CREATE_DLSSG: display size, render size = depth size, RGBA8 UNORM).
  The second-eye capture is R8G8B8A8_TYPELESS, so that eye's colour is copied into a typed texture
  first; the other eye's capture is used directly.
- Every real frame is evaluated, resets included (NGX keeps the previous backbuffer itself, so there is
  no history copy on our side). Inputs: the eye's final colour, the engine depth and motion vectors
  copies (NON_PIXEL_SHADER_RESOURCE), output RGBA8 UAV. mvecScale = motionScale * motion-grid size
  (the raw vector times motionScale is a fraction of the image; NGX wants pixels of the motion grid),
  cameraMotionIncluded, depthInverted, camera position/axes/near/far/FOV/aspect from the engine view.
- Matrices are built from the engine camera (row vectors, reversed Z; clip->view->world->previous
  view->previous clip). Replay with identity clip-to-previous-clip instead: no measurable difference.
- Output is the midpoint camera; submitted with the slerped pose like the FidelityFX path.
- NGX estimate: 421 MiB per eye at 2560x2560 (render 1707x1707). NGX writes nvngx.log/nvsdk_ngx.log
  into the plugin folder.

## FidelityFX without optical flow

Replaying real captures (below) through FidelityFX FI with the flow forced to zero gave byte-identical
results to FidelityFX optical flow and to NVIDIA OFA on every capture: the engine's vectors already
describe the motion, and FI never took anything from the flow. `xr_framegen_flow_scale=0` now skips the
pack pass, the optical-flow context and its textures; FI reads an all-zero flow and scene-change buffer
(cleared by the existing Flow pass) and the whole pair is one command list. Optical flow stays
available for content without vectors (some particles/effects).

## Measurements (live, RTX 5070 Ti, both eyes 2560x2560, FSR2 Quality 1707x1707, 45 -> 90 fps)

Median of 40 GPU-timestamp samples per variant, two alternating rounds (build/framegen-native/fg-874).
VRAM is the framegen total including the engine depth/vector input copies (~134 MiB of it).

| Variant | Generation (ms GPU) | VRAM |
|---|---|---|
| FidelityFX FI, optical flow off | 1.33-1.49 (p10-p90 1.32-1.62) | 481-503 MiB |
| FidelityFX OF + FI | 2.0-4.0 (two submissions; the span includes the game's work queued between them) | 506-528 MiB |
| NVIDIA OFA + FidelityFX | 4.2 (includes waiting for the OFA engine) | 507-529 MiB |
| NVIDIA DLSS-G | 3.95-4.14 | 1051-1073 MiB |

All variants: REAL 45 / FG 45 / OUT 90, no skips.

## Quality: capture and replay

`CyberpunkVR_FramegenCapture` (export, set to 1) writes the next three consecutive real frames --
both eyes' colour, engine depth and vectors, engine camera, eye pose and FOV -- to
`bin\x64\framegen-capture\<time>\` from a background thread (src/Framegen/FrameCapture.cpp).
`tools/framegen_tests/capture_sweep.py` moves the OpenXR Simulator head and arms it.
`framegen_tests replay <capture> <out>` generates the midpoint of frames 0 and 2 with every variant
(frame 2's vectors composed with frame 1's to reach frame 0), rotates each output from its submitted
pose into frame 1's (as the runtime does) and compares with frame 1, the truth. The gap is two real
frames, so every method sees twice its normal motion. PNG crops and error maps go to `<out>`.

| Capture (build/framegen-native/captures) | repeat frame (runtime only) | FidelityFX (any flow) | DLSS-G |
|---|---|---|---|
| 144932 yaw, PSNR all / moving region, eye 0 | 23.4 / 13.7 dB | 29.9 / 21.7 dB | 30.1 / 22.2 dB |
| 144932 eye 1 | 21.7 / 14.7 | 27.7 / 22.2 | 28.1 / 22.9 |
| 144934 strafe, eye 0 | 27.1 / 19.2 | 28.6 / 22.5 | 29.5 / 22.9 |
| 144934 eye 1 | 27.6 / 19.8 | 28.9 / 22.9 | 29.8 / 23.4 |

(Capture 144927 is not used: the simulator moved unevenly, frame 1 is nearly frame 0.)

## Tried and removed: OFXR's game-motion synthesis

OFXR's "+ DLSS vectors" composition (frame_synthesis.hlsl, LGPL) was ported as an "Engine vectors"
backend: a fixed-point solve of the engine's vectors in the current eye camera with head rotation
removed through the eye poses, plus engine depth for occlusion, adaptive occlusion probes and depth
dilation of the vectors. It was the cheapest (0.85 ms, 245 MiB) and exact on synthetic motion, but on
real captures it stayed 0.5-3 dB behind FidelityFX and DLSS-G in moving regions (trailing-edge ghost
of fast near objects, edges a few pixels short). Per the steering above it was removed; no OFXR code
remains in the port. The capture/replay tooling built for it stays.

## Tests

framegen_tests: 20/20 (WARP), plus hardware `gpu_nvidia`, `gpu_nvidia_flow`, `gpu_nvidia_noflow` and
`gpu_dlssg` (needs nvngx_dlssg.dll next to the test exe; the game's copy works). The DLSS-G fixture
lands the moving box within 0.9 px of the true midpoint in both eyes.
