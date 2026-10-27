# FSR, XeSS and framegen without DLSS (2026-10-09)

The question was whether the port is tied to DLSS: does the second eye render correctly under AMD FSR,
and does the port's framegen work with it. Everything was checked live on 2.31 (RTX 5070 Ti, game
upscaler switched in UserSettings.json, QuickBoot save, same roof scene).

## What the game's upscaler options are

| Setting | Engine node (work RVA) | Implementation | Context |
|---|---|---|---|
| DLSS | ApplyDLSS 0x37D5C4 | Streamline | one per Streamline viewport; the port gives the second eye its own (Dlss.cpp) |
| FSR2 | ApplyFSR2 0x7772E8 | the engine's own compute passes | history resources named per view: already per eye |
| FSR3 | ApplyFSR3_0 0x1D4E8B0 | ffx_fsr3_x64.dll (FidelityFX SDK 1.0) | ONE FfxFsr3Context at manager+44, manager = renderer+18032 |
| FSR3 (FSR 3.1 / FSR 4) | ApplyFSR3 0x1D4BD30 | amd_fidelityfx_dx12.dll (FidelityFX API) | ONE ffxContext in the manager at renderer+18024 |
| XeSS | ApplyXESS 0x76BA40 | libxess.dll | ONE xess_context_handle_t at manager+32 |

On this NVIDIA machine the FSR3 setting ran ApplyFSR3_0 (ffx_fsr3_x64.dll); the FidelityFX API manager
was loaded but created no upscale context. FSR 4 (RDNA 4) goes through the FidelityFX API (see the last
section).

The second eye picks up the upscaler by itself: the frame-graph flag force copies MAIN's upscaler group
(f1 bit 0x80 FSR2, 0x100 FSR3, 0x200 XeSS), and the render-resolution query (sub_1404E42A0) scales any
view whose view+0x17D8 has one of those bits. Both views rendered at 1707x1707 under FSR Quality and
1506x1506 under XeSS (2560 output). No crop.

## Problem 1: FSR 3 and XeSS share one history between the eyes

With one context, each eye's frame is accumulated onto the other eye's history. In the A/B below the
floor loses its detail and a diagonal ghost appears next to the hand (build/single-pass-20261008/
fsr-866/ab_right_hand.png for FSR 3, fsr-868/ab_xess_right.png for XeSS; left = own context, right =
shared).

Fix: src/Stereo/UpscalerViews.cpp. A second context is created next to the engine's, inside the
engine's own create call, and the second eye's dispatches (t_view_side == 1) use it. Destroy and every
setting call follow the engine's context.

- FSR 3.0: hooks ffxFsr3ContextCreate/Destroy/DispatchUpscale. The engine builds the three backend
  interfaces of its description on one scratch buffer sized for four backend contexts; the second
  context gets its own scratch (ffxGetScratchMemorySizeDX12(4) = 5,520,208 bytes) and interfaces made by
  the game's ffxGetInterfaceDX12, so it never takes the engine's slots. The description layout (0x208
  bytes, interfaces at +0x20/+0xB8/+0x150, 0x98 each) is checked: every interface must start with a
  function of ffx_backend_dx12_x64.dll, otherwise nothing is doubled.
- FidelityFX API: hooks ffxCreateContext/DestroyContext/Configure/Dispatch. Only upscale contexts
  (create type 0x00010000) are doubled; upscale and reactive-mask dispatches (0x00010001, 0x00010006) of
  the second eye go to the second context. The engine loads the library late (sub_141D4B800), so the
  hooks are installed from a detour on sub_141D4DEA0, which takes its entry points right after
  LoadLibrary and before any context exists. Exercised through the FSR 4 simulation below.
- XeSS: hooks xessD3D12CreateContext/DestroyContext/D3D12Init/D3D12Execute/SetVelocityScale/
  SetContextParameterF (param in edx, value in xmm2). xessD3D12Init gets no engine heaps
  (sub_14291004C), so two contexts cannot alias each other's memory.

`CyberpunkVR_UpscalerPerEye` (default 1) switches it off live; `CyberpunkVR_UpscalerPerEyeStats[3][4]`
counts per library: second contexts created, first-eye dispatches, second-eye dispatches, failures.
Measured: equal dispatch rates per eye (+426/+426 in 5 s for FSR 3, +242/+242 for XeSS); the surplus of
first-eye dispatches is the main menu and loading, where only MAIN renders.

## Problem 2: framegen got no input without DLSS

Framegen's depth, motion and camera came from Streamline (slSetTag, slSetConstants) or from one native
node, ApplyTXAA, whose camera still came from the Streamline constants node. With FSR the overlay read
`FG 0.0` and every input counter stayed at zero.

Fix:

- src/Framegen/NativeInputs.cpp: the native consumer is any of TAA, FSR2, FSR3_0, FSR3 and XeSS. All four
  upscaler nodes are handed the same two view-salted resources, depth (view<<24 ^ 0x61F178D4) and
  velocity (view<<24 ^ 0x15EAB19C) -- sub_141D4E150 passes them to FSR 3 as "depth" and
  "motionVectors" -- observed through the existing resolve detour (sub_1401F3D20). FSR2 is captured at
  its first engine dispatch like TAA. The libraries move their inputs into their own states, so FSR 3
  and XeSS are captured from the library hooks, before the call.
- An input the current command context has not touched yet is not in the engine's state ledger. For a
  library it is in the state the engine promises the library: FFX state 12 is PIXEL_COMPUTE_READ in this
  SDK (COMMON = 1), D3D12 0xC0. FSR 3's motion was such a resource.
- src/Framegen/Inputs.cpp: the camera of the native consumers is read from the view (the fields
  SetStreamlineConstants copies: fixed-point position at +0x70, right/forward/up at +0x2C0/+0x2D0/
  +0x2E0, near/far +0xB0/+0xB4, FOV in degrees +0x90, aspect +0x98, jitter +0x3E0/-(+0x3E4), motion
  scale and 3D-motion globals, reset at +0xEF0), recorded by the node dispatcher before the node runs.
  `CyberpunkVR_FramegenCameraDelta[2][6]` compares it with Streamline's camera for the same eye and
  frame: all zero on NVIDIA (FOV 104 deg in both).
- src/Framegen/Generator.cpp: the NVIDIA optical-flow backend (the default) needs an NVIDIA adapter and
  nvofapi64.dll; anywhere else the FidelityFX optical flow is used and the overlay says so. Before, the
  backend failed to initialise on AMD and framegen stayed off.

| Upscaler | Native source | Depth / motion | Framegen (overlay) |
|---|---|---|---|
| FSR2 | 2 | 1707x1707, 0xE0 / 0xC0 | REAL 45, FG 45, OUT 92 |
| FSR3 | 3 | 1707x1707, 0xE0 / 0xC0 | REAL 45, FG 45, OUT 92 |
| XeSS | 5 | 1506x1506, 0xE0 / 0xC0 | inputs submitted for both eyes |
| DLSS | Streamline (unchanged) | 1485x1485 | REAL 45, FG 45, OUT 92 |

framegen_tests: 19/19 pass.

## FSR 4 and simulating an AMD card

FSR 4 is in the engine as its own ResolutionScaling value, "FSR4" (with FSR4 quality "Auto" and
FSR4_Sharpness). The list (sub_141C57360) offers it only when the renderer says so (vtable +1016,
sub_141D40FF0): manager(renderer+18024)+0xA0 != -1. sub_141D4C3D0 fills that from ffxQuery
GET_VERSIONS: one version id per family, +0x90 "2.x", +0x98 "3.x", +0xA0 "4.x". Here the library
reports 2.x and 3.x only (`[upscaler] FidelityFX API upscalers:` line in the log); 4.x needs an RDNA 4
driver. Selecting FSR4 creates the upscale context with that version as an override (sub_141D4C7E0) and
runs it in CRenderNode_ApplyFSR3 -- the FidelityFX API path, one context, same per-eye problem.

Two test switches, files next to Cyberpunk2077.exe, inert without them:

- `cvr_simulate_fsr4.txt`: the 3.x provider is put in the 4.x slot, so the game offers and runs FSR4 on
  any GPU, with FSR 3.1 shaders. Everything else is the RDNA 4 path: menu entry, version override,
  ApplyFSR3 node, ffxCreateContext/Configure/Dispatch, the per-eye context, framegen's capture.
- `cvr_simulate_amd.txt`: framegen treats the GPU as non-NVIDIA (vendor 0x1002) and takes the
  FidelityFX optical flow.

Run fsr4-870 (both switches, ResolutionScaling FSR4, framegen on): second-eye FidelityFX API context
created, +243/+243 dispatches per eye in 5 s, native source 4 (ApplyFSR3) at 1280x1280 ("Auto" = 0.5),
overlay `REAL 45 / FG 45 / OUT 92`, `FG: FidelityFX`. A/B with the shared context shows the hand doubled
(build/single-pass-20261008/fsr4-870/ab_fsr4_right.png).

## Not covered

- The FSR 4 model itself and anything the AMD driver does differently (its FSR 4 runtime, stricter
  resource-state handling, AGS). That needs an AMD card; the simulation covers the engine path and the
  port's code around it.
- The game's own frame generation (FSR3_FrameGeneration, DLSS-G) stays as it was: it interpolates the
  desktop swap chain, not the headset.
