// Rectangle depth clears on the real adapter: the live Pico 4 mask (DLSS input 1485 x 1485,
// radius 100%, 205 rectangles) cleared to 1.0 over a 0.0 scene-depth clear, read back and compared
// pixel by pixel. `hidden_area_gpu batched` uses 64 rectangles per call (the shipped path);
// `hidden_area_gpu single` issues all of them in one call (the call that ended the game).
#include "Render/HiddenArea.hpp"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace cvr::hidden_area;

static bool Ok(HRESULT hr,const char* what){if(FAILED(hr)){std::printf("%s failed 0x%08lx\n",what,unsigned long(hr));return false;}return true;}

int main(int argc,char** argv) {
    const bool single=argc>1 && !std::strcmp(argv[1],"single");
    const UINT w=1485,h=1485;const float t=std::tan(52*3.14159265f/180);
    auto rects=BuildRects({-t,t,t,-t},100,w,h,t,t,8,1);
    if(single && argc>2)rects.resize(std::min(rects.size(),size_t(std::atoi(argv[2]))));   // probe the limit
    ComPtr<IDXGIFactory6> factory;ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;
    if(!Ok(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"factory") ||
       !Ok(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)),"adapter") ||
       !Ok(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)),"device"))return 2;
    DXGI_ADAPTER_DESC1 ad{};adapter->GetDesc1(&ad);
    std::printf("adapter %ls, %zu rectangles, %s\n",ad.Description,rects.size(),single?"one call":"64 per call");
    D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> list;
    if(!Ok(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"queue") ||
       !Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)),"allocator") ||
       !Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)),"list"))return 2;
    D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc{D3D12_RESOURCE_DIMENSION_TEXTURE2D,0,w,h,1,1,DXGI_FORMAT_R32G8X24_TYPELESS,{1,0},D3D12_TEXTURE_LAYOUT_UNKNOWN,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL};
    D3D12_CLEAR_VALUE cv{};cv.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;cv.DepthStencil={0,0};
    ComPtr<ID3D12Resource> depth;
    if(!Ok(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&cv,IID_PPV_ARGS(&depth)),"depth"))return 2;
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1};ComPtr<ID3D12DescriptorHeap> dsvHeap;
    if(!Ok(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsvHeap)),"dsv heap"))return 2;
    D3D12_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;dv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
    const auto dsv=dsvHeap->GetCPUDescriptorHandleForHeapStart();device->CreateDepthStencilView(depth.Get(),&dv,dsv);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes{};device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES rb{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bd{D3D12_RESOURCE_DIMENSION_BUFFER,0,bytes,1,1,1,DXGI_FORMAT_UNKNOWN,{1,0},D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    ComPtr<ID3D12Resource> readback;
    if(!Ok(device->CreateCommittedResource(&rb,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"readback"))return 2;

    list->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,0,0,0,nullptr);
    const auto* all=reinterpret_cast<const D3D12_RECT*>(rects.data());
    const size_t batch=single?rects.size():64;
    for(size_t at=0;at<rects.size();at+=batch)
        list->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,UINT(std::min(batch,rects.size()-at)),all+at);
    D3D12_RESOURCE_BARRIER b{};b.Transition={depth.Get(),0,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION dst{readback.Get(),D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};dst.PlacedFootprint=fp;
    D3D12_TEXTURE_COPY_LOCATION src{depth.Get(),D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};src.SubresourceIndex=0;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    if(!Ok(list->Close(),"close"))return 2;
    ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;if(!Ok(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence"))return 2;
    queue->Signal(fence.Get(),1);HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);fence->SetEventOnCompletion(1,event);
    if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0){std::printf("GPU timeout, removed reason 0x%08lx\n",unsigned long(device->GetDeviceRemovedReason()));return 3;}
    if(!Ok(device->GetDeviceRemovedReason(),"device"))return 3;

    std::vector<uint8_t> inside(size_t(w)*h);
    for(const auto& r:rects)for(int32_t y=r.top;y<r.bottom;++y)for(int32_t x=r.left;x<r.right;++x)inside[size_t(y)*w+x]=1;
    void* data{};if(!Ok(readback->Map(0,nullptr,&data),"map"))return 2;
    size_t wrong=0,masked=0;
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x) {
        float v;std::memcpy(&v,static_cast<uint8_t*>(data)+fp.Offset+size_t(y)*fp.Footprint.RowPitch+x*4,4);
        masked+=v==1.0f;wrong+=(v==1.0f)!=(inside[size_t(y)*w+x]!=0) || (v!=1.0f && v!=0.0f);
    }
    readback->Unmap(0,nullptr);
    std::printf("masked %zu of %u pixels (%.1f%%), wrong %zu\n",masked,w*h,100.0*masked/(double(w)*h),wrong);
    return wrong?1:0;
}
