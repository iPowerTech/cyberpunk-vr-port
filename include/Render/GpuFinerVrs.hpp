#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <span>
#include <vector>

namespace cvr::stereo {
// Inputs are owned original-position snapshots in SHADING_RATE_SOURCE state.
// The caller proves their GPU producer order; no source pixels are read on CPU
// during creation or recording. Source states are restored after reduction.
class GpuFinerVrs {
public:
    GpuFinerVrs();~GpuFinerVrs();
    HRESULT Create(ID3D12Device*,std::span<ID3D12Resource* const>);
    // Single-use DIRECT recording. Changes heaps and compute bindings.
    bool Record(ID3D12GraphicsCommandList*);
    ID3D12Resource* Image()const;
    UINT SourceCount()const;
    UINT DispatchCount()const;
    UINT64 ReadbackBytes()const;
    // Diagnostic only, after the consumer's actual submission fence completes.
    HRESULT Read(std::vector<uint8_t>& pixels,UINT& invalidRates)const;
private:
    struct Storage;Microsoft::WRL::ComPtr<Storage> storage_;
};
}
