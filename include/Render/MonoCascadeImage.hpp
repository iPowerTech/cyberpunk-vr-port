#pragma once
#include <d3d12.h>
#include <wrl/client.h>

namespace cvr::cascades {
class AtlasImage {
    Microsoft::WRL::ComPtr<ID3D12Resource> image;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptors;
    D3D12_RESOURCE_DESC shape{};
public:
    bool Initialize(ID3D12Device*,ID3D12Resource*,DXGI_FORMAT);
    bool Compatible(ID3D12Resource*) const;
    bool Clear(ID3D12GraphicsCommandList*,float);
    bool Capture(ID3D12GraphicsCommandList*,ID3D12Resource*,UINT slice,D3D12_RESOURCE_STATES);
    bool Restore(ID3D12GraphicsCommandList*,ID3D12Resource*,UINT first,UINT count);
    ID3D12Resource* Resource() const{return image.Get();}
};
}
