#pragma once
#include <d3d12.h>
#include <cstdint>

namespace cvr::stereo::instance_gpu_probe {
// The caller validates source ownership, native read position, resource states
// and an initially disabled per-copy predicate before using this command block.
inline void Record(ID3D12GraphicsCommandList* list,ID3D12Resource* source,uint64_t sourceOffset,
    ID3D12Resource* readback,uint64_t destinationOffset,uint64_t bytes,ID3D12Resource* predicate,uint64_t predicateOffset){
    list->SetPredication(predicate,predicateOffset,D3D12_PREDICATION_OP_EQUAL_ZERO);
    list->CopyBufferRegion(readback,destinationOffset,source,sourceOffset,bytes);
    list->SetPredication(nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
}
}
