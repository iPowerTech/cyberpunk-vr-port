#pragma once
#include "Render/StereoSceneState.hpp"

namespace cvr::stereo {
// One frame's writable depth/G-buffer chain. The caller establishes native
// target states, serializes recording, orders same-eye lists, and exports before
// any native reader, alias, clear or unredirected writer can consume the targets.
// This owns images, not geometry/material admission or native command scheduling.
class StereoGeometryChain {
public:
    StereoGeometryChain();
    ~StereoGeometryChain();
    HRESULT Create(ID3D12Device*,UINT width,UINT height);
    // Import each newly attached target once. Repeated depth/3-MRT/4-MRT stages
    // retain previous layer contents; targets must remain RT/DEPTH_WRITE.
    HRESULT PrepareEye(ID3D12GraphicsCommandList*,UINT eye,const scene_state::GeometryTargets&);
    // Alternative isolated initialization. No native attachment/import/export.
    bool ClearPrivateEye(ID3D12GraphicsCommandList*,UINT eye,UINT colors,float depth,UINT8 stencil);
    bool SeedPrivateDepth(ID3D12GraphicsCommandList*,UINT eye,ID3D12Resource* completedDepth);
    // Full private baseline, with colors and DEPTH_WRITE already established by
    // the caller's ordered checkpoint producer. No clear or native attachment.
    bool SeedPrivateEye(ID3D12GraphicsCommandList*,UINT eye,const scene_state::GeometryTargets& baseline);
    // Mask 1/2 selects one layer with native PSOs; mask 3 requires a proven
    // view-instanced PSO. No shaders, roots, viewport or view mask are changed.
    bool Bind(ID3D12GraphicsCommandList*,UINT eyeMask,UINT colors);
    bool RestoreNative(ID3D12GraphicsCommandList*,UINT eye,UINT colors);
    // Export all attached images once and seal this eye. Native states are
    // restored; only this private eye transitions to COPY_SOURCE afterwards.
    bool ExportEye(ID3D12GraphicsCommandList*,UINT eye);
    ID3D12Resource* Resource(UINT slot)const; // four color slots, depth in slot 4
    // Attachment imports 0/1, single-eye binds 0/1, both-eye binds,
    // attachment exports 0/1.
    std::array<uint64_t,7> Counts()const;
private:
    struct Storage;
    Microsoft::WRL::ComPtr<Storage> storage_;
};
}
