#pragma once
#include "Render/HudPanel.hpp"

// Opt-in diagnostic readback. Uses the same leased native texture and Present
// queue as HUD capture, without changing the material, camera or its pixels.
namespace cvr::optics::probe {
bool Request(std::shared_ptr<hud::TextureLease> texture);
void Capture(ID3D12Device* device, ID3D12CommandQueue* queue);
bool StageRequested();
bool StageView(uint64_t key,uint64_t eyeKey);
void StageTarget(ID3D12GraphicsCommandList*,ID3D12Resource*);
void StageBarriers(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
void StageEnd(ID3D12Device*);
}
