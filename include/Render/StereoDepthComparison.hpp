#pragma once
#include "Render/StereoGeometryPass.hpp"

namespace cvr::stereo {
struct DepthComparisonResult {
    uint32_t width{},height{},draws{},complete{};
    std::array<uint64_t,2> depthDifferent{},stencilDifferent{},written{},invalidDepth{};
    std::array<float,2> maxDepthError{};
};
static_assert(sizeof(DepthComparisonResult)==88);
// Diagnostic GPU comparison of original PSOs and paired PSOs. Inputs must be
// immutable, GPU-complete snapshots. This owns private outputs and a private
// queue, never copies back into game targets, and never waits on the CPU.
class StereoDepthComparison {
public:
    StereoDepthComparison();
    ~StereoDepthComparison();
    HRESULT Begin(ID3D12Device*,std::span<const GeometryPassDraw>,const D3D12_RESOURCE_DESC&);
    HRESULT Read(DepthComparisonResult&);
    UINT Stage() const {return stage_;}
private:
    struct Storage;
    Microsoft::WRL::ComPtr<Storage> storage_;
    UINT stage_{};
};
}
