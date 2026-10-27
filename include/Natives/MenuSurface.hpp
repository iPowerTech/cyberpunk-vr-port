#pragma once
#include <d3d12.h>

namespace cvr::menu_surface {
bool Waiting();
// Native ink targets owned by the menu, outside the shared world-widget atlas.
// These callbacks observe engine work; they never composite into an XR image.
void Barriers(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
void Written(ID3D12GraphicsCommandList*,ID3D12Resource*);
void Submitted(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
void ResetList(ID3D12GraphicsCommandList*);
}
