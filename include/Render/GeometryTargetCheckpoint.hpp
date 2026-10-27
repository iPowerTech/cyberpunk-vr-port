#pragma once
#include "Render/StereoSceneState.hpp"

namespace cvr::stereo {
class GeometryTargetCheckpoint {
    struct Storage;
    Microsoft::WRL::ComPtr<Storage> storage_;
public:
    GeometryTargetCheckpoint();
    ~GeometryTargetCheckpoint();
    HRESULT Create(ID3D12Device*,const scene_state::GeometryTargets& shape);
    // One recording only. The caller proves native RT/DEPTH_WRITE states and
    // one-shot submission. Captured means recorded, not GPU-complete.
    bool Capture(ID3D12GraphicsCommandList*,const scene_state::GeometryTargets&);
    bool Captured() const;
    // Empty until Capture has successfully recorded all copies.
    const scene_state::GeometryTargets& Targets() const;
};
}
