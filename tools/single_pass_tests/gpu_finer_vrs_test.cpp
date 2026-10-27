#include "Render/GpuFinerVrs.hpp"
#include "Render/StereoVrsSnapshot.hpp"
#include "Render/CommandResources.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;
using namespace cvr::stereo;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Check(HRESULT value){if(FAILED(value))throw std::runtime_error("GPU VRS API failure: "+std::to_string(uint32_t(value)));}
int main(int argc,char** argv)try{
    const bool hardware=argc>1 && std::string(argv[1])=="--hardware";
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    if(hardware){for(UINT i=0;SUCCEEDED(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));++i){
        DXGI_ADAPTER_DESC1 desc{};Check(adapter->GetDesc1(&desc));if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))break;adapter.Reset();}if(!adapter)return 77;
    }else Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> messages;device.As(&messages);
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC queueDesc{};Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    constexpr UINT width=13,height=7;constexpr uint8_t rates[]{0,1,4,5,6,9,10};
    std::array<std::vector<uint8_t>,4> values;for(auto& value:values)value.resize(width*height,10);
    for(UINT i=0;i<width*height;++i){values[0][i]=rates[(i/7)%7];values[1][i]=rates[i%7];}values[3].back()=255;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Format=DXGI_FORMAT_R8_UINT;desc.Width=width;desc.Height=height;
    desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
    std::array<ComPtr<ID3D12Resource>,4> images,uploads;
    for(UINT i=0;i<images.size();++i){D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&images[i])));
        auto buffer=desc;buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Format=DXGI_FORMAT_UNKNOWN;buffer.Width=bytes;buffer.Height=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        heap.Type=D3D12_HEAP_TYPE_UPLOAD;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&uploads[i])));
        void* mapped{};Check(uploads[i]->Map(0,nullptr,&mapped));std::memset(mapped,0,size_t(bytes));
        for(UINT y=0;y<height;++y)std::memcpy(static_cast<uint8_t*>(mapped)+size_t(y)*footprint.Footprint.RowPitch,values[i].data()+y*width,width);
        uploads[i]->Unmap(0,nullptr);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=uploads[i].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
        to.pResource=images[i].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={images[i].Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE};list->ResourceBarrier(1,&barrier);
    }
    GpuFinerVrs combined,invalid,duplicate;
    std::array<ID3D12Resource*,4> input{images[0].Get(),images[1].Get(),images[2].Get(),images[0].Get()};
    Check(combined.Create(device.Get(),input));Require(combined.SourceCount()==3,"Duplicate sources were not deduplicated");
    std::array<ID3D12Resource*,2> bad{images[0].Get(),images[3].Get()},same{images[0].Get(),images[0].Get()};
    Check(invalid.Create(device.Get(),bad));Check(duplicate.Create(device.Get(),same));
    GpuFinerVrs empty;Require(FAILED(empty.Create(device.Get(),{})),"Empty rate set was accepted");
    std::vector<ID3D12Resource*> excessive(513,images[0].Get());Require(FAILED(empty.Create(device.Get(),excessive)),"Unbounded descriptor array accepted");
    std::vector<uint8_t> result;UINT errors{};Require(combined.Read(result,errors)==E_PENDING,"Unrecorded merge exposed output");
    // Producer copies, reduction and its consumers are all in one submission;
    // there is no source Map or CPU fence wait before these compute dispatches.
    Require(combined.Record(list.Get()) && invalid.Record(list.Get()) && duplicate.Record(list.Get()),"GPU merge recording failed");
    Require(!combined.Record(list.Get()),"A rate reduction was recorded twice");Check(list->Close());ID3D12CommandList* raw=list.Get();
    auto flight=cvr::gpu::PrepareCommandResources(1,&raw);queue->ExecuteCommandLists(1,&raw);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(flight));
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));Check(queue->Signal(fence.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event unavailable");Check(fence->SetEventOnCompletion(1,event));
    const auto waited=WaitForSingleObject(event,10000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"GPU merge did not complete");
    Check(combined.Read(result,errors));std::vector<uint8_t> expected;Require(BuildFinerVrs(values[0],values[1],expected),"CPU reference failed");
    Require(!errors && result==expected && combined.DispatchCount()==2,"GPU rate reduction differs from per-axis minimum");
    result.clear();Check(duplicate.Read(result,errors));Require(!errors && result==values[0] && duplicate.SourceCount()==1,"Single unique map changed rates");
    result.clear();Check(invalid.Read(result,errors));Require(errors==1 && result.back()==0,"Invalid rate did not flag failure and clamp to finest");
    if(messages)for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T size{};Check(messages->GetMessage(i,nullptr,&size));std::vector<uint8_t> bytes(size);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Check(messages->GetMessage(i,message,&size));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("GPU VRS validation error");}}
    cvr::gpu::CollectCommandResources();std::puts("PASS GPU-produced common VRS: all49 rate pairs, bounds, duplicate maps, invalid rates and source restoration");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
