#include "Render/GpuGeometryCompare.hpp"
#include "Render/CommandResources.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace cvr::stereo;
static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("D3D12 geometry comparison failed: "+std::to_string(uint32_t(hr)));}
static void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,UINT64 bytes,D3D12_HEAP_TYPE type){
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&result)));return result;
}
int main(int argc,char** argv)try{
    const bool hardware=argc>1 && std::string(argv[1])=="--hardware";
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    if(hardware){for(UINT i=0;SUCCEEDED(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));++i){
        DXGI_ADAPTER_DESC1 desc{};Check(adapter->GetDesc1(&desc));if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))break;adapter.Reset();}if(!adapter)return 77;
    }else Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> messages;device.As(&messages);
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC queueDesc{};Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    constexpr UINT width=16384;
    auto a=Buffer(device.Get(),width,D3D12_HEAP_TYPE_DEFAULT),b=Buffer(device.Get(),width,D3D12_HEAP_TYPE_DEFAULT);
    auto upload=Buffer(device.Get(),width,D3D12_HEAP_TYPE_UPLOAD),changes=Buffer(device.Get(),width,D3D12_HEAP_TYPE_UPLOAD);
    auto gate=Buffer(device.Get(),16,D3D12_HEAP_TYPE_UPLOAD),zero=Buffer(device.Get(),8192,D3D12_HEAP_TYPE_UPLOAD);
    auto readback=Buffer(device.Get(),8192,D3D12_HEAP_TYPE_READBACK);
    std::vector<uint8_t> data(width);for(UINT i=0;i<width;++i)data[i]=uint8_t(i*17+5);
    void* mapped{};const D3D12_RANGE none{};
    for(auto* resource:{upload.Get(),changes.Get()}){Check(resource->Map(0,&none,&mapped));std::memcpy(mapped,data.data(),width);resource->Unmap(0,nullptr);}
    Check(changes->Map(0,&none,&mapped));static_cast<uint8_t*>(mapped)[100]^=1;static_cast<uint8_t*>(mapped)[11999]^=2;changes->Unmap(0,nullptr);
    Check(gate->Map(0,&none,&mapped));std::memset(mapped,0,16);gate->Unmap(0,nullptr);
    Check(zero->Map(0,&none,&mapped));std::memset(mapped,0,8192);static_cast<uint64_t*>(mapped)[0]=0xABCDEF;zero->Unmap(0,nullptr);
    std::vector<GeometryCompareRange> ranges;
    std::vector<bool> equal;
    // Every offset alignment and every masked tail; bytes just outside the range
    // differ for several cases and must not affect their predicate.
    for(UINT offset=97;offset<104;++offset)for(UINT bytes=1;bytes<=8;++bytes){
        const UINT pair=UINT(equal.size());equal.push_back(!(offset<=100 && 100<offset+bytes));
        ranges.push_back({pair,{a,b},{offset,offset},bytes});
    }
    const UINT longPair=UINT(equal.size());equal.push_back(false);
    ranges.push_back({longPair,{a,b},{4096,4096},8000});
    const UINT multiPair=UINT(equal.size());equal.push_back(false);
    ranges.push_back({multiPair,{a,b},{500,500},65});ranges.push_back({multiPair,{a,b},{11998,11998},2});
    const UINT remapped=UINT(equal.size());equal.push_back(true);
    ranges.push_back({remapped,{a,b},{1025,1281},2037});
    const UINT reverse=UINT(equal.size());equal.push_back(false);
    ranges.push_back({reverse,{b,a},{11998,11998},2});
    GpuGeometryCompare enabled,disabled;
    Check(enabled.Create(device.Get(),UINT(equal.size()),ranges,gate.Get(),8));
    Check(disabled.Create(device.Get(),UINT(equal.size()),ranges,gate.Get(),0));
    std::array<GpuGeometryCompare,3> chained;
    const std::array<GeometryCompareRange,1> equalRange{{{0,{a,b},{500,500},65}}};
    Check(chained[0].Create(device.Get(),1,equalRange,enabled.Predicates(),GpuGeometryCompare::Offset(0)));
    Check(chained[1].Create(device.Get(),1,equalRange,enabled.Predicates(),GpuGeometryCompare::Offset(3)));
    Check(chained[2].Create(device.Get(),1,equalRange,disabled.Predicates(),GpuGeometryCompare::Offset(0)));
    GpuGeometryCompare tiled;
    const std::vector<GeometryCompareRange> tiledRanges(1025,GeometryCompareRange{0,{a,b},{5000,5000},1024});
    Check(tiled.Create(device.Get(),1,tiledRanges,gate.Get(),8));
    const auto rejected=[&](UINT pairs,std::span<const GeometryCompareRange> values,UINT64 offset){GpuGeometryCompare invalid;
        Require(FAILED(invalid.Create(device.Get(),pairs,values,gate.Get(),offset)),"Invalid geometry compare input admitted");};
    rejected(0,ranges,0);rejected(UINT(equal.size()+1),ranges,0);rejected(UINT(equal.size()),ranges,1);
    auto bad=ranges;bad[0].bytes=0;rejected(UINT(equal.size()),bad,0);
    bad=ranges;bad[0].offsets[0]=width;rejected(UINT(equal.size()),bad,0);
    bad=ranges;bad[0].pair=UINT(equal.size());rejected(UINT(equal.size()),bad,0);
    bad=ranges;bad[0].bytes=UINT64(UINT_MAX)+1;rejected(UINT(equal.size()),bad,0);
    list->CopyBufferRegion(readback.Get(),0,zero.Get(),8,8184);
    // Both sources are produced and one is modified on the GPU in THIS list.
    // Equality, fallback and consumption must all work before any CPU fence wait.
    list->CopyBufferRegion(a.Get(),0,upload.Get(),0,width);list->CopyBufferRegion(b.Get(),0,upload.Get(),0,width);
    list->CopyBufferRegion(b.Get(),100,changes.Get(),100,1);list->CopyBufferRegion(b.Get(),11999,changes.Get(),11999,1);
    std::array<D3D12_RESOURCE_BARRIER,2> barriers{};
    for(UINT i=0;i<2;++i){barriers[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition={i?b.Get():a.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_GENERIC_READ};}
    list->ResourceBarrier(2,barriers.data());
    Require(enabled.Record(list.Get()) && disabled.Record(list.Get()),"GPU comparison recording failed");
    Require(tiled.Record(list.Get()),"Two-dimensional comparison dispatch failed");
    list->CopyBufferRegion(readback.Get(),4240,tiled.Predicates(),0,16);
    for(UINT i=0;i<chained.size();++i){Require(chained[i].Record(list.Get()),"GPU-produced predicate could not gate dependent comparison");
        list->CopyBufferRegion(readback.Get(),4096+i*16,chained[i].Predicates(),0,16);
        list->SetPredication(chained[i].Predicates(),0,D3D12_PREDICATION_OP_EQUAL_ZERO);
        list->CopyBufferRegion(readback.Get(),4160+i*8,zero.Get(),0,8);
        list->SetPredication(nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
    }
    Require(!enabled.Record(list.Get()),"Single-use equality batch recorded twice");
    const UINT64 predicateBytes=equal.size()*16;
    list->CopyBufferRegion(readback.Get(),0,enabled.Predicates(),0,predicateBytes);
    list->CopyBufferRegion(readback.Get(),1024,disabled.Predicates(),0,predicateBytes);
    // Actually execute both conditional branches, not just inspect shader output.
    for(UINT pair=0;pair<equal.size();++pair)for(UINT fallback=0;fallback<2;++fallback){
        list->SetPredication(enabled.Predicates(),GpuGeometryCompare::Offset(pair,fallback!=0),D3D12_PREDICATION_OP_EQUAL_ZERO);
        list->CopyBufferRegion(readback.Get(),2048+pair*16+fallback*8,zero.Get(),0,8);
        list->SetPredication(disabled.Predicates(),GpuGeometryCompare::Offset(pair,fallback!=0),D3D12_PREDICATION_OP_EQUAL_ZERO);
        list->CopyBufferRegion(readback.Get(),3072+pair*16+fallback*8,zero.Get(),0,8);
    }
    list->SetPredication(nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);Check(list->Close());
    // The existing native pre-submit validator enables this only after recording.
    Check(gate->Map(0,&none,&mapped));static_cast<uint64_t*>(mapped)[1]=1;gate->Unmap(0,nullptr);
    ID3D12CommandList* raw=list.Get();queue->ExecuteCommandLists(1,&raw);
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));Check(queue->Signal(fence.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event unavailable");
    Check(fence->SetEventOnCompletion(1,event));const auto waited=WaitForSingleObject(event,10000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"Geometry comparison timeout");
    const D3D12_RANGE read{0,8192};Check(readback->Map(0,&read,&mapped));const auto* result=static_cast<const uint64_t*>(mapped);
    for(UINT i=0;i<equal.size();++i){
        Require(result[i*2]==UINT64(equal[i]) && result[i*2+1]==UINT64(!equal[i]),"Equality/fallback predicates differ");
        Require(!result[128+i*2] && !result[128+i*2+1],"Disabled gate enabled a branch");
        Require(result[256+i*2]==(equal[i]?0xABCDEFull:0) && result[256+i*2+1]==(equal[i]?0:0xABCDEFull),"Predicated branch did not execute exactly once");
        Require(!result[384+i*2] && !result[384+i*2+1],"Disabled branch executed");
    }
    for(UINT i=0;i<chained.size();++i){Require(result[512+i*2]==(i==0?1:0) && !result[512+i*2+1],"GPU selector rejection failed to disable dependent branches");
        Require(result[520+i]==(i==0?0xABCDEFull:0),"GPU-gated consumer ran with rejected or unsubmitted source inputs");}
    Require(result[530]==1 && !result[531] && tiled.RangeCount()==1025 && tiled.DispatchCount()==2,"Batched two-dimensional coverage differs");
    readback->Unmap(0,&none);
    Require(enabled.PairCount()==equal.size() && enabled.RangeCount()==ranges.size() && enabled.DispatchCount()==3,"Resource-pair batch accounting differs");
    if(messages)for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T size{};Check(messages->GetMessage(i,nullptr,&size));
        std::vector<uint8_t> storage(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());Check(messages->GetMessage(i,message,&size));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("Geometry comparison debug error");}}
    cvr::gpu::ResetCommandResources(list.Get());
    std::puts("PASS GPU-only geometry equality, multiple ranges, alignment/tails, late permission, complementary fallback and rejected input bounds");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
