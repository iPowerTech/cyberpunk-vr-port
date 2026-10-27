#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <vector>
#include <span>
#include <algorithm>
namespace cvr::stereo {
inline bool BuildFinerVrs(std::span<const uint8_t> a,std::span<const uint8_t> b,std::vector<uint8_t>& out){
    out.clear();if(a.empty() || a.size()!=b.size())return false;std::vector<uint8_t> values;values.reserve(a.size());
    const auto valid=[](uint8_t value){return value==0 || value==1 || value==4 || value==5 || value==6 || value==9 || value==10;};
    for(size_t i=0;i<a.size();++i){if(!valid(a[i]) || !valid(b[i]))return false;
        values.push_back(uint8_t(((std::min)(a[i]>>2,b[i]>>2)<<2)|(std::min)(a[i]&3,b[i]&3)));}
    out=std::move(values);return true;
}
// One diagnostic frame. Owner retains native source images until its fence.
class StereoVrsSnapshot {
public:
    HRESULT Initialize(ID3D12Device*,ID3D12Resource* image);
    HRESULT Capture(ID3D12GraphicsCommandList*,ID3D12Resource* image,UINT eye);
    bool Retain(ID3D12GraphicsCommandList*) const;
    HRESULT Read(uint64_t& differences,std::array<std::array<uint64_t,16>,2>& histogram) const;
    // Caller has observed the source submission fence. Returns pixels only.
    HRESULT ReadEye(UINT eye,std::vector<uint8_t>&)const;
    ID3D12Resource* SourceImage() const {return frozen_.Get();}
    UINT Width()const{return UINT(desc_.Width);}UINT Height()const{return desc_.Height;}
private:
    Microsoft::WRL::ComPtr<ID3D12Resource> frozen_,readback_;
    D3D12_RESOURCE_DESC desc_{};
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,2> footprints_{};
    UINT64 bytes_{};
    UINT capturedMask_{};
};
}
