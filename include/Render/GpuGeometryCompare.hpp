#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <span>

namespace cvr::stereo {
struct GeometryCompareRange {
    UINT pair{};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,2> resources;
    std::array<UINT64,2> offsets{};
    UINT64 bytes{};
};
// GPU equality plus complementary draw predicates. The caller certifies immutable
// inputs, NON_PIXEL_SHADER_RESOURCE states and producer order through execution.
// Each pair must have at least one range. A zero external gate disables BOTH
// branches; otherwise exactly one branch executes. No CPU readback is required.
// Results also finish NON_PIXEL_SHADER_RESOURCE, so a checked predicate can be
// the permission input of another comparison in the same GPU recording.
// All ranges sharing the same resource pair use one metadata-driven dispatch.
class GpuGeometryCompare {
public:
    GpuGeometryCompare();
    ~GpuGeometryCompare();
    HRESULT Create(ID3D12Device*,UINT pairs,std::span<const GeometryCompareRange>,ID3D12Resource* gate,UINT64 gateOffset=0);
    // Single recording, direct list only. Changes compute root/PSO and clears
    // predication; caller must restore native bindings. Retains all input owners.
    bool Record(ID3D12GraphicsCommandList*);
    ID3D12Resource* Predicates() const;
    static constexpr UINT64 Offset(UINT pair,bool fallback=false){return UINT64(pair)*16+(fallback?8:0);}
    UINT PairCount() const;
    UINT RangeCount() const;
    UINT DispatchCount() const;
    UINT64 ComparedBytes() const;
private:
    struct Storage;
    Microsoft::WRL::ComPtr<Storage> storage_;
};
}
