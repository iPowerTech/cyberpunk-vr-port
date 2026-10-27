#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <span>

namespace cvr::stereo {
// Array output for a common geometry pass, with an explicit copy back into the
// native single-view targets consumed by unmodified lighting/temporal shaders.
// The owner retains this object and native destinations until their command
// queue fence completes; no CPU waits here.
class StereoTargetArray {
public:
    HRESULT Initialize(ID3D12Device*,const D3D12_RESOURCE_DESC& nativeTarget,
        D3D12_RESOURCE_STATES initialState,const D3D12_CLEAR_VALUE* clear=nullptr);
    HRESULT CopyEye(ID3D12GraphicsCommandList*,UINT eye,D3D12_RESOURCE_STATES sourceState,
        ID3D12Resource* nativeTarget,D3D12_RESOURCE_STATES targetBefore,D3D12_RESOURCE_STATES targetAfter) const;
    HRESULT ImportEye(ID3D12GraphicsCommandList*,UINT eye,D3D12_RESOURCE_STATES arrayState,
        ID3D12Resource* nativeSource,D3D12_RESOURCE_STATES sourceState) const;
    // Initialize either or both layers with one source/array transition pair.
    HRESULT ImportEyes(ID3D12GraphicsCommandList*,UINT eyeMask,D3D12_RESOURCE_STATES arrayState,
        ID3D12Resource* nativeSource,D3D12_RESOURCE_STATES sourceState) const;
    // Per-layer variants leave the other eye's state and contents untouched.
    HRESULT TransitionEye(ID3D12GraphicsCommandList*,UINT eye,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) const;
    HRESULT ImportEyeLayer(ID3D12GraphicsCommandList*,UINT eye,D3D12_RESOURCE_STATES arrayState,
        ID3D12Resource* nativeSource,D3D12_RESOURCE_STATES sourceState) const;
    HRESULT CopyEyeLayer(ID3D12GraphicsCommandList*,UINT eye,D3D12_RESOURCE_STATES arrayState,
        ID3D12Resource* nativeTarget,D3D12_RESOURCE_STATES targetBefore,D3D12_RESOURCE_STATES targetAfter) const;
    // Exactly PlaneCount states in native subresource order. Copies honor the
    // caller's predication; barriers remain unconditional. The caller proves
    // native access/state and retains lifetimes; this helper grants no authority.
    HRESULT CopyEyeLayer(ID3D12GraphicsCommandList*,UINT eye,D3D12_RESOURCE_STATES arrayState,
        ID3D12Resource* nativeTarget,std::span<const D3D12_RESOURCE_STATES> targetBefore,
        std::span<const D3D12_RESOURCE_STATES> targetAfter) const;
    ID3D12Resource* Resource() const {return resource_.Get();}
    UINT PlaneCount() const {return planes_;}
private:
    HRESULT ValidateLayer(ID3D12GraphicsCommandList*,UINT eye,ID3D12Resource* native) const;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    D3D12_RESOURCE_DESC native_{};
    UINT planes_{};
};
}
