#pragma once

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

// FSR3-upscaler's "reconstruct dilated velocity + previous depth" pass, ported from AMD's public
// FidelityFX-SDK GPU source (ffx_fsr3upscaler_reconstruct_dilated_velocity_and_previous_depth.h --
// ReconstructAndDilate / FindNearestDepth / ReconstructPrevDepth) rather than pulling in the whole
// fsr3upscaler component: frame interpolation only consumes this pass's OUTPUT (dilatedDepth,
// dilatedMotionVectors, reconstructPrevNearDepth, see FrameGenFsr3.hpp), not the upscaler's own
// spatial reconstruction, temporal lock or accumulation.
//
// Ported at RENDER resolution only (this port's per-eye Color/MotionVectors/Depth capture, see
// src/Hooks/NgxEvalPassthrough.cpp, is already render-res) -- the FFX_FSR3UPSCALER_OPTION_
// LOW_RESOLUTION_MOTION_VECTORS branch of the source, so no separate high-res motion-vector
// upsample position math. displaySize == renderSize for now (native path, no FSR upscale); a real
// upscale-aware path is future work.
struct FrameGenDilateOutputs {
    ID3D12Resource* dilatedDepth = nullptr;             // R32_FLOAT, render-res
    ID3D12Resource* dilatedMotionVectors = nullptr;     // R16G16_FLOAT, render-res
    ID3D12Resource* reconstructPrevNearDepth = nullptr; // R32_FLOAT, render-res
};

bool FrameGenDilate_Init(ID3D12Device* device);

// Runs Clear + ReconstructAndDilate for one eye slot (0 or 1), (re)allocating that slot's output
// textures if renderWidth/renderHeight changed since the last call. Caller is responsible for
// barriering rawDepth/rawMotionVectors into a shader-readable state before calling, and for
// barriering the returned outputs out of UNORDERED_ACCESS before anything else reads them.
const FrameGenDilateOutputs* FrameGenDilate_Run(ID3D12GraphicsCommandList* cmdList, int eyeSlot,
                                                 ID3D12Resource* rawDepth,
                                                 ID3D12Resource* rawMotionVectors, int renderWidth,
                                                 int renderHeight, float motionVectorScaleX,
                                                 float motionVectorScaleY, bool invertedDepth);

void FrameGenDilate_Shutdown();

// Returns the outputs from the last successful FrameGenDilate_Run for this eye (same pointers --
// no new work done), or nullptr if it has never run. For the FSR3 panel's raw-motion-vector debug
// capture, which wants to look at this pass's own output independent of what FFX does with it.
const FrameGenDilateOutputs* FrameGenDilate_GetLastOutputs(int eyeSlot);
