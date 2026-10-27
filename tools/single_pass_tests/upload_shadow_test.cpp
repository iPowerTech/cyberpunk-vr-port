#include "Render/NativeUploadShadow.hpp"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <stdexcept>
#include <iostream>
using namespace cvr::stereo::packets;using Microsoft::WRL::ComPtr;
void Require(bool value){if(!value)throw std::runtime_error("Upload shadow assertion failed");}
void Check(HRESULT result){Require(SUCCEEDED(result));}
int main()try{
    UploadShadow shadow;shadow.Reset(16);std::array<uint8_t,8> a{1,2,3,4,5,6,7,8},out{};
    Require(!shadow.Read(0,out.data(),1));Require(shadow.Write(0,a));Require(shadow.Write(8,a));Require(shadow.Read(4,out.data(),8));
    Require(out[0]==5 && out[4]==1);shadow.Invalidate(6,4);Require(!shadow.Read(4,out.data(),8) && shadow.Read(0,out.data(),6) && shadow.Read(10,out.data(),6));
    Require(shadow.Write(6,std::span<const uint8_t>(a).first(4)) && shadow.Read(4,out.data(),8));
    Require(!shadow.Write(15,a) && !shadow.Read(SIZE_MAX,out.data(),8));shadow.Reset();Require(!shadow.Read(0,out.data(),1));
    ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> warp;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    auto buffer=[&](D3D12_HEAP_TYPE type,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE){
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=64;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=flags;
        ComPtr<ID3D12Resource> result;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&result)));return result;
    };
    auto source=buffer(D3D12_HEAP_TYPE_UPLOAD),destination=buffer(D3D12_HEAP_TYPE_DEFAULT),unknown=buffer(D3D12_HEAP_TYPE_DEFAULT);
    void* data{};D3D12_RANGE none{};Check(source->Map(0,&none,&data));for(unsigned i=0;i<64;++i)static_cast<uint8_t*>(data)[i]=uint8_t(i);source->Unmap(0,nullptr);
    const auto gpu=destination->GetGPUVirtualAddress(),id=reinterpret_cast<uintptr_t>(destination.Get());upload_shadow::Watch(gpu,id,64);
    upload_shadow::Observe(destination.Get(),8,source.Get(),3,12);Require(upload_shadow::Read(gpu,id,8,out.data(),8) && out[0]==3 && out[7]==10);
    Require(!upload_shadow::Read(gpu,id+1,8,out.data(),8) && !upload_shadow::Read(gpu,id,0,out.data(),1));
    upload_shadow::Observe(destination.Get(),10,unknown.Get(),0,4);Require(!upload_shadow::Read(gpu,id,8,out.data(),8) && upload_shadow::Read(gpu,id,8,out.data(),2));
    upload_shadow::Invalidate(destination.Get());Require(!upload_shadow::Read(gpu,id,8,out.data(),1));
    auto uav=buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);const auto uavGpu=uav->GetGPUVirtualAddress(),uavId=reinterpret_cast<uintptr_t>(uav.Get());
    upload_shadow::Watch(uavGpu,uavId,64);upload_shadow::Observe(uav.Get(),0,source.Get(),0,16);Require(!upload_shadow::Read(uavGpu,uavId,0,out.data(),8));
    upload_shadow::Watch(0,0,0);Require(!upload_shadow::Active());
    std::cout<<"Upload shadow: ownership, holes, overlapping copies, unknown writes, identity, UAV exclusion and CPU-upload mapping passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
