#pragma once
#include <d3d12.h>

namespace cvr::diagnostics {
void ObserveResourceCreation(ID3D12Resource* resource, const D3D12_RESOURCE_DESC& desc);
}
