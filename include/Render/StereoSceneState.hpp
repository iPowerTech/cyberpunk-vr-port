#pragma once
#include "Render/NativeStereoProbe.hpp"
#include <wrl/client.h>
namespace cvr::stereo::scene_state {
struct Bindings {
    struct Rate {
        D3D12_SHADING_RATE value=D3D12_SHADING_RATE_1X1;
        std::array<D3D12_SHADING_RATE_COMBINER,2> combiners{D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,D3D12_SHADING_RATE_COMBINER_PASSTHROUGH};
        ID3D12Resource* image{};
        bool known{};
    } rate;
    ID3D12RootSignature* root{};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE,64> tables{};
    uint64_t tableMask{};
    ID3D12RootSignature* computeRoot{};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE,64> computeTables{};
    uint64_t computeTableMask{};
    std::array<ID3D12DescriptorHeap*,2> heaps{};
    UINT heapCount{},stencil{};
    std::array<float,4> blendFactor{1,1,1,1};
    D3D12_PRIMITIVE_TOPOLOGY topology{};
    bool resetSeen{},heapsSeen{};
    void Restore(ID3D12GraphicsCommandList*) const;
};
struct Targets {std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,4> resources;};
using TargetInfo=pass_targets::View;
struct GeometryTargets {
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,5> resources;
    std::array<TargetInfo,5> views{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE,5> handles{};
    UINT colors{};
};
struct DrawState {
    Microsoft::WRL::ComPtr<ID3D12PipelineState> original;
    Microsoft::WRL::ComPtr<ID3D12Resource> rateImage;
    Bindings::Rate rate;
    D3D12_PRIMITIVE_TOPOLOGY topology{};
    UINT stencil{};
    std::array<float,4> blendFactor{1,1,1,1};
    Targets targets;
};
void RootCreated(ID3D12RootSignature*,const void*,size_t);
void PipelineCreated(ID3D12PipelineState*,bool resolveCompatible);
void ResetCapture();
void Reset(ID3D12GraphicsCommandList*,bool rateKnown=false);
void Root(ID3D12GraphicsCommandList*,ID3D12RootSignature*);
void Table(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_DESCRIPTOR_HANDLE);
void ComputeRoot(ID3D12GraphicsCommandList*,ID3D12RootSignature*);
void ComputeTable(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_DESCRIPTOR_HANDLE);
void Heaps(ID3D12GraphicsCommandList*,UINT,ID3D12DescriptorHeap* const*);
void Topology(ID3D12GraphicsCommandList*,D3D12_PRIMITIVE_TOPOLOGY);
void Stencil(ID3D12GraphicsCommandList*,UINT);
void BlendFactor(ID3D12GraphicsCommandList*,const float*);
void ShadingRate(ID3D12GraphicsCommandList*,D3D12_SHADING_RATE,const D3D12_SHADING_RATE_COMBINER*);
void ShadingRateImage(ID3D12GraphicsCommandList*,ID3D12Resource*);
bool Snapshot(ID3D12GraphicsCommandList*,Bindings&);
// Actual source-command bindings, including legal stencil255. Captures all
// initialized64-slot tables; shader-required coverage and descriptor contents
// remain separate caller receipts.
bool CaptureSourceBindings(ID3D12GraphicsCommandList*,Bindings&);
// Only at an actual native draw, while its PSO and bound image are live.
// Does not certify shader-specific dynamic depth/sample state or resource contents.
bool CaptureDrawState(ID3D12GraphicsCommandList*,ID3D12PipelineState*,DrawState&);
void RtvCreated(ID3D12Resource*,const D3D12_RENDER_TARGET_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
void DsvCreated(ID3D12Resource*,const D3D12_DEPTH_STENCIL_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
bool CaptureTargets(const native_probe::DrawRecord&,Targets&);
// Only at an actual native draw while the engine owns all bound resources.
// Slots 0..3 are colors; slot 4 is depth. Preserves read-only DSV flags.
// Identity and shape checks do not establish resource state or alias lifetime.
bool CaptureGeometryTargets(const native_probe::DrawRecord&,GeometryTargets&);
// At an actual OM/clear API call while its caller owns the bound resources.
// Resource-only lookup, including array/mip views and internal private targets;
// grants no whole-view, resource-state, residency or alias permission.
bool CaptureBoundResources(UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,BOOL,const D3D12_CPU_DESCRIPTOR_HANDLE*,
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,9>&);
// Weak metadata captured at the actual draw. Identity is tied to COM resource
// lifetime; this does not retain a resource or prove its contents/alias state.
bool DescribeTargets(const native_probe::DrawRecord&,std::array<TargetInfo,9>&);
bool Compatible(ID3D12PipelineState*);
}
