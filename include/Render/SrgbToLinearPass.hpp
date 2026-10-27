// The eye images hold display-encoded (sRGB) bytes. When the runtime offers no swapchain format that
// declares them as such (PickMonoSwapchainFormat, include/Runtimes/OpenXRInternal.hpp), the submit draws
// each eye through this pass instead of copying it: decode to linear, and let the target view encode
// again if it is an sRGB format. Ported from the TofuExpress fork (2026-10-16).
#pragma once

#include <windows.h>
#include <d3d12.h>
#include <mutex>
#include <wrl.h>

class SrgbToLinearPass {
public:
    SrgbToLinearPass() = default;
    ~SrgbToLinearPass();

    // outputFormat: the swapchain's typed format (the render target view). Multi-call safe.
    bool EnsureInitialized(ID3D12Device* device, DXGI_FORMAT outputFormat, uint32_t width, uint32_t height);
    // source in PIXEL_SHADER_RESOURCE, destination in RENDER_TARGET. descriptorIndex < 6: one per submit
    // command list (three) and eye, so a list still in flight never has its descriptors rewritten.
    bool Record(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* source,
                ID3D12Resource* destination, uint32_t descriptorIndex);
    void Shutdown();

private:
    std::mutex m_mutex;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    DXGI_FORMAT m_outputFormat = DXGI_FORMAT_UNKNOWN;
    UINT m_srvDescriptorSize = 0;
    UINT m_rtvDescriptorSize = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};
