#pragma once

struct ID3D12Device;

// Lifecycle for the per-eye FSR3 frame-interpolation contexts (vr_framegen branch scaffold).
//
// Real motion-vector + depth frame generation, built from AMD's public FidelityFX-SDK (tag
// fsr3-v3.0.4, see CMakeLists.txt) instead of the color-only optical flow OFXR-Bridge does --
// Ghidra confirmed the game's own shipped ffx_frameinterpolation_x64.dll matches this SDK tag
// byte-for-byte, so this is the same ABI without depending on whatever version CDPR ships.
//
// NOT wired into the render path yet. This only proves the SDK links and the create/destroy
// lifecycle is correct -- one shared FfxInterface backend, two FfxFrameInterpolationContext (one
// per eye, since each eye is a fully independent render in this port's real-stereo pipeline). The
// actual per-frame Dispatch needs dilated depth/motion vectors and the Color/MotionVectors/Depth
// capture this reuses from src/Hooks/NgxEvalPassthrough.cpp -- that is the next step.
bool FrameGen_Init(ID3D12Device* device, unsigned renderWidth, unsigned renderHeight,
                    unsigned displayWidth, unsigned displayHeight);
void FrameGen_Shutdown();
