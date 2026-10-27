// CAS Sharpening: saturate(center + 4*amount*(CAS-center)).
// The private scratch SRV never changes. All calls must execute in recording
// order on the same queue; barriers serialize scratch copies and shader reads.
#pragma once

#include <windows.h>
#include <d3d12.h>
#include <mutex>
#include <wrl.h>

class SharpenPass {
public:
    SharpenPass() = default;
    ~SharpenPass();

    // (device, output color format, width, height). Multi-call safe.
    bool EnsureInitialized(ID3D12Device* device,
                           DXGI_FORMAT colorFormat,
                           uint32_t width,
                           uint32_t height);

    // Source stays COPY_SOURCE, destination stays RENDER_TARGET. A private copy
    // also accepts sources with DENY_SHADER_RESOURCE. False records no commands.
    // For an exclusively owned capture, src==dst sharpens in place and restores
    // COPY_SOURCE, so real frames can be filtered before interpolation.
    // sharpness = Emphasis (0..1), amount = CAS Sharpening (0..1, default 0).
    bool RecordSharpen(ID3D12GraphicsCommandList* cmdList,
                       ID3D12Resource* srcColor,
                       ID3D12Resource* dstColor,
                       float sharpness,
                       float amount);

    void Shutdown();

private:
    std::mutex m_mutex;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_scratch;
    DXGI_FORMAT m_colorFormat = DXGI_FORMAT_UNKNOWN;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};
