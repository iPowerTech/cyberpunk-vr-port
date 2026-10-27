#include "Render/RegisteredConstantUpload.hpp"
#include "Render/ConstantUploadRegistry.hpp"
#include "Render/CommandResources.hpp"
#include "Render/RenderProbeScope.hpp"
#include "Render/NativeTextureRead.hpp"
#include "Render/NativeIaSnapshot.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace cvr::stereo;
namespace cvr::stereo::native_probe {bool ObserveBindings(){return true;}}
namespace cvr::stereo::families {bool Enabled(){return true;}}
namespace cvr::stereo::camera_upload {
Context CurrentContext(uintptr_t){throw std::runtime_error("Unexpected native context in placement-only test");}
}
namespace cvr::stereo::native_recording {
bool Capture(ID3D12GraphicsCommandList*,camera_upload::Context,Lease&){throw std::runtime_error("Unexpected native capture in placement-only test");}
bool ValidateRecording(const Lease&,ID3D12GraphicsCommandList*,camera_upload::Context){throw std::runtime_error("Unexpected native recording in placement-only test");}
bool ValidateSubmission(const Lease&,ID3D12CommandQueue*){throw std::runtime_error("Unexpected native submission in placement-only test");}
}
namespace cvr::stereo::ia_snapshot {
bool ReadPinnedPlacedOffset(const detail::PlacedAllocation&,detail::PlacedAllocation&){throw std::runtime_error("Unexpected native allocation in placement-only test");}
}
extern "C" std::atomic<uint32_t> CyberpunkVR_NativeStereoProbeState{1};
extern "C" {
std::atomic<uint32_t> CyberpunkVR_ConstantCaptureEnable{},CyberpunkVR_ConstantRegisteredTypes[6]{},CyberpunkVR_ConstantRegisteredRejects{};
}
namespace cvr::stereo::constants {
ID3D12Resource* RegisteredSource(uint32_t,uint32_t){throw std::runtime_error("Unexpected upload path in creation-only test");}
bool ReadUpload(uint64_t,void*,size_t){throw std::runtime_error("Unexpected upload read in creation-only test");}
}
namespace cvr::gpu {
bool KeepCommandResources(ID3D12GraphicsCommandList*,std::initializer_list<IUnknown*>){throw std::runtime_error("Unexpected submission retention in creation-only test");}
}
static void Require(bool value,const char* message,const std::source_location at=std::source_location::current()) {
    if(!value)throw std::runtime_error(std::string(message)+" at line "+std::to_string(at.line()));
}
static void Check(HRESULT result){Require(SUCCEEDED(result),"D3D12 setup/metadata operation failed");}
class Sentinel final:public IUnknown {
    std::atomic<ULONG> refs_{1};std::atomic<UINT>& destroyed_;
public:
    explicit Sentinel(std::atomic<UINT>& destroyed):destroyed_(destroyed){}
    ~Sentinel(){++destroyed_;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** output)override{if(!output)return E_POINTER;*output=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*output=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto left=--refs_;if(!left)delete this;return left;}
};
constexpr GUID CreationKey{0x4d3815bb,0x3885,0x463d,{0x81,0xc3,0x48,0x32,0x2a,0xcb,0x62,0x53}};
constexpr GUID HeapOwnerKey{0x4d3815bb,0x3885,0x463d,{0x81,0xc3,0x48,0x32,0x2a,0xcb,0x62,0x52}};
constexpr GUID LifetimeKey{0x929ac8c1,0x5e15,0x4472,{0x99,0x9b,0x11,0x4d,0x2b,0x59,0xf8,0x61}};
static D3D12_RESOURCE_DESC BufferDesc(){D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=256;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;return desc;}
static ComPtr<ID3D12Resource> Committed(ID3D12Device* device){D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    const auto desc=BufferDesc();ComPtr<ID3D12Resource> result;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&result)));return result;}
static void Reject(const ComPtr<ID3D12Resource>& resource){constants::ResourceCreationInfo output;output.kind=99;output.resource=resource;
    Require(!constants::QueryResourceCreation(resource,output),"Unknown/corrupt creation receipt accepted");
    Require(!output.kind && !output.initialStateKnown && !output.resource && !output.heap && !output.heapOffset && !output.allocationBytes,"Failed query published stale ownership");}
static void ObservedAllocations(ID3D12Device* device){
    constexpr GUID placementKey{0xe9165914,0x664a,0x4174,{0xb1,0x1b,0x20,0x3a,0xad,0xd5,0x34,0x88}};
    constexpr GUID heapKey{0xe9165914,0x664a,0x4174,{0xb1,0x1b,0x20,0x3a,0xad,0xd5,0x34,0x89}};
    auto resource=Committed(device);constants::ResourceCreationInfo info;
    const auto reject=[&](const ComPtr<ID3D12Resource>& value){info.kind=99;info.resource=value;
        Require(!texture_read::QueryObservedAllocation(value,info) && !info.resource && !info.heap && !info.kind,"Invalid observed placement accepted or stale output retained");};
    reject(resource);
    {probe::Scope internal;texture_read::ResourceCreated(resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,false);}
    Require(texture_read::QueryObservedAllocation(resource,info) && info.resource==resource && info.kind==1 &&
        !info.initialStateKnown && !info.heap,"Private committed placement observation failed");
    Require(texture_read::QueryObservedAllocation(info.resource,info),"Aliased observed query lost resource");
    const auto desc=BufferDesc();const auto allocation=device->GetResourceAllocationInfo(0,1,&desc);
    D3D12_HEAP_DESC hd{};hd.SizeInBytes=allocation.SizeInBytes+allocation.Alignment;hd.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    hd.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;ComPtr<ID3D12Heap> heap;Check(device->CreateHeap(&hd,IID_PPV_ARGS(&heap)));
    ComPtr<ID3D12Resource> placed;Check(device->CreatePlacedResource(heap.Get(),allocation.Alignment,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&placed)));
    {probe::Scope internal;texture_read::ResourceCreated(placed.Get(),D3D12_RESOURCE_STATE_COPY_DEST,true,heap.Get(),allocation.Alignment,allocation.SizeInBytes);}
    Require(texture_read::QueryObservedAllocation(placed,info) && info.kind==2 && info.heap==heap &&
        info.heapOffset==allocation.Alignment && info.allocationBytes==allocation.SizeInBytes && !info.initialStateKnown,"Private placed observation failed");
    UINT bytes{};Check(placed->GetPrivateData(placementKey,&bytes,nullptr));std::vector<uint8_t> original(bytes);
    Check(placed->GetPrivateData(placementKey,&bytes,original.data()));
    for(const UINT64 offset:{UINT64_MAX,UINT64(1),hd.SizeInBytes}){auto bad=original;std::memcpy(bad.data()+8,&offset,8);
        Check(placed->SetPrivateData(placementKey,UINT(bad.size()),bad.data()));reject(placed);}
    for(const UINT64 size:{UINT64(0),UINT64_MAX,allocation.SizeInBytes-1,hd.SizeInBytes}){auto bad=original;std::memcpy(bad.data()+16,&size,8);
        Check(placed->SetPrivateData(placementKey,UINT(bad.size()),bad.data()));reject(placed);}
    Check(placed->SetPrivateData(placementKey,UINT(original.size()),original.data()));
    Check(placed->SetPrivateDataInterface(heapKey,nullptr));reject(placed);
    Check(placed->SetPrivateDataInterface(heapKey,heap.Get()));
    Require(texture_read::QueryObservedAllocation(placed,info),"Restored observed placement rejected");
    heap.Reset();placed.Reset();Require(info.heap && info.resource,"Observed placement failed to hold live COM objects");info={};
}

int main()try {
    std::atomic<UINT> heapDestroyed{};
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> adapter;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ObservedAllocations(device.Get());
    constants::ResourceCreationInfo info;ComPtr<ID3D12Resource> missing;Reject(missing);
    auto resource=Committed(device.Get());Reject(resource);
    constants::RegisteredResourceCreated(resource.Get(),false);
    Require(constants::QueryResourceCreation(resource,info) && info.kind==1 && !info.initialStateKnown && info.resource==resource &&
        !info.heap && !info.heapOffset && !info.allocationBytes,"Old committed caller did not preserve unknown initial state");
    constants::RegisteredResourceCreated(resource.Get(),false,nullptr,0,0,D3D12_RESOURCE_STATE_COPY_DEST);
    Require(constants::QueryResourceCreation(resource,info) && info.initialStateKnown && info.initialState==D3D12_RESOURCE_STATE_COPY_DEST,"Committed creation state was lost");
    constants::RegisteredResourceCreated(resource.Get(),false,nullptr,0,0,D3D12_RESOURCE_STATE_COMMON);
    Require(constants::QueryResourceCreation(resource,info) && info.initialStateKnown && info.initialState==D3D12_RESOURCE_STATE_COMMON,"Known COMMON was confused with unknown state");
    Require(constants::QueryResourceCreation(info.resource,info) && info.resource==resource,"Aliased query input/output lost its held resource");
    UINT bytes{};Check(resource->GetPrivateData(CreationKey,&bytes,nullptr));Require(bytes==56,"Creation receipt ABI changed unexpectedly");
    std::vector<uint8_t> original(bytes);Check(resource->GetPrivateData(CreationKey,&bytes,original.data()));
    for(UINT index=0;index<original.size();++index){auto changed=original;changed[index]^=1;Check(resource->SetPrivateData(CreationKey,UINT(changed.size()),changed.data()));Reject(resource);}
    Check(resource->SetPrivateData(CreationKey,UINT(original.size()-1),original.data()));Reject(resource);
    Check(resource->SetPrivateData(CreationKey,UINT(original.size()),original.data()));
    Require(constants::QueryResourceCreation(resource,info),"Restoring the exact creation receipt failed");
    auto otherResource=Committed(device.Get());Check(otherResource->SetPrivateData(CreationKey,UINT(original.size()),original.data()));Reject(otherResource);
    Check(resource->SetPrivateData(CreationKey,0,nullptr));Reject(resource);
    Check(otherResource->SetPrivateData(CreationKey,0,nullptr));
    {probe::Scope internal;constants::RegisteredResourceCreated(otherResource.Get(),false,nullptr,0,0,D3D12_RESOURCE_STATE_COPY_DEST);}Reject(otherResource);
    ComPtr<IUnknown> sentinel;sentinel.Attach(new Sentinel(heapDestroyed));
    const auto desc=BufferDesc();const auto allocation=device->GetResourceAllocationInfo(0,1,&desc);
    Require(allocation.SizeInBytes && allocation.SizeInBytes!=UINT64_MAX && allocation.Alignment,"Placed allocation description failed");
    D3D12_HEAP_DESC heapDesc{};heapDesc.SizeInBytes=allocation.SizeInBytes+2*allocation.Alignment;heapDesc.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    heapDesc.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;ComPtr<ID3D12Heap> heap;Check(device->CreateHeap(&heapDesc,IID_PPV_ARGS(&heap)));
    Check(heap->SetPrivateDataInterface(LifetimeKey,sentinel.Get()));sentinel.Reset();
    ComPtr<ID3D12Resource> placed;Check(device->CreatePlacedResource(heap.Get(),allocation.Alignment,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&placed)));
    constants::RegisteredResourceCreated(placed.Get(),true,heap.Get(),allocation.Alignment,allocation.SizeInBytes,D3D12_RESOURCE_STATE_COPY_DEST);
    Require(constants::QueryResourceCreation(placed,info) && info.kind==2 && info.initialStateKnown && info.initialState==D3D12_RESOURCE_STATE_COPY_DEST &&
        info.heap==heap && info.heapOffset==allocation.Alignment && info.allocationBytes==allocation.SizeInBytes,"Placed API receipt query changed heap/span/state");
    for(UINT n=0;n<100;++n){constants::ResourceCreationInfo repeated;Require(constants::QueryResourceCreation(placed,repeated) && repeated.heap==heap,"Repeated heap interface query failed");}
    info={};Check(placed->SetPrivateDataInterface(HeapOwnerKey,nullptr));Reject(placed);
    constants::RegisteredResourceCreated(placed.Get(),true,heap.Get(),allocation.Alignment,allocation.SizeInBytes);
    Require(constants::QueryResourceCreation(placed,info) && !info.initialStateKnown,"Placed old/enhanced caller gained an inferred state");info={};
    ComPtr<ID3D12Heap> differentHeap;Check(device->CreateHeap(&heapDesc,IID_PPV_ARGS(&differentHeap)));
    Check(placed->SetPrivateDataInterface(HeapOwnerKey,differentHeap.Get()));Reject(placed);
    for(const auto badOffset:{UINT64_MAX,uint64_t(1),heapDesc.SizeInBytes}){
        constants::RegisteredResourceCreated(placed.Get(),true,heap.Get(),badOffset,allocation.SizeInBytes);Reject(placed);}
    for(const auto badBytes:{uint64_t(0),UINT64_MAX,allocation.SizeInBytes-1,heapDesc.SizeInBytes}){
        constants::RegisteredResourceCreated(placed.Get(),true,heap.Get(),allocation.Alignment,badBytes);Reject(placed);}
    constants::RegisteredResourceCreated(placed.Get(),true,nullptr,allocation.Alignment,allocation.SizeInBytes);Reject(placed);
    constants::RegisteredResourceCreated(placed.Get(),true,heap.Get(),allocation.Alignment,allocation.SizeInBytes,D3D12_RESOURCE_STATE_COPY_DEST);
    heap.Reset();Require(!heapDestroyed,"Placed private API receipt did not retain the actual heap");
    Require(constants::QueryResourceCreation(placed,info) && info.heap,"Query could not retain a released caller's heap");placed.Reset();
    Require(!heapDestroyed,"Held creation info lost its resource/heap lifetime");info.resource.Reset();
    Require(!heapDestroyed,"Heap interface retrieval failed to transfer its COM reference");info.heap.Reset();Require(heapDestroyed==1,"Creation queries leaked or failed to retain the placement heap");
    ComPtr<ID3D12InfoQueue> messages;Check(device.As(&messages));for(UINT64 n=0;n<messages->GetNumStoredMessages();++n){SIZE_T size{};Check(messages->GetMessage(n,nullptr,&size));std::vector<char> storage(size);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());Check(messages->GetMessage(n,message,&size));if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("D3D12 validation error");}}
    std::puts("PASS current resource API creation receipt: committed/placed kind, known/unknown historical state, missing/corrupt metadata, placement bounds, transferred heap COM lifetime; no byte/alias/order/copy permission");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
