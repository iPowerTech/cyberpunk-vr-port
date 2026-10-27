#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>

namespace cvr::stereo {
// Opaque, reverse-Z group only: ALWAYS/REPLACE stencil, full MRT writes, no
// blending. The caller restores native graphics bindings after Record().
class StereoGroupResolve {
public:
    static bool Supports(const D3D12_BLEND_DESC&,const D3D12_DEPTH_STENCIL_DESC&);
    HRESULT Initialize(ID3D12Device*,const std::array<ID3D12Resource*,4>&,bool depthOnly=false);
    bool Retain(ID3D12GraphicsCommandList*) const;
    void Record(ID3D12GraphicsCommandList*,UINT eye,UINT stencilReference,
        const D3D12_VIEWPORT&,const D3D12_RECT&,
        const std::array<D3D12_CPU_DESCRIPTOR_HANDLE,3>&,D3D12_CPU_DESCRIPTOR_HANDLE) const;
private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    std::array<ID3D12Resource*,4> sources_{};
    bool depthOnly_{};
};
}
