#include "Render/GpuResidencyLease.hpp"
#include "Render/NativeTextureRead.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace residency=cvr::gpu::residency;
namespace texture=cvr::stereo::texture_read;
namespace target=cvr::stereo::target_lifetime;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Residency fixture API call failed");}

static void Transactions(){
    using residency::State;using residency::detail::Transaction;
    UINT makes{};std::vector<size_t> evictions;
    const auto make=[&]{++makes;return S_OK;};
    const auto evict=[&](size_t index){evictions.push_back(index);return S_OK;};
    Transaction cancelled;
    Require(cancelled.Acquire(0,make)==E_INVALIDARG && !makes,"Empty acquisition called MakeResident");
    Check(cancelled.Acquire(3,make));
    Require(cancelled.Acquire(3,make)==E_INVALIDARG && makes==1,"Repeated acquisition added residency references");
    Require(cancelled.AssociateFence(1,S_OK)==E_UNEXPECTED,"Fence preceded potential submission");
    Check(cancelled.Cancel(evict));
    Require(cancelled.Status()==State::Retired && evictions==std::vector<size_t>{0,1,2},"Cancellation did not balance every acquired object once");
    Require(FAILED(cancelled.Cancel(evict)) && !cancelled.MarkSubmitted() && evictions.size()==3,"Retired references were reused or evicted twice");

    Transaction failed;evictions.clear();
    Require(failed.Acquire(3,[]{return E_OUTOFMEMORY;})==E_OUTOFMEMORY && failed.Status()==State::Empty && !failed.ObjectCount(),
        "Failed atomic MakeResident acquired partial references");
    Require(FAILED(failed.Cancel(evict)) && evictions.empty(),"Failed MakeResident was compensated with Evict");
    Check(failed.Acquire(1,make));Check(failed.Cancel(evict));

    Transaction submitted;evictions.clear();Check(submitted.Acquire(2,make));
    Require(submitted.MarkSubmitted() && !submitted.MarkSubmitted(),"Submission was not a one-time transition");
    Require(FAILED(submitted.Cancel(evict)) && evictions.empty(),"Potentially submitted work was cancelled");
    Check(submitted.AssociateFence(5,S_OK));
    Require(submitted.AssociateFence(6,S_OK)==E_UNEXPECTED,"Associated completion evidence was replaced");
    Require(submitted.Retire(4,evict)==S_FALSE && evictions.empty(),"Incomplete GPU work was evicted");
    Check(submitted.Retire(5,evict));
    Require(submitted.Status()==State::Retired && evictions==std::vector<size_t>{0,1},"Completed work was not balanced");
    Require(FAILED(submitted.Retire(6,evict)) && evictions.size()==2,"Completed references were evicted twice");

    for(UINT failure=0;failure<4;++failure){Transaction uncertain;evictions.clear();Check(uncertain.Acquire(2,make));
        Require(uncertain.MarkSubmitted(),"Uncertain submission setup failed");
        if(failure==0)Require(FAILED(uncertain.AssociateFence(1,E_FAIL)),"Failed Signal acquired a fence");
        if(failure==1)Require(FAILED(uncertain.AssociateFence(0,S_OK)),"Zero fence value acquired completion");
        if(failure==2)Require(FAILED(uncertain.AssociateFence(UINT64_MAX,S_OK)),"Removed-device sentinel became a signal");
        if(failure==3)Require(FAILED(uncertain.Retire(0,evict)),"Missing completion fence was ignored");
        Require(uncertain.Status()==State::Quarantined && FAILED(uncertain.Cancel(evict)) &&
            FAILED(uncertain.Retire(UINT64_MAX-1,evict)) && evictions.empty(),"Uncertain submission released residency");
    }
    Transaction removed;evictions.clear();Check(removed.Acquire(2,make));Require(removed.MarkSubmitted(),"Device removal setup failed");
    Check(removed.AssociateFence(7,S_OK));
    Require(removed.Retire(UINT64_MAX,evict)==DXGI_ERROR_DEVICE_REMOVED && removed.Status()==State::Quarantined && evictions.empty(),
        "Device removal was treated as completed GPU work");

    Transaction evictionFailure;evictions.clear();Check(evictionFailure.Acquire(3,make));
    const auto partial=[&](size_t index){evictions.push_back(index);return index==1?E_FAIL:S_OK;};
    Require(evictionFailure.Cancel(partial)==E_FAIL && evictionFailure.Status()==State::Quarantined &&
        evictions==std::vector<size_t>{0,1,2},"Eviction failure lost per-object attempt accounting");
    Require(FAILED(evictionFailure.Cancel(partial)) && evictions.size()==3,"Uncertain eviction was retried");
    Transaction thrown;evictions.clear();
    Require(FAILED(thrown.Acquire(1,[]()->HRESULT{throw std::runtime_error("unknown SDK outcome");})) &&
        thrown.Status()==State::Quarantined && FAILED(thrown.Cancel(evict)) && evictions.empty(),"Unknown acquisition outcome released residency");
    Require(!target::detail::SameBorrowedIdentity({},{}),"Empty public target metadata became an identity match");
    residency::Lease absent;const std::array<residency::BorrowedTarget,1> emptyTarget{};
    Require(absent.AcquireBorrowedTargets(nullptr,emptyTarget)==E_INVALIDARG && absent.Status()==State::Empty &&
        !absent.SourceCount() && !absent.ObjectCount(),"Empty target acquisition changed residency state");
    std::array<residency::Lease*,1> emptyGroup{&absent};
    Require(residency::Lease::MarkSubmittedTogether(nullptr,emptyGroup)==E_INVALIDARG && absent.Status()==State::Empty,
        "Null queue changed an unacquired grouped member");
}

struct Pin final:IUnknown {
    ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs;}
};
struct FixtureMapping {Pin* pin{};ID3D12Resource* resource{};ID3D12Pageable* pageable{};uint32_t handle{};};
static std::vector<FixtureMapping> fixtureMappings;
static UINT borrowedAuthenticationAttempts{};
// This target links GpuResidencyLease.cpp without NativeTextureRead.cpp. The
// explicit fixture mapping replaces only authentication, never SDK residency.
namespace cvr::stereo::texture_read {
bool GetImmutablePageable(ID3D12Device* device,const ImmutableLease& source,ImmutablePageable& output){
    output={};if(!device)return false;
    for(const auto& mapped:fixtureMappings)if(source.owner.Get()==mapped.pin && source.source.resource.Get()==mapped.resource &&
        source.source.native.registry==1 && source.source.native.resource==reinterpret_cast<uintptr_t>(mapped.resource) &&
        source.source.native.handle==mapped.handle && !source.source.native.reserved && source.source.receipt==mapped.handle+100 &&
        source.source.owner==reinterpret_cast<uintptr_t>(mapped.pin)){
        ComPtr<ID3D12Device> resourceDevice,pageableDevice;
        if(FAILED(mapped.resource->GetDevice(IID_PPV_ARGS(&resourceDevice))) || FAILED(mapped.pageable->GetDevice(IID_PPV_ARGS(&pageableDevice))) ||
            resourceDevice.Get()!=device || pageableDevice.Get()!=device)return false;
        output={source,mapped.pageable};return true;
    }
    return false;
}
}
// This fixture cannot create a real native borrowed scope. Reject all target
// requests rather than fabricating positive native authority. The real getter
// is verified by the in-game native invocation tests, not this SDK fixture.
namespace cvr::stereo::target_lifetime {
bool GetBorrowedPageable(ID3D12Device*,const texture_read::WritableTargetIdentity&,const BorrowedScope&,BorrowedPageable& output){
    ++borrowedAuthenticationAttempts;output={};return false;
}
}
static D3D12_RESOURCE_DESC Buffer(UINT64 bytes){
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;return desc;
}
static ComPtr<ID3D12Resource> Committed(ID3D12Device* device,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
    const auto desc=Buffer(bytes);D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&resource)));return resource;
}
static texture::ImmutableLease Source(const FixtureMapping& mapping){
    return {mapping.pin,{mapping.resource,{1,reinterpret_cast<uintptr_t>(mapping.resource),mapping.handle,0},
        uint64_t(mapping.handle)+100,reinterpret_cast<uintptr_t>(mapping.pin)}};
}
static void Gpu(bool hardware){
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<ID3D12Device> device;
    if(hardware){
        for(UINT index=0;!device;++index){ComPtr<IDXGIAdapter1> adapter;const auto result=factory->EnumAdapters1(index,&adapter);
            if(result==DXGI_ERROR_NOT_FOUND)break;Check(result);
            DXGI_ADAPTER_DESC1 desc{};Check(adapter->GetDesc1(&desc));if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
            D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device));
        }
        Require(bool(device),"No hardware adapter supports the residency fixture");
    }else{ComPtr<IDXGIAdapter> warp;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        Check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));}
    ComPtr<ID3D12CommandQueue> queue;const D3D12_COMMAND_QUEUE_DESC queueDesc{D3D12_COMMAND_LIST_TYPE_DIRECT};
    Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    auto committed=Committed(device.Get(),256,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto upload=Committed(device.Get(),768,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto readback=Committed(device.Get(),768,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    const auto desc=Buffer(256);const auto allocation=device->GetResourceAllocationInfo(0,1,&desc);
    Require(allocation.SizeInBytes && allocation.SizeInBytes!=UINT64_MAX && allocation.SizeInBytes<=UINT64_MAX/2,"Invalid fixture placement size");
    D3D12_HEAP_DESC heapDesc{};heapDesc.SizeInBytes=allocation.SizeInBytes*2;heapDesc.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    heapDesc.Alignment=allocation.Alignment;heapDesc.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    ComPtr<ID3D12Heap> heap;Check(device->CreateHeap(&heapDesc,IID_PPV_ARGS(&heap)));
    std::array<ComPtr<ID3D12Resource>,2> placed;
    for(UINT index=0;index<2;++index)Check(device->CreatePlacedResource(heap.Get(),index*allocation.SizeInBytes,&desc,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&placed[index])));
    std::array<uint8_t,768> expected{};for(size_t index=0;index<expected.size();++index)expected[index]=uint8_t(index*17+index/256);
    void* mapped{};const D3D12_RANGE none{};Check(upload->Map(0,&none,&mapped));std::memcpy(mapped,expected.data(),expected.size());
    const D3D12_RANGE written{0,expected.size()};upload->Unmap(0,&written);
    Pin committedPin,placedPinA,placedPinB;
    fixtureMappings={{&committedPin,committed.Get(),committed.Get(),1},{&placedPinA,placed[0].Get(),heap.Get(),2},{&placedPinB,placed[1].Get(),heap.Get(),3}};
    struct ClearMappings {~ClearMappings(){fixtureMappings.clear();}} clearMappings;
    std::vector<texture::ImmutableLease> sources;for(const auto& mapping:fixtureMappings)sources.push_back(Source(mapping));
    auto bad=sources;++bad.front().source.native.handle;residency::Lease rejected;
    Require(FAILED(rejected.Acquire(device.Get(),bad)) && rejected.Status()==residency::State::Empty && residency::Healthy(),
        "Unmapped fixture owner acquired residency");bad.clear();
    std::array<residency::BorrowedTarget,2> borrowed{};
    auto& identity=borrowed[0].identity;identity.creation={2,true,D3D12_RESOURCE_STATE_COPY_DEST,placed[0],heap,0,allocation.SizeInBytes};
    identity.native={1,reinterpret_cast<uintptr_t>(placed[0].Get()),2,0};identity.recording={&placedPinA,7,8,9,1};
    identity.originalContext={8,10,9};identity.resourceDesc=placed[0]->GetDesc();identity.heapDesc=heap->GetDesc();
    identity.resourceHeapProperties=identity.heapDesc.Properties;identity.resourceHeapFlags=identity.heapDesc.Flags;
    identity.expectedWritableState=D3D12_RESOURCE_STATE_COPY_DEST;identity.allocatorOwnerObservation=11;
    std::memcpy(identity.nativeRecord.data(),&identity.native.resource,8);identity.nativeRecord[8]=2;
    std::memcpy(identity.nativeRecord.data()+0x10,&identity.allocatorOwnerObservation,8);
    // Equality of public metadata is not authentication. The raw snapshots may
    // differ outside the native kind/resource/owner identity fields.
    auto same=identity;same.nativeRecord[0x40]^=1;
    Require(target::detail::SameBorrowedIdentity(identity,same),"Mutable non-identity native bytes prevented legitimate target pairing");
    auto different=same;different.native.handle++;
    Require(!target::detail::SameBorrowedIdentity(identity,different),"Another native handle matched the borrowed target");
    different=same;different.recording.generation++;
    Require(!target::detail::SameBorrowedIdentity(identity,different),"Another recording generation matched the borrowed target");
    different=same;different.creation.heapOffset=allocation.SizeInBytes;
    Require(!target::detail::SameBorrowedIdentity(identity,different),"Another placement matched the borrowed target");
    different=same;different.nativeRecord[0x10]^=1;
    Require(!target::detail::SameBorrowedIdentity(identity,different),"Another native allocator owner matched the borrowed target");
    different=same;different.creation.resource=placed[1];
    Require(!target::detail::SameBorrowedIdentity(identity,different),"Another resource matched the borrowed target");
    target::BorrowedPageable untrusted;Require(!target::GetBorrowedPageable(device.Get(),identity,borrowed[0].lifetime,untrusted) &&
        !untrusted.pageable && !untrusted.lifetime,"Public target fields forged a live borrowed scope");
    residency::Lease borrowedRejected;const auto beforeAuthentication=borrowedAuthenticationAttempts;
    Require(borrowedRejected.AcquireBorrowedTargets(device.Get(),borrowed)==E_ACCESSDENIED &&
        borrowedAuthenticationAttempts==beforeAuthentication+1 && borrowedRejected.Status()==residency::State::Empty &&
        !borrowedRejected.ObjectCount() && !borrowedRejected.SourceCount() && residency::Healthy(),
        "Unauthenticated borrowed targets acquired SDK residency or poisoned accounting");
    Require(borrowedRejected.AcquireBorrowedTargets(device.Get(),{})==E_INVALIDARG &&
        borrowedAuthenticationAttempts==beforeAuthentication+1,"Empty target span reached native authentication");
    borrowed={};same={};different={};
    ID3D12Pageable* pageables[]{committed.Get(),heap.Get()};Check(device->Evict(2,pageables));
    residency::Lease first;Check(first.Acquire(device.Get(),sources));
    Require(first.ObjectCount()==2 && first.SourceCount()==3,"Shared placed heap was not deduplicated while retaining every source");
    const auto beforeMixed=borrowedAuthenticationAttempts;
    Require(first.AcquireBorrowedTargets(device.Get(),borrowed)==E_INVALIDARG && borrowedAuthenticationAttempts==beforeMixed &&
        first.Status()==residency::State::Acquired && first.ObjectCount()==2 && first.SourceCount()==3,
        "Borrowed targets were mixed into an existing material residency transaction");
    residency::Lease lease(std::move(first));Require(first.Status()==residency::State::Empty,"Moved residency lease retained an acquisition");
    sources.clear();Require(committedPin.refs==2 && placedPinA.refs==2 && placedPinB.refs==2,"Residency lease dropped native allocation pins");
    ID3D12Resource* inputs[]{committed.Get(),placed[0].Get(),placed[1].Get()};std::array<D3D12_RESOURCE_BARRIER,3> barriers{};
    for(UINT index=0;index<3;++index){list->CopyBufferRegion(inputs[index],0,upload.Get(),index*256,256);
        barriers[index].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[index].Transition={inputs[index],D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};}
    list->ResourceBarrier(UINT(barriers.size()),barriers.data());
    for(UINT index=0;index<3;++index)list->CopyBufferRegion(readback.Get(),index*256,inputs[index],0,256);
    Check(list->Close());Check(lease.MarkSubmitted(queue.Get()));ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);
    const auto signaled=queue->Signal(fence.Get(),1);Check(lease.AssociateFence(queue.Get(),fence.Get(),1,signaled));
    struct Event {HANDLE value=CreateEventW(nullptr,FALSE,FALSE,nullptr);~Event(){if(value)CloseHandle(value);}} event;
    Require(bool(event.value),"Fixture completion event failed");Check(fence->SetEventOnCompletion(1,event.value));
    Require(WaitForSingleObject(event.value,10000)==WAIT_OBJECT_0,"Fixture GPU work did not finish");
    Check(lease.Retire());Require(lease.Status()==residency::State::Retired && committedPin.refs==1 && placedPinA.refs==1 && placedPinB.refs==1,
        "Completed residency lease retained or released the wrong native pins");
    const D3D12_RANGE read{0,expected.size()};Check(readback->Map(0,&read,&mapped));
    const bool equal=std::memcmp(mapped,expected.data(),expected.size())==0;readback->Unmap(0,&none);
    Require(equal,"Resident committed/placed GPU copy bytes differ");
    for(const auto& mapping:fixtureMappings)sources.push_back(Source(mapping));
    residency::Lease cancelled;Check(cancelled.Acquire(device.Get(),sources));Check(cancelled.Cancel());
    Require(cancelled.Status()==residency::State::Retired && residency::Healthy() && !residency::QuarantinedCount(),"Pre-submit cancellation poisoned residency");
    residency::Lease groupedFirst,groupedSecond,unacquired;
    Check(groupedFirst.Acquire(device.Get(),std::span(sources).first(1)));
    std::array<residency::Lease*,2> laterFailure{&groupedFirst,&unacquired};
    Require(residency::Lease::MarkSubmittedTogether(queue.Get(),laterFailure)==E_INVALIDARG &&
        groupedFirst.Status()==residency::State::Acquired && unacquired.Status()==residency::State::Empty,
        "A later member failure committed the first member");
    Check(groupedFirst.Cancel());Require(groupedFirst.Status()==residency::State::Retired && residency::Healthy(),
        "Refused grouped submission was not cancelable without quarantine");
    Check(groupedFirst.Acquire(device.Get(),std::span(sources).first(1)));
    Check(groupedSecond.Acquire(device.Get(),std::span(sources).subspan(1)));
    std::array<residency::Lease*,2> duplicates{&groupedFirst,&groupedFirst},nullMember{&groupedFirst,nullptr};
    Require(residency::Lease::MarkSubmittedTogether(queue.Get(),duplicates)==E_INVALIDARG &&
        residency::Lease::MarkSubmittedTogether(queue.Get(),nullMember)==E_INVALIDARG &&
        residency::Lease::MarkSubmittedTogether(queue.Get(),{})==E_INVALIDARG &&
        groupedFirst.Status()==residency::State::Acquired && groupedSecond.Status()==residency::State::Acquired,
        "Duplicate/null/empty group consumed acquired members");
    std::array<residency::Lease*,2> group{&groupedFirst,&groupedSecond};
    ComPtr<ID3D12CommandQueue> copyQueue;const D3D12_COMMAND_QUEUE_DESC copyQueueDesc{D3D12_COMMAND_LIST_TYPE_COPY};
    Check(device->CreateCommandQueue(&copyQueueDesc,IID_PPV_ARGS(&copyQueue)));
    Require(residency::Lease::MarkSubmittedTogether(copyQueue.Get(),group)==E_INVALIDARG &&
        residency::Lease::MarkSubmittedTogether(nullptr,group)==E_INVALIDARG &&
        groupedFirst.Status()==residency::State::Acquired && groupedSecond.Status()==residency::State::Acquired,
        "Wrong queue committed grouped residency");
    Check(allocator->Reset());Check(list->Reset(allocator.Get(),nullptr));
    for(UINT index=0;index<3;++index)list->CopyBufferRegion(readback.Get(),index*256,inputs[index],0,256);
    Check(list->Close());
    Check(residency::Lease::MarkSubmittedTogether(queue.Get(),group));
    Require(groupedFirst.Status()==residency::State::Submitted && groupedSecond.Status()==residency::State::Submitted,
        "Successful group did not commit all members");
    Require(FAILED(residency::Lease::MarkSubmittedTogether(queue.Get(),group)),"Submitted group was replayed");
    queue->ExecuteCommandLists(1,lists);const auto groupSignal=queue->Signal(fence.Get(),2);
    Check(groupedFirst.AssociateFence(queue.Get(),fence.Get(),2,groupSignal));
    Check(groupedSecond.AssociateFence(queue.Get(),fence.Get(),2,groupSignal));
    Check(fence->SetEventOnCompletion(2,event.value));
    Require(WaitForSingleObject(event.value,10000)==WAIT_OBJECT_0,"Grouped residency GPU work did not finish");
    Check(groupedFirst.Retire());Check(groupedSecond.Retire());
    Require(groupedFirst.Status()==residency::State::Retired && groupedSecond.Status()==residency::State::Retired &&
        residency::Healthy() && !residency::QuarantinedCount(),"Completed grouped residency did not balance both leases");
    Check(readback->Map(0,&read,&mapped));const bool groupEqual=std::memcmp(mapped,expected.data(),expected.size())==0;readback->Unmap(0,&none);
    Require(groupEqual,"Grouped resident GPU copy bytes differ");
    ComPtr<ID3D12InfoQueue> info;if(SUCCEEDED(device.As(&info))){
        for(UINT64 index=0;index<info->GetNumStoredMessages();++index){SIZE_T bytes{};Check(info->GetMessage(index,nullptr,&bytes));
            std::vector<uint8_t> storage(bytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());Check(info->GetMessage(index,message,&bytes));
            Require(message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION && message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR,"GPU residency debug layer reported an error");}
    }
}
int main(int argc,char** argv)try{
    Transactions();
    if(argc>1){const bool hardware=std::strcmp(argv[1],"--hardware")==0;
        Require(hardware || std::strcmp(argv[1],"--gpu")==0,"Expected --gpu or --hardware");Gpu(hardware);}
    std::puts("PASS owned GPU residency lifecycle");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
