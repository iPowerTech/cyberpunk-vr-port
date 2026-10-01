// Depth-plane resolve for 64bpp scene depth.
//
// CP2077 (and many modern engines) use R32G8X24_TYPELESS for the main scene
// depth — a 64-bits-per-pixel format with separate depth (32-bit float plane
// 0) and stencil (8-bit uint plane 1) subresources. OpenXR composition_layer_
// depth wants a single typed depth swapchain (D32_FLOAT 32bpp). Naive
// CopyResource between the two formats triggers DEVICE_HUNG because the bit
// layouts don't match.
//
// Solution: fullscreen shader pass. Sample the depth plane via
// R32_FLOAT_X8X24_TYPELESS SRV (reads plane 0 of R32G8X24_TYPELESS as a single
// float channel) and write to a D32_FLOAT render target (DepthStencilView).
// The shader uses SV_Depth to write into the depth plane.
//
// This unlocks the depth-layer submit for the majority of scenes — without
// it positional async-timewarp is not possible.
#pragma once

#include <windows.h>
#include <d3d12.h>
#include <mutex>
#include <wrl.h>

class DepthResolve {
public:
    DepthResolve() = default;
    ~DepthResolve();

    // Lazy init for a given (device, output dims/format). Multi-call safe.
    bool EnsureInitialized(ID3D12Device* device,
                           DXGI_FORMAT depthOutputFormat,
                           uint32_t width,
                           uint32_t height);

    // Record a fullscreen draw that resolves the depth plane of `src` (a
    // R32G8X24_TYPELESS texture) into the depth plane of `dst` (D32_FLOAT or
    // R32_FLOAT, depending on what the swapchain expects). Caller must
    // transition:
    //   src: PIXEL_SHADER_RESOURCE
    //   dst: DEPTH_WRITE
    bool RecordResolve(ID3D12GraphicsCommandList* cmdList,
                       ID3D12Resource* src,
                       ID3D12Resource* dst);


    void Shutdown();

private:
    bool EnsureColorPipeline();   // lazy-build the SV_Target R32_FLOAT variant

    std::mutex m_mutex;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    // Color (R32_FLOAT RTV) variant — its own PSO + SRV/RTV heaps so it never
    // disturbs the depth-output path used by the mono depth-layer submit.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoColor;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeapColor;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    DXGI_FORMAT m_outputFormat = DXGI_FORMAT_UNKNOWN;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    // THE SRV HEAP IS A RING, AND IT HAS TO BE.
    //
    // RecordResolve used to write its one descriptor into slot 0 of a 1-slot SHADER-VISIBLE heap on
    // every call. A shader-visible descriptor is read by the GPU when the draw EXECUTES, not when it
    // is recorded, so rewriting that slot while an earlier frame's DrawInstanced is still in flight
    // is undefined behaviour. It survived for one reason only: the bytes written were normally
    // identical (same source resource, same format), so the overwrite was a no-op in practice.
    //
    // It stops being a no-op the moment CP2077's scene depth resource actually changes -- a new
    // resource pointer, and a different SRV format when it swaps between R32_TYPELESS (32bpp) and
    // R32G8X24_TYPELESS (64bpp). That is exactly the frame the crash logs mark: the last line before
    // the process dies is always "depth format gameFmt=19 selected=40" arriving after an earlier
    // pin of 39. Runs where the format never changed never crashed; runs where it changed sometimes
    // did -- the signature of a race against whatever is still in flight.
    //
    // A ring removes the race outright: a descriptor is never rewritten while it can still be read.
    static const uint32_t kSrvRing = 64;
    uint32_t m_srvRing = 0;
    UINT m_srvInc = 0;
};
