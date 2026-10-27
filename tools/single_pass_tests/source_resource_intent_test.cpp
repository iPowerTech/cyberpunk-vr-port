#include "Render/SourceResourceIntent.hpp"
#include "Render/RegisteredConstantUpload.hpp"
#include <dxgi1_6.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace intent=cvr::stereo::source_resource_intent;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Check(HRESULT value){if(FAILED(value))throw std::runtime_error("WARP fixture API failed");}
static std::vector<uint8_t> Pattern(size_t bytes,uint8_t seed){std::vector<uint8_t> result(bytes);for(size_t i=0;i<bytes;++i)result[i]=uint8_t(seed+i*17);return result;}
static bool Equal(std::span<const uint8_t> a,std::span<const uint8_t> b){return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin());}
static D3D12_RESOURCE_DESC Description(uint64_t bytes){D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width=bytes;desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;return desc;}
struct Fixture {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    Fixture(){ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));Check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,IID_PPV_ARGS(&allocator)));
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COPY,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));}
    ComPtr<ID3D12Resource> Buffer(uint64_t bytes,D3D12_HEAP_TYPE type=D3D12_HEAP_TYPE_DEFAULT){
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;const auto desc=Description(bytes);ComPtr<ID3D12Resource> result;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
            type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:type==D3D12_HEAP_TYPE_READBACK?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,IID_PPV_ARGS(&result)));return result;}
    void Write(ID3D12Resource* resource,uint64_t offset,std::span<const uint8_t> bytes){void* mapped{};const D3D12_RANGE none{};
        Check(resource->Map(0,&none,&mapped));Require(mapped!=nullptr,"UPLOAD fixture map failed");std::memcpy(static_cast<uint8_t*>(mapped)+offset,bytes.data(),bytes.size());
        const D3D12_RANGE written{SIZE_T(offset),SIZE_T(offset+bytes.size())};resource->Unmap(0,&written);}
    bool Capture(const ComPtr<ID3D12Resource>& resource,uint64_t offset,size_t bytes,intent::Candidate& output){return intent::CaptureAtDraw(resource,resource->GetGPUVirtualAddress()+offset,bytes,output);}
};
// Re-enter an observation at the exact unlocked sampling point, without timing
// assumptions about worker scheduling or modifying the sampled UPLOAD bytes.
struct SamplingResource final:ID3D12Resource {
    ULONG refs=1;ComPtr<ID3D12Resource> actual;std::function<void()> onMap;
    explicit SamplingResource(ID3D12Resource* resource):actual(resource){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown) && id!=__uuidof(ID3D12Resource))return E_NOINTERFACE;
        *out=static_cast<ID3D12Resource*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto count=--refs;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID id,UINT* bytes,void* data)override{return actual->GetPrivateData(id,bytes,data);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID id,UINT bytes,const void* data)override{return actual->SetPrivateData(id,bytes,data);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID id,const IUnknown* data)override{return actual->SetPrivateDataInterface(id,data);}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name)override{return actual->SetName(name);}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id,void** out)override{return actual->GetDevice(id,out);}
    HRESULT STDMETHODCALLTYPE Map(UINT sub,const D3D12_RANGE* range,void** out)override{
        if(onMap){auto action=std::move(onMap);onMap={};action();}return actual->Map(sub,range,out);
    }
    void STDMETHODCALLTYPE Unmap(UINT sub,const D3D12_RANGE* range)override{actual->Unmap(sub,range);}
    D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc()override{return actual->GetDesc();}
    D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress()override{return actual->GetGPUVirtualAddress();}
    HRESULT STDMETHODCALLTYPE WriteToSubresource(UINT sub,const D3D12_BOX* box,const void* data,UINT row,UINT depth)override{return actual->WriteToSubresource(sub,box,data,row,depth);}
    HRESULT STDMETHODCALLTYPE ReadFromSubresource(void* data,UINT row,UINT depth,UINT sub,const D3D12_BOX* box)override{return actual->ReadFromSubresource(data,row,depth,sub,box);}
    HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES* heap,D3D12_HEAP_FLAGS* flags)override{return actual->GetHeapProperties(heap,flags);}
};
static void InterleavedPoolCopies(Fixture& fixture){
    intent::Clear();auto destination=fixture.Buffer(65536),upload=fixture.Buffer(65536,D3D12_HEAP_TYPE_UPLOAD);
    const auto a=Pattern(292,17),b=Pattern(132,51);fixture.Write(upload.Get(),0,a);fixture.Write(upload.Get(),1024,b);
    ComPtr<SamplingResource> sampled;sampled.Attach(new SamplingResource(upload.Get()));
    bool nested{};sampled->onMap=[&]{nested=intent::ObserveCopy(fixture.list.Get(),destination.Get(),1024,upload.Get(),1024,b.size());};
    const bool accepted=intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,sampled.Get(),0,a.size());
    Require(nested,"Interleaved independent pool copy failed");
    Require(accepted,"Unrelated pool copy invalidated an in-flight constant sample");
    intent::Candidate left,right;
    Require(fixture.Capture(destination,0,a.size(),left) && fixture.Capture(destination,1024,b.size(),right) && Equal(left.Bytes(),a) && Equal(right.Bytes(),b),
        "Independent interleaved copies did not retain both exact inputs");
    left={};right={};intent::Clear();
    sampled->onMap=[&]{nested=intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,upload.Get(),1024,b.size());};
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,sampled.Get(),0,a.size()) && nested,
        "Overlapping interleaved write admitted stale sampled bytes");
    Require(fixture.Capture(destination,0,b.size(),right) && Equal(right.Bytes(),b),"Rejected old sample erased the newer overlapping copy");
    right={};intent::Clear();
    sampled->onMap=[&]{intent::Invalidate(destination.Get());};
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,sampled.Get(),0,a.size()),"Full invalidation admitted in-flight bytes");
    sampled->onMap=[&]{intent::Clear();};
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,sampled.Get(),0,a.size()),"Generation change admitted in-flight bytes");
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,sampled.Get(),0,a.size()),"Completed/rejected observations leaked their admission slot");
    intent::Clear();unsigned acceptedCount{},rejectedCount{};
    std::function<void(unsigned)> nestedCopy=[&](unsigned index){
        ComPtr<SamplingResource> current;current.Attach(new SamplingResource(upload.Get()));
        current->onMap=[&]{nestedCopy(index+1);};
        if(intent::ObserveCopy(fixture.list.Get(),destination.Get(),index*512,current.Get(),0,32))++acceptedCount;
        else ++rejectedCount;
    };
    nestedCopy(0);
    Require(acceptedCount==64 && rejectedCount==1 && intent::RangeCount()==64,"Concurrent sampling admission was unbounded or invalidated independent slots");
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),32768,upload.Get(),0,32),"Sampling admission stayed saturated after scopes exited");
}
static void DisjointAndContainedViews(Fixture& fixture){
    intent::Clear();auto destination=fixture.Buffer(65536);auto source=fixture.Buffer(65536,D3D12_HEAP_TYPE_UPLOAD);
    const auto a=Pattern(64,7),b=Pattern(32,91);fixture.Write(source.Get(),16,a);fixture.Write(source.Get(),128,b);
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),512,source.Get(),16,a.size()),"First sampled pool slot missing");
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),1024,source.Get(),128,b.size()),"Disjoint sampled pool slot missing");
    intent::Candidate first,second,view;Require(fixture.Capture(destination,512,a.size(),first) && fixture.Capture(destination,1024,b.size(),second),"Disjoint slots overwrote each other");
    Require(Equal(first.Bytes(),a) && Equal(second.Bytes(),b),"CPU-intended payload differs");
    Require(first.Destination()==destination.Get() && first.ProducerList()==reinterpret_cast<uintptr_t>(fixture.list.Get()) &&
        first.CopySequence() && first.SourceOffset()==16 && first.DestinationOffset()==512 && first.CopiedBytes()==64,"Copy observation labels changed");
    Require(fixture.Capture(destination,528,16,view) && view.ViewOffset()==16 && Equal(view.Bytes(),std::span<const uint8_t>(a).subspan(16,16)),"One covering copy could not provide exact b4 subview");
    Require(!fixture.Capture(destination,560,512,view) && !view,"Disjoint slots were assembled into a candidate");
    Require(intent::CurrentBytes()==96 && intent::RangeCount()==2,"Current pool accounting differs");
}
static void OverwritesAndImmutableCandidates(Fixture& fixture){
    intent::Clear();auto destination=fixture.Buffer(65536);auto source=fixture.Buffer(65536,D3D12_HEAP_TYPE_UPLOAD);
    const auto old=Pattern(64,2),fresh=Pattern(64,99),partial=Pattern(8,33);fixture.Write(source.Get(),0,old);
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),512,source.Get(),0,old.size()),"Old copy missing");
    intent::Candidate captured,current;Require(fixture.Capture(destination,512,old.size(),captured),"Old capture missing");const auto serial=captured.CopySequence();
    fixture.Write(source.Get(),0,fresh);Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),512,source.Get(),0,fresh.size()),"Replacement copy missing");
    Require(fixture.Capture(destination,512,fresh.size(),current) && Equal(current.Bytes(),fresh) && current.CopySequence()>serial,"Current copy not replaced atomically");
    Require(Equal(captured.Bytes(),old),"Captured prior candidate became mutable");
    fixture.Write(source.Get(),0,partial);Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),520,source.Get(),0,partial.size()),"Partial new write missing");
    Require(!fixture.Capture(destination,512,64,current),"Partial overwrite retained an old complete copy");
    Require(!fixture.Capture(destination,512,8,current),"Unwritten old fragment was reconstructed");
    Require(fixture.Capture(destination,520,8,current) && Equal(current.Bytes(),partial),"Exact new partial copy unavailable");
    Require(Equal(captured.Bytes(),old),"Invalidation changed an immutable captured candidate");
}
static void UnknownWritersAndBounds(Fixture& fixture){
    intent::Clear();auto destination=fixture.Buffer(131072);auto source=fixture.Buffer(65536,D3D12_HEAP_TYPE_UPLOAD);
    const auto data=Pattern(32,18);fixture.Write(source.Get(),0,data);intent::Candidate result;
    auto record=[&]{Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),512,source.Get(),0,32),"Known copy missing");};
    record();Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),512,nullptr,0,32),"Unknown source admitted");
    Require(!fixture.Capture(destination,512,32,result),"Unknown source left stale overlap usable");
    record();Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),512,source.Get(),0,65537),"Oversized source admitted");
    Require(!fixture.Capture(destination,512,32,result),"Oversized write left stale overlap usable");
    record();Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),512,destination.Get(),0,32),"DEFAULT source was sampled as UPLOAD");
    Require(!fixture.Capture(destination,512,32,result),"Rejected heap left stale overlap usable");
    record();Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),512,source.Get(),65530,32),"Short UPLOAD span admitted");
    Require(!fixture.Capture(destination,512,32,result),"Rejected source bounds retained stale overlap");
    record();Require(!intent::ObserveCopy(nullptr,destination.Get(),512,source.Get(),0,32),"Missing producer list admitted");
    Require(!fixture.Capture(destination,512,32,result),"Rejected producer left stale intent");
    record();intent::Invalidate(destination.Get(),512,8);Require(!fixture.Capture(destination,512,32,result),"Explicit overlapping invalidation failed");
    record();intent::Invalidate(destination.Get());Require(!fixture.Capture(destination,512,32,result),"Whole-resource invalidation failed");
    Require(!intent::RecordIntendedCopy(fixture.list.Get(),destination.Get(),131070,0,data),"Destination overflow admitted");
    Require(!intent::RecordIntendedCopy(fixture.list.Get(),destination.Get(),0,UINT64_MAX,data),"Source metadata overflow admitted");
    Require(!intent::CaptureAtDraw(destination,UINT64_MAX,32,result),"GPU-address overflow admitted");
}
static void ExactResourceEvenAtSameAddress(Fixture& fixture){
    intent::Clear();D3D12_HEAP_DESC desc{};desc.SizeInBytes=65536;desc.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;desc.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    ComPtr<ID3D12Heap> heap;Check(fixture.device->CreateHeap(&desc,IID_PPV_ARGS(&heap)));const auto buffer=Description(65536);
    ComPtr<ID3D12Resource> a,b;Check(fixture.device->CreatePlacedResource(heap.Get(),0,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&a)));
    Check(fixture.device->CreatePlacedResource(heap.Get(),0,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&b)));
    Require(a.Get()!=b.Get(),"Placed alias fixture did not create distinct resources");
    const auto data=Pattern(32,49);Require(intent::RecordIntendedCopy(fixture.list.Get(),a.Get(),128,0,data),"Exact resource candidate missing");
    intent::Candidate candidate;Require(fixture.Capture(a,128,32,candidate),"Exact resource capture failed");
    Require(!intent::CaptureAtDraw(b,a->GetGPUVirtualAddress()+128,32,candidate),"Same-address/resource alias inherited foreign candidate");
    Require(!fixture.Capture(b,128,32,candidate),"Unobserved placed resource inherited a candidate");
}
static ULONG References(ID3D12Resource* resource){const auto count=resource->AddRef();resource->Release();return count;}
static void PrivateLifetimeAndClear(Fixture& fixture){
    intent::Clear();Require(intent::GetStatistics().livePayloadBytes==0,"Earlier immutable fixture payload leaked");
    auto resource=fixture.Buffer(65536);const auto baseline=References(resource.Get());const auto data=Pattern(64,71);
    Require(intent::RecordIntendedCopy(fixture.list.Get(),resource.Get(),0,0,data),"Lifetime fixture record failed");
    Require(References(resource.Get())==baseline,"Private-data owner retained destination and formed a COM cycle");
    intent::Candidate candidate;Require(fixture.Capture(resource,0,64,candidate),"Lifetime capture failed");resource.Reset();
    Require(candidate.Destination()!=nullptr && Equal(candidate.Bytes(),data),"Candidate did not own destination externally");
    intent::Clear();auto stats=intent::GetStatistics();Require(!stats.currentBytes && !stats.rangeCount && !stats.ownerCount &&
        stats.livePayloadBytes==64 && stats.livePayloads==1,"Clear lost retained payload accounting or current ranges");
    Require(Equal(candidate.Bytes(),data),"Clear mutated a captured candidate");candidate={};stats=intent::GetStatistics();
    Require(!stats.currentBytes && !stats.rangeCount && !stats.livePayloadBytes && !stats.livePayloads && !stats.ownerCount,"Final candidate/resource release leaked cache bytes");
    auto other=fixture.Buffer(65536);Require(intent::RecordIntendedCopy(fixture.list.Get(),other.Get(),0,0,data),"Uncaptured lifetime record failed");
    other.Reset();stats=intent::GetStatistics();Require(!stats.currentBytes && !stats.rangeCount && !stats.livePayloadBytes && !stats.ownerCount,"Resource destruction did not retire private data");
}
static void PerResourceBudgetsAndEviction(Fixture& fixture){
    intent::Clear();auto resource=fixture.Buffer(3*1024*1024);const auto ordinary=Pattern(intent::MaxSmallCopyBytes+1,37);intent::Candidate candidate;
    for(size_t index=0;index<=intent::MaxRangesPerResource;++index)Require(intent::RecordIntendedCopy(fixture.list.Get(),resource.Get(),index*1024,0,ordinary),"Range budget permanently saturated");
    Require(intent::RangeCount()==intent::MaxRangesPerResource && intent::CurrentBytes()==intent::MaxRangesPerResource*ordinary.size(),"Per-resource ordinary range limit not enforced");
    Require(!fixture.Capture(resource,0,1,candidate) && fixture.Capture(resource,intent::MaxRangesPerResource*1024,1,candidate),"Range eviction did not preserve the latest copy");
    candidate={};intent::Clear();const auto full=Pattern(intent::MaxCopyBytes,29);
    for(size_t index=0;index<=intent::MaxResourceBytes/intent::MaxCopyBytes;++index)
        Require(intent::RecordIntendedCopy(fixture.list.Get(),resource.Get(),index*full.size(),0,full),"Byte budget permanently saturated");
    Require(intent::CurrentBytes()==intent::MaxResourceBytes && intent::GetStatistics().livePayloadBytes==intent::MaxResourceBytes,"Per-resource payload byte limit exceeded");
    Require(!fixture.Capture(resource,0,full.size(),candidate) && fixture.Capture(resource,intent::MaxResourceBytes,full.size(),candidate),"Byte eviction did not retain newest payload");
    intent::CacheEventDiagnostic event;Require(intent::QueryCacheEvents(resource,0,1,event) &&
        event.lastEvicted.reason==uint64_t(intent::CacheEventReason::ResourceBytesEviction) && event.lastEvicted.copySequence,
        "Per-resource byte eviction lost its actual event reason/span");
}
static void GlobalBudgetAndCapturedAccounting(Fixture& fixture){
    intent::Clear();std::vector<ComPtr<ID3D12Resource>> resources;const auto data=Pattern(intent::MaxCopyBytes,81);
    const size_t count=intent::MaxGlobalBytes/intent::MaxResourceBytes+1;
    for(size_t index=0;index<count;++index){resources.push_back(fixture.Buffer(intent::MaxResourceBytes));
        for(size_t slot=0;slot<intent::MaxResourceBytes/data.size();++slot)
            Require(intent::RecordIntendedCopy(fixture.list.Get(),resources.back().Get(),slot*data.size(),0,data),"Global cache became a startup-history saturation latch");}
    auto stats=intent::GetStatistics();Require(stats.currentBytes==intent::MaxGlobalBytes && stats.livePayloadBytes==intent::MaxGlobalBytes &&
        stats.evictions && stats.ownerCount<=intent::MaxOwners,"Global cache accounting exceeded capacity");
    intent::Candidate candidate;Require(!fixture.Capture(resources.front(),0,data.size(),candidate) && fixture.Capture(resources.back(),0,data.size(),candidate),"Global eviction lost latest resource");
    intent::CacheEventDiagnostic event;Require(intent::QueryCacheEvents(resources.front(),0,1,event) &&
        event.lastEvicted.reason==uint64_t(intent::CacheEventReason::GlobalBytesEviction) && event.lastEvicted.copySequence,
        "Global byte eviction was mislabelled as a per-resource/range-cap event");
    intent::Clear();stats=intent::GetStatistics();Require(!stats.currentBytes && !stats.rangeCount && stats.livePayloadBytes==data.size(),"Captured immutable payload was not included in global accounting");
    candidate={};resources.clear();stats=intent::GetStatistics();Require(!stats.livePayloadBytes && !stats.livePayloads && !stats.ownerCount,"Global payload release accounting leaked");
}
static void ClearGenerationAndResume(Fixture& fixture){
    const auto before=intent::GetStatistics().generation;intent::Clear();
    Require(intent::GetStatistics().generation==before+1,"Clear did not invalidate in-flight publication generation");
    auto resource=fixture.Buffer(65536);const auto data=Pattern(32,6);
    Require(intent::RecordIntendedCopy(fixture.list.Get(),resource.Get(),0,0,data),"Post-clear generation could not record a legitimate copy");
    intent::Candidate candidate;Require(fixture.Capture(resource,0,data.size(),candidate) && Equal(candidate.Bytes(),data),"Post-clear generation capture failed");
}
static void DispatchGate(Fixture& fixture){
    intent::Clear();auto destination=fixture.Buffer(65536),source=fixture.Buffer(65536,D3D12_HEAP_TYPE_UPLOAD);
    const auto data=Pattern(32,18);fixture.Write(source.Get(),0,data);std::atomic<uint32_t> enabled{};
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,32,&enabled) && !intent::RangeCount(),
        "Disabled old dispatch populated the current cache");
    enabled=1;Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,32,&enabled),"Enabled dispatch gate rejected a copy");
    enabled=0;intent::Clear();
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,32,&enabled) && !intent::CurrentBytes() && !intent::RangeCount(),
        "Dispatch admitted before control shutdown repopulated a cleared generation");
}
static D3D12_SHADER_RESOURCE_VIEW_DESC Structured(uint64_t bytes,uint64_t first=0){D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;view.Format=DXGI_FORMAT_UNKNOWN;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Buffer.FirstElement=first;view.Buffer.NumElements=UINT(bytes/128);view.Buffer.StructureByteStride=128;return view;}
static void HintedLargeCopyAndCrossBoundaryViews(Fixture& fixture){
    intent::Clear();constexpr size_t size=1024*1024;auto destination=fixture.Buffer(size),source=fixture.Buffer(size,D3D12_HEAP_TYPE_UPLOAD);
    const auto data=Pattern(size,43);fixture.Write(source.Get(),0,data);const auto view=Structured(size);
    Require(intent::MarkStructuredView(destination.Get(),&view),"Exact structured128 view did not establish a typed sampling hint");
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,size),"One actual hinted1MiB copy was rejected");
    intent::Candidate selected,cross,current;Require(fixture.Capture(destination,436736,2048,selected) && selected.CopiedBytes()==size &&
        selected.ViewOffset()==436736 && Equal(selected.Bytes(),std::span<const uint8_t>(data).subspan(436736,2048)),"Exact multilayer2KiB view did not come from one original copy");
    constexpr size_t at=intent::MaxCopyBytes-1024;
    Require(fixture.Capture(destination,at,2048,cross) && cross.CopiedBytes()==size && Equal(cross.Bytes(),std::span<const uint8_t>(data).subspan(at,2048)),
        "Candidate crossing64KiB boundary was fabricated or rejected");
    Require(!fixture.Capture(destination,0,intent::MaxCopyBytes+1,current),"Large candidate widened the normal64KiB view limit");
    const auto changed=Pattern(size,89);fixture.Write(source.Get(),0,changed);
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,size) && fixture.Capture(destination,at,2048,current) &&
        Equal(current.Bytes(),std::span<const uint8_t>(changed).subspan(at,2048)),"Whole large-copy replacement failed");
    Require(Equal(cross.Bytes(),std::span<const uint8_t>(data).subspan(at,2048)) && selected.CopySequence()==cross.CopySequence() &&
        current.CopySequence()>cross.CopySequence(),"Previous large-copy snapshot did not stay immutable");
    const auto stats=intent::GetStatistics();Require(stats.currentBytes==size && stats.rangeCount==1 && stats.livePayloadBytes==2*size,
        "Large copies were split into chunks or lost retained-payload accounting");
}
static void LargeHintRejectionsAndRanges(Fixture& fixture){
    intent::Clear();constexpr size_t size=1024*1024;auto destination=fixture.Buffer(size),source=fixture.Buffer(size,D3D12_HEAP_TYPE_UPLOAD);
    const auto data=Pattern(size,71);fixture.Write(source.Get(),0,data);const auto good=Structured(size);intent::Candidate candidate;
    const auto compactData=std::span<const uint8_t>(data).first(32);
    Require(intent::RecordIntendedCopy(fixture.list.Get(),destination.Get(),0,0,compactData),"No-hint stale-range precondition failed");
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,size) && !fixture.Capture(destination,0,32,candidate),
        "No-hint large writer preserved a stale overlapping candidate");
    for(unsigned field=0;field<8;++field){Require(intent::MarkStructuredView(destination.Get(),&good),"Good marker precondition failed");auto bad=good;
        switch(field){case 0:bad.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;break;case 1:bad.Format=DXGI_FORMAT_R32_UINT;break;
        case 2:bad.Shader4ComponentMapping=0;break;case 3:bad.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;break;case 4:bad.Buffer.StructureByteStride=64;break;
        case 5:bad.Buffer.NumElements=0;break;case 6:bad.Buffer.FirstElement=UINT64_MAX;break;case 7:++bad.Buffer.NumElements;break;}
        Require(!intent::MarkStructuredView(destination.Get(),&bad) && !intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,size),
            "Malformed/current structured hint admitted a large copy");}
    Require(!intent::MarkStructuredView(destination.Get(),nullptr),"Implicit SRV description became a typed hint");
    auto shifted=Structured(size-128,1);Require(intent::MarkStructuredView(destination.Get(),&shifted),"Bounded shifted structured view was rejected");
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,size),"Large copy outside marked view bounds was admitted");
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),128,source.Get(),0,size-128) && fixture.Capture(destination,65500,2048,candidate),
        "One large copy inside a shifted exact view was rejected");
    candidate={};Require(!intent::RecordIntendedCopy(fixture.list.Get(),destination.Get(),0,0,data),"Direct normal-copy API accepted unqualified large bytes");
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,intent::MaxResourceBytes+1) && !intent::RangeCount(),
        "Oversized writer did not invalidate the previously hinted range");
}
static void LargeExactResourceAliasAndGate(Fixture& fixture){
    intent::Clear();constexpr size_t size=1024*1024;D3D12_HEAP_DESC desc{};desc.SizeInBytes=size;desc.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    desc.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;ComPtr<ID3D12Heap> heap;Check(fixture.device->CreateHeap(&desc,IID_PPV_ARGS(&heap)));
    const auto buffer=Description(size);ComPtr<ID3D12Resource> a,b;Check(fixture.device->CreatePlacedResource(heap.Get(),0,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&a)));
    Check(fixture.device->CreatePlacedResource(heap.Get(),0,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&b)));
    auto source=fixture.Buffer(size,D3D12_HEAP_TYPE_UPLOAD);const auto data=Pattern(size,9);fixture.Write(source.Get(),0,data);const auto view=Structured(size);
    std::atomic<uint32_t> enabled{};Require(!intent::MarkStructuredView(a.Get(),&view,&enabled),"Disabled mode attached a typed hint");
    enabled=1;Require(intent::MarkStructuredView(a.Get(),&view,&enabled),"Enabled hint was rejected");
    Require(!intent::ObserveCopy(fixture.list.Get(),b.Get(),0,source.Get(),0,size,&enabled),"Resource/address alias inherited another resource's typed hint");
    Require(intent::ObserveCopy(fixture.list.Get(),a.Get(),0,source.Get(),0,size,&enabled),"Exact marked resource was rejected");
    enabled=0;intent::Clear();Require(!intent::ObserveCopy(fixture.list.Get(),a.Get(),0,source.Get(),0,size,&enabled) && !intent::CurrentBytes(),
        "Disabled large-copy dispatch repopulated cleared candidates");
    enabled=1;Require(!intent::ObserveCopy(fixture.list.Get(),a.Get(),0,source.Get(),0,size,&enabled),"Stale prior-generation hint admitted a large copy");
    Require(intent::MarkStructuredView(a.Get(),&view,&enabled) && intent::ObserveCopy(fixture.list.Get(),a.Get(),0,source.Get(),0,size,&enabled),
        "Renewed typed hint could not resume in the current generation");
}
static void LargeRetainedBudgetAndCleanup(Fixture& fixture){
    intent::Clear();const size_t size=intent::MaxResourceBytes;auto destination=fixture.Buffer(size),source=fixture.Buffer(size,D3D12_HEAP_TYPE_UPLOAD);
    const auto data=Pattern(size,21);fixture.Write(source.Get(),0,data);const auto view=Structured(size);
    Require(intent::MarkStructuredView(destination.Get(),&view) && intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,size),
        "Maximum bounded typed2MiB copy was rejected");intent::Candidate retained;
    Require(fixture.Capture(destination,65500,2048,retained),"Maximum-payload contained view was rejected");intent::Clear();
    Require(intent::GetStatistics().livePayloadBytes==size && !intent::CurrentBytes(),"Clear did not account for immutable retained large payload");
    Require(intent::MarkStructuredView(destination.Get(),&view),"Current-generation budget marker failed");
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,size) && intent::GetStatistics().livePayloadBytes==size && !intent::RangeCount(),
        "New large copy exceeded the retained per-resource2MiB budget");retained={};
    intent::CopyDiagnostic rejected;Require(intent::QueryCopyDiagnostic(destination,rejected) && rejected.outcome==uint64_t(intent::DiagnosticOutcome::Budget) &&
        rejected.stage==uint64_t(intent::DiagnosticStage::Publication),"Large budget rejection did not preserve its exact diagnostic reason");
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,size),"Released immutable large payload kept the budget saturated");
    Require(intent::GetStatistics().livePayloadBytes<=intent::MaxGlobalBytes && intent::CurrentBytes()==size && intent::RangeCount()==1,
        "Large-copy publication bypassed original global/per-resource limits");intent::Clear();
    Require(!intent::GetStatistics().livePayloadBytes && !intent::RangeCount(),"Large-copy cleanup leaked payload accounting");
}
static void ExactResourceDiagnosticFacts(Fixture& fixture){
    intent::Clear();auto destination=fixture.Buffer(1024*1024),source=fixture.Buffer(1024*1024,D3D12_HEAP_TYPE_UPLOAD),other=fixture.Buffer(1024*1024);
    intent::CopyDiagnostic copy;intent::MarkDiagnostic mark;
    Require(!intent::QueryCopyDiagnostic(destination,copy) && !intent::QueryMarkDiagnostic(destination,mark),"Unobserved resource fabricated diagnostic history");
    const auto data=Pattern(32,19);fixture.Write(source.Get(),0,data);std::atomic<uint32_t> enabled{};
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),16,source.Get(),8,32,&enabled) && intent::QueryCopyDiagnostic(destination,copy) &&
        copy.outcome==uint64_t(intent::DiagnosticOutcome::Disabled) && copy.stage==uint64_t(intent::DiagnosticStage::Begin) && copy.bytes==32 &&
        copy.destinationOffset==16 && copy.sourceOffset==8 && copy.resource==reinterpret_cast<uintptr_t>(destination.Get()),"Disabled dispatch lacked its actual API labels");
    enabled=1;Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,32,&enabled),"Diagnostic successful-copy precondition failed");
    Require(intent::QueryCopyDiagnostic(destination,copy) && copy.outcome==uint64_t(intent::DiagnosticOutcome::Success) &&
        copy.stage==uint64_t(intent::DiagnosticStage::Complete) && copy.sourceHeapType==D3D12_HEAP_TYPE_UPLOAD &&
        copy.sourceResource==reinterpret_cast<uintptr_t>(source.Get()) && copy.producerList==reinterpret_cast<uintptr_t>(fixture.list.Get()) && copy.checksum,
        "Successful copy lost exact source/heap/outcome facts");
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,other.Get(),0,32,&enabled) && intent::QueryCopyDiagnostic(destination,copy) &&
        copy.outcome==uint64_t(intent::DiagnosticOutcome::SourceDefault) && copy.sourceHeapType==D3D12_HEAP_TYPE_DEFAULT,
        "DEFAULT source rejection was conflated with a missing hint");
    auto readback=fixture.Buffer(1024*1024,D3D12_HEAP_TYPE_READBACK);
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,readback.Get(),0,32,&enabled) && intent::QueryCopyDiagnostic(destination,copy) &&
        copy.outcome==uint64_t(intent::DiagnosticOutcome::UnsupportedHeap),"Unsupported heap outcome was lost");
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,1024*1024,&enabled) && intent::QueryCopyDiagnostic(destination,copy) &&
        copy.outcome==uint64_t(intent::DiagnosticOutcome::NoHint) && !copy.hintPresent && copy.sourceHeapType==D3D12_HEAP_TYPE_UPLOAD,
        "No-hint large copy did not record its source heap before rejection");
    const auto view=Structured(1024*1024);Require(intent::MarkStructuredView(destination.Get(),&view,&enabled) && intent::QueryMarkDiagnostic(destination,mark) &&
        mark.outcome==uint64_t(intent::DiagnosticOutcome::Success) && mark.hintAccepted && mark.stride==128 && mark.viewBytes==1024*1024 && mark.resourceWidth==1024*1024,
        "Accepted structured marker lost its exact creation facts");
    auto bad=view;bad.Buffer.StructureByteStride=64;Require(!intent::MarkStructuredView(destination.Get(),&bad,&enabled) && intent::QueryMarkDiagnostic(destination,mark) &&
        mark.outcome==uint64_t(intent::DiagnosticOutcome::InvalidView) && mark.stride==64 && !mark.hintAccepted,"Rejected marker had no outcome receipt");
    Require(!intent::QueryCopyDiagnostic(other,copy) && !intent::QueryMarkDiagnostic(other,mark),"Diagnostic query revived another resource's history");
    intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,32,&enabled);Require(intent::QueryCopyDiagnostic(destination,copy),"Checksum fixture diagnostic missing");
    constexpr GUID copyKey{0x680b32c4,0x81fa,0x4c6d,{0xaf,0x61,0x22,0x5a,0xc3,0x78,0x14,0x84}};
    Check(other->SetPrivateData(copyKey,sizeof(copy),&copy));Require(!intent::QueryCopyDiagnostic(other,copy),"Copied POD receipt inherited another exact resource identity");
    Require(intent::QueryCopyDiagnostic(destination,copy),"Original receipt changed after foreign query");++copy.bytes;
    Check(destination->SetPrivateData(copyKey,sizeof(copy),&copy));Require(!intent::QueryCopyDiagnostic(destination,copy),"Torn diagnostic checksum was accepted");
}
static void WholeBufferCopyResourceIntent(Fixture& fixture){
    intent::Clear();constexpr size_t size=1024*1024;auto destination=fixture.Buffer(size),source=fixture.Buffer(size,D3D12_HEAP_TYPE_UPLOAD),unknown=fixture.Buffer(size);
    const auto data=Pattern(size,57);fixture.Write(source.Get(),0,data);const auto view=Structured(size);
    Require(intent::MarkStructuredView(destination.Get(),&view),"Whole-buffer marker failed");
    const auto uploadBytes=cvr::stereo::constants::CopyResourceBufferBytes(destination->GetDesc(),source->GetDesc());
    Require(uploadBytes==size && intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,uploadBytes),"Normalized whole UPLOAD copy failed");
    intent::Candidate retained,current;Require(intent::CaptureStructuredAtDraw(destination,destination->GetGPUVirtualAddress()+436736,2048,retained) &&
        Equal(retained.Bytes(),std::span<const uint8_t>(data).subspan(436736,2048)),"Whole UPLOAD copy did not provide the exact structured span");
    intent::CopyDiagnostic before,after;Require(intent::QueryCopyDiagnostic(destination,before),"Whole UPLOAD copy diagnostic missing");
    const auto defaultBytes=cvr::stereo::constants::CopyResourceBufferBytes(destination->GetDesc(),unknown->GetDesc());
    Require(defaultBytes==size && !intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,unknown.Get(),0,defaultBytes) &&
        intent::QueryCopyDiagnostic(destination,after) && after.sequence>before.sequence && after.bytes==size && !after.destinationOffset && !after.sourceOffset &&
        after.sourceResource==reinterpret_cast<uintptr_t>(unknown.Get()) && after.producerList==reinterpret_cast<uintptr_t>(fixture.list.Get()) &&
        after.sourceHeapType==D3D12_HEAP_TYPE_DEFAULT && after.outcome==uint64_t(intent::DiagnosticOutcome::SourceDefault),
        "Whole DEFAULT writer retained a stale prior API label or fabricated CPU data");
    intent::StructuredCoverageDiagnostic coverage;Require(intent::QueryStructuredCoverage(destination,436736,2048,coverage) &&
        !coverage.structuredBytes && !coverage.structuredRanges && !coverage.coveredBytes && coverage.firstHoleOffset==436736 &&
        !intent::CaptureStructuredAtDraw(destination,destination->GetGPUVirtualAddress()+436736,2048,current),"Whole unknown writer preserved old coverage");
    Require(Equal(retained.Bytes(),std::span<const uint8_t>(data).subspan(436736,2048)),"Whole unknown writer mutated a retained immutable snapshot");
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),0,source.Get(),0,uploadBytes) &&
        intent::CaptureStructuredAtDraw(destination,destination->GetGPUVirtualAddress()+436736,2048,current),"New whole UPLOAD copy could not resume observation");
    auto shape=Description(size),compatible=shape;compatible.Alignment=65536;compatible.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    Require(cvr::stereo::constants::CopyResourceBufferBytes(shape,compatible)==size,"Allocation metadata was treated as a different buffer shape");
    for(unsigned field=0;field<9;++field){auto changed=shape;
        switch(field){case 0:changed.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;break;case 1:--changed.Width;break;case 2:changed.Height=2;break;
            case 3:changed.DepthOrArraySize=2;break;case 4:changed.MipLevels=2;break;case 5:changed.Format=DXGI_FORMAT_R32_UINT;break;
            case 6:changed.SampleDesc.Count=2;break;case 7:changed.SampleDesc.Quality=1;break;case 8:changed.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;break;}
        Require(!cvr::stereo::constants::CopyResourceBufferBytes(shape,changed) && !cvr::stereo::constants::CopyResourceBufferBytes(changed,shape),
            "Incompatible/nonbuffer CopyResource shape became a sampled full-buffer range");}
    shape.Width=0;Require(!cvr::stereo::constants::CopyResourceBufferBytes(shape,shape),"Zero-width CopyResource shape was sampled");
}
static void GenericCoverageAndPresentSequences(Fixture& fixture){
    intent::Clear();auto destination=fixture.Buffer(1024);intent::GenericCoverageDiagnostic diagnostic;
    const auto before=intent::GetStatistics();Require(intent::QueryGenericCoverage(destination,64,64,diagnostic) && diagnostic.version &&
        diagnostic.resource==reinterpret_cast<uintptr_t>(destination.Get()) && !diagnostic.ownerPresent && !diagnostic.rangeCount &&
        !diagnostic.livePayloadBytes && !diagnostic.coveredBytes && diagnostic.firstHoleOffset==64,"Nonowner generic query created or inherited ranges");
    Require(intent::GetStatistics().ownerCount==before.ownerCount && intent::GetStatistics().livePayloadBytes==before.livePayloadBytes,
        "Generic query created a cache owner or payload");
    const auto data=Pattern(32,9),patch=Pattern(8,76);intent::Candidate candidate;
    Require(intent::RecordIntendedCopy(fixture.list.Get(),destination.Get(),64,0,data) && fixture.Capture(destination,64,32,candidate),"Generic query first copy failed");
    const auto first=candidate.CopySequence();candidate={};
    Require(intent::RecordIntendedCopy(fixture.list.Get(),destination.Get(),96,0,data) && fixture.Capture(destination,96,32,candidate),"Generic query adjacent copy failed");
    const auto second=candidate.CopySequence();candidate={};
    Require(intent::QueryGenericCoverage(destination,64,64,diagnostic) && diagnostic.rangeCount==2 && diagnostic.livePayloadBytes==64 &&
        diagnostic.overlapCount==2 && !diagnostic.singleCoverCount && diagnostic.coveredBytes==64 && diagnostic.firstHoleOffset==UINT64_MAX &&
        diagnostic.oldestSequence==first && diagnostic.newestSequence==second && !fixture.Capture(destination,64,64,candidate),
        "Generic union query fabricated one covering copy or lost PRESENT sequence labels");
    Require(intent::QueryGenericCoverage(destination,56,80,diagnostic) && diagnostic.coveredBytes==64 && diagnostic.firstHoleOffset==56,
        "Generic query concealed an initial hole");
    Require(intent::RecordIntendedCopy(fixture.list.Get(),destination.Get(),80,0,patch) && fixture.Capture(destination,80,8,candidate),"Generic query partial overwrite failed");
    const auto latest=candidate.CopySequence();candidate={};
    Require(intent::QueryGenericCoverage(destination,64,64,diagnostic) && diagnostic.rangeCount==2 && diagnostic.overlapCount==2 &&
        diagnostic.coveredBytes==40 && diagnostic.firstHoleOffset==64 && diagnostic.oldestSequence==second && diagnostic.newestSequence==latest &&
        diagnostic.rangeInvalidations==3 && !diagnostic.fullInvalidations && !diagnostic.genericEvictions &&
        diagnostic.lastOperation==uint64_t(intent::GenericOperation::RecordIntendedCopy) && diagnostic.lastInvalidationOffset==80 &&
        diagnostic.lastInvalidationBytes==8 && !diagnostic.lastOperationSource && diagnostic.lastOperationProducerList==reinterpret_cast<uintptr_t>(fixture.list.Get()),
        "Generic overwrite diagnostic revived old fragments or lost exact direct-copy labels");
    Require(!intent::QueryGenericCoverage(destination,1020,8,diagnostic) && !intent::QueryGenericCoverage(destination,0,intent::MaxCopyBytes+1,diagnostic),
        "Generic query admitted invalid or unbounded spans");
}
static void GenericObservedInvalidationAndClear(Fixture& fixture){
    intent::Clear();auto destination=fixture.Buffer(1024),source=fixture.Buffer(1024,D3D12_HEAP_TYPE_UPLOAD),unknown=fixture.Buffer(1024);
    const auto data=Pattern(32,43);fixture.Write(source.Get(),0,data);
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),128,source.Get(),0,32),"Generic observed counter precondition failed");
    intent::Candidate retained;Require(fixture.Capture(destination,128,32,retained),"Generic retained counter precondition failed");
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),128,unknown.Get(),0,8),"Unknown DEFAULT copy was sampled");
    intent::CopyDiagnostic copy;intent::GenericCoverageDiagnostic diagnostic;
    Require(intent::QueryCopyDiagnostic(destination,copy) && intent::QueryGenericCoverage(destination,128,32,diagnostic) &&
        !diagnostic.rangeCount && !diagnostic.overlapCount && !diagnostic.singleCoverCount && !diagnostic.coveredBytes && diagnostic.firstHoleOffset==128 &&
        !diagnostic.oldestSequence && !diagnostic.newestSequence && diagnostic.livePayloadBytes==32 && diagnostic.rangeInvalidations==2 &&
        diagnostic.lastOperation==uint64_t(intent::GenericOperation::ObserveCopy) && diagnostic.lastOperationSequence==copy.sequence &&
        diagnostic.lastOperationSource==reinterpret_cast<uintptr_t>(unknown.Get()) && diagnostic.lastOperationProducerList==reinterpret_cast<uintptr_t>(fixture.list.Get()) &&
        diagnostic.lastInvalidationOffset==128 && diagnostic.lastInvalidationBytes==8 && Equal(retained.Bytes(),data),
        "Rejected copy lost its actual operation labels, current hole or retained-byte budget");
    intent::Invalidate(destination.Get());Require(intent::QueryGenericCoverage(destination,128,32,diagnostic) && diagnostic.fullInvalidations==1 &&
        diagnostic.lastOperation==uint64_t(intent::GenericOperation::Invalidate) && !diagnostic.lastInvalidationOffset && diagnostic.lastInvalidationBytes==1024,
        "Whole-resource invalidation diagnostic lost its exact span");
    const auto generation=diagnostic.generation;intent::Clear();
    Require(intent::QueryGenericCoverage(destination,128,32,diagnostic) && diagnostic.ownerPresent && diagnostic.generation==generation+1 &&
        diagnostic.fullInvalidations==2 && diagnostic.rangeInvalidations==2 && diagnostic.livePayloadBytes==32 && !diagnostic.rangeCount &&
        diagnostic.lastOperation==uint64_t(intent::GenericOperation::Clear) && diagnostic.lastOperationGeneration==diagnostic.generation &&
        !diagnostic.lastOperationSource && !diagnostic.lastOperationProducerList && Equal(retained.Bytes(),data),"Clear lost persistent diagnostic labels or revived captured bytes");
    retained={};Require(intent::QueryGenericCoverage(destination,128,32,diagnostic) && !diagnostic.livePayloadBytes && !diagnostic.coveredBytes,
        "Generic query counted released immutable bytes as current coverage");
}
static void GenericEvictionCounters(Fixture& fixture){
    intent::Clear();auto destination=fixture.Buffer(1024*1024);const auto value=Pattern(intent::MaxSmallCopyBytes+1,17);
    for(size_t index=0;index<=intent::MaxRangesPerResource;++index)
        Require(intent::RecordIntendedCopy(fixture.list.Get(),destination.Get(),index*1024,0,value),"Generic eviction fixture record failed");
    intent::GenericCoverageDiagnostic diagnostic;intent::Candidate candidate;
    Require(intent::QueryGenericCoverage(destination,0,1,diagnostic) && diagnostic.rangeCount==intent::MaxRangesPerResource &&
        diagnostic.livePayloadBytes==intent::MaxRangesPerResource*value.size() && diagnostic.genericEvictions==1 && diagnostic.rangeInvalidations==intent::MaxRangesPerResource+1 &&
        !diagnostic.coveredBytes && diagnostic.firstHoleOffset==0 && diagnostic.oldestSequence && diagnostic.newestSequence>diagnostic.oldestSequence &&
        diagnostic.lastOperation==uint64_t(intent::GenericOperation::GenericEviction) && !diagnostic.lastInvalidationOffset && diagnostic.lastInvalidationBytes==value.size(),
        "Generic range-cap eviction diagnostic lost the removed span or PRESENT sequence extrema");
    intent::CacheEventDiagnostic event;Require(intent::QueryCacheEvents(destination,0,1,event) &&
        event.storedEvents<=intent::MaxCacheEventsPerOwner && event.droppedEvents &&
        event.lastEvicted.reason==uint64_t(intent::CacheEventReason::GenericRangeEviction) && !event.lastEvicted.offset &&
        event.lastEvicted.bytes==value.size() && event.lastEvicted.copySequence,"Generic range cap lost its exact bounded eviction receipt");
    Require(fixture.Capture(destination,intent::MaxRangesPerResource*1024,1,candidate),"Generic eviction removed the latest copy");candidate={};
    intent::Clear();Require(intent::QueryGenericCoverage(destination,0,1,diagnostic) && diagnostic.genericEvictions==1 && !diagnostic.rangeCount &&
        !diagnostic.oldestSequence && !diagnostic.newestSequence && !diagnostic.livePayloadBytes,"Clear counted historical eviction sequences as present payloads");
}
static void SmallCopyRetentionAndMixedInvalidation(Fixture& fixture){
    intent::Clear();auto destination=fixture.Buffer(3*1024*1024),source=fixture.Buffer(65536,D3D12_HEAP_TYPE_UPLOAD),unknown=fixture.Buffer(65536);
    constexpr uint64_t oldOffset=24576;const auto old=Pattern(228,61),fresh=Pattern(228,97),ordinary=Pattern(intent::MaxSmallCopyBytes+1,33),large=Pattern(1024,19);
    fixture.Write(source.Get(),0,old);fixture.Write(source.Get(),4096,ordinary);fixture.Write(source.Get(),8192,large);
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),oldOffset,source.Get(),0,old.size()),"Small retained source copy failed");
    intent::Candidate captured,current;Require(fixture.Capture(destination,oldOffset,old.size(),captured),"Small retained source capture failed");
    const auto sequence=captured.CopySequence();constexpr size_t unrelated=intent::MaxRangesPerResource+96;
    for(size_t index=0;index<unrelated;++index)
        Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),65536+index*256,source.Get(),0,old.size()),"Unrelated small actual copy failed");
    intent::GenericCoverageDiagnostic coverage;
    Require(fixture.Capture(destination,oldOffset,old.size(),current) && current.CopySequence()==sequence && Equal(current.Bytes(),old) &&
        intent::QueryGenericCoverage(destination,oldOffset,old.size(),coverage) && coverage.rangeCount==unrelated+1 &&
        coverage.singleCoverCount==1 && coverage.coveredBytes==old.size() && !coverage.genericEvictions,
        "Unrelated small writes evicted an untouched actual copy before its byte budget");
    for(size_t index=0;index<=intent::MaxRangesPerResource;++index)
        Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),1024*1024+index*1024,source.Get(),4096,ordinary.size()),"Mixed ordinary actual copy failed");
    Require(fixture.Capture(destination,oldOffset,old.size(),current) && current.CopySequence()==sequence &&
        !fixture.Capture(destination,1024*1024,ordinary.size(),current) &&
        intent::QueryGenericCoverage(destination,oldOffset,old.size(),coverage) && coverage.rangeCount==unrelated+1+intent::MaxRangesPerResource &&
        coverage.genericEvictions==1 && coverage.livePayloadBytes<intent::MaxResourceBytes,
        "Ordinary range pressure evicted a small record or concealed its real eviction");
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),oldOffset-128,source.Get(),8192,large.size()) &&
        fixture.Capture(destination,oldOffset,old.size(),current) && current.CopySequence()>sequence &&
        Equal(current.Bytes(),std::span<const uint8_t>(large).subspan(128,old.size())) && Equal(captured.Bytes(),old),
        "Large overwrite retained a stale current small copy or mutated a held prior candidate");
    fixture.Write(source.Get(),0,fresh);
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),oldOffset,source.Get(),0,fresh.size()) &&
        intent::ObserveCopy(fixture.list.Get(),destination.Get(),oldOffset+fresh.size(),source.Get(),0,fresh.size()) &&
        !fixture.Capture(destination,oldOffset-128,128,current) && !fixture.Capture(destination,oldOffset,fresh.size()*2,current) &&
        intent::QueryGenericCoverage(destination,oldOffset,fresh.size()*2,coverage) && coverage.overlapCount==2 &&
        !coverage.singleCoverCount && coverage.coveredBytes==fresh.size()*2,
        "Mixed small overwrite revived large remnants or fabricated a composed generic candidate");
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),oldOffset,unknown.Get(),0,8) &&
        !fixture.Capture(destination,oldOffset,fresh.size(),current) &&
        fixture.Capture(destination,oldOffset+fresh.size(),fresh.size(),current) && Equal(current.Bytes(),fresh),
        "Rejected unknown writer retained the old small epoch or discarded a disjoint actual copy");
    intent::Invalidate(destination.Get(),oldOffset+fresh.size(),8);
    Require(!fixture.Capture(destination,oldOffset+fresh.size(),fresh.size(),current),"Explicit range invalidation preserved a small payload");
    intent::Invalidate(destination.Get());Require(!intent::RangeCount() && !intent::CurrentBytes(),"Whole invalidation retained mixed small/ordinary cache records");
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),oldOffset,source.Get(),0,fresh.size()),"New actual copy could not resume after full invalidation");
    intent::Clear();Require(!fixture.Capture(destination,oldOffset,fresh.size(),current) && Equal(captured.Bytes(),old),
        "Generation clear reused the old small record or mutated an externally held old observation");
    captured={};Require(!intent::GetStatistics().livePayloadBytes,"Mixed small-copy cleanup leaked immutable payload bytes");
}
static void SmallRangeCapAndSharedByteBudget(Fixture& fixture){
    intent::Clear();auto destination=fixture.Buffer(3*1024*1024);const std::array<uint8_t,1> one{17};intent::Candidate current;
    for(size_t index=0;index<=intent::MaxSmallRangesPerResource;++index)
        Require(intent::RecordIntendedCopy(fixture.list.Get(),destination.Get(),index*16,0,one),"Small range capacity permanently saturated");
    intent::GenericCoverageDiagnostic coverage;intent::CacheEventDiagnostic event;
    Require(!fixture.Capture(destination,0,1,current) && fixture.Capture(destination,intent::MaxSmallRangesPerResource*16,1,current) &&
        intent::QueryGenericCoverage(destination,0,1,coverage) && coverage.rangeCount==intent::MaxSmallRangesPerResource &&
        coverage.livePayloadBytes==intent::MaxSmallRangesPerResource && coverage.genericEvictions==1 && !coverage.coveredBytes &&
        intent::QueryCacheEvents(destination,0,1,event) && event.lastEvicted.reason==uint64_t(intent::CacheEventReason::GenericRangeEviction) &&
        event.lastEvicted.bytes==1 && event.lastEvicted.copySequence,
        "Small range bound lost its actual eviction receipt or retained an evicted epoch");
    current={};intent::Clear();const auto smallCopy=Pattern(intent::MaxSmallCopyBytes,27),ordinary=Pattern(intent::MaxSmallCopyBytes+1,89);
    Require(intent::QueryGenericCoverage(destination,0,1,coverage),"Shared-budget baseline query failed");
    const auto previousEvictions=coverage.genericEvictions;
    constexpr auto budgetCopies=intent::MaxResourceBytes/intent::MaxSmallCopyBytes;
    static_assert(budgetCopies<=intent::MaxSmallRangesPerResource && budgetCopies*intent::MaxSmallCopyBytes==intent::MaxResourceBytes);
    for(size_t index=0;index<budgetCopies;++index)
        Require(intent::RecordIntendedCopy(fixture.list.Get(),destination.Get(),index*smallCopy.size(),0,smallCopy),"Small byte-budget fill failed");
    Require(intent::CurrentBytes()==intent::MaxResourceBytes &&
        intent::RecordIntendedCopy(fixture.list.Get(),destination.Get(),intent::MaxResourceBytes,0,ordinary) &&
        !fixture.Capture(destination,0,smallCopy.size(),current) && !fixture.Capture(destination,smallCopy.size(),smallCopy.size(),current) &&
        fixture.Capture(destination,intent::MaxResourceBytes,ordinary.size(),current) &&
        intent::QueryGenericCoverage(destination,0,1,coverage) && coverage.genericEvictions==previousEvictions+2 && coverage.livePayloadBytes<=intent::MaxResourceBytes &&
        intent::QueryCacheEvents(destination,0,1,event) && event.lastEvicted.reason==uint64_t(intent::CacheEventReason::ResourceBytesEviction),
        "Small record class bypassed the shared resource byte budget or concealed ordinary pressure evictions");
    current={};intent::Clear();Require(!intent::GetStatistics().livePayloadBytes && !intent::RangeCount(),"Small shared-budget cleanup leaked bytes");
}
static void PooledMaterialCopyRetention(Fixture& fixture){
    intent::Clear();auto destination=fixture.Buffer(1024*1024),source=fixture.Buffer(4096,D3D12_HEAP_TYPE_UPLOAD);
    const auto material=Pattern(292,37),ordinary=Pattern(1024,71);fixture.Write(source.Get(),0,material);
    for(size_t index=0;index<1024;++index)
        Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),index*512,source.Get(),0,material.size()),"Material pool copy failed");
    intent::Candidate saved,current;intent::GenericCoverageDiagnostic coverage;
    Require(fixture.Capture(destination,61*512,material.size(),saved) && Equal(saved.Bytes(),material) &&
        intent::QueryGenericCoverage(destination,61*512,material.size(),coverage) && coverage.rangeCount==1024 &&
        coverage.livePayloadBytes==1024*material.size() && coverage.singleCoverCount==1,
        "292-byte material constants were evicted by the ordinary512-record limit");
    fixture.Write(source.Get(),0,ordinary);
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),1024*512,source.Get(),0,ordinary.size()) &&
        fixture.Capture(destination,61*512,material.size(),current) && Equal(current.Bytes(),material),
        "Ordinary copies evicted material constants despite available shared budget");
    current={};intent::Invalidate(destination.Get(),61*512+100,1);
    Require(!fixture.Capture(destination,61*512,material.size(),current) && Equal(saved.Bytes(),material),
        "Material retention weakened actual overwrite invalidation or changed held bytes");
    saved={};intent::Clear();Require(!intent::GetStatistics().livePayloadBytes,"Material pool cache leaked bytes");
}
static void ExactIntervalEventMetadata(Fixture& fixture){
    intent::Clear();auto destination=fixture.Buffer(1024),source=fixture.Buffer(1024,D3D12_HEAP_TYPE_UPLOAD),unknown=fixture.Buffer(1024);
    intent::CacheEventDiagnostic event;Require(intent::QueryCacheEvents(destination,128,8,event) && !event.ownerPresent && !event.storedEvents && !event.droppedEvents,
        "Event query created an owner or inferred historical events");
    const auto data=Pattern(32,37);fixture.Write(source.Get(),0,data);
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),128,source.Get(),0,32),"Event publication precondition failed");
    Require(intent::QueryCacheEvents(destination,128,8,event) && event.lastPublishedCopy.reason==uint64_t(intent::CacheEventReason::PublishedCopy) &&
        event.lastPublishedCopy.offset==128 && event.lastPublishedCopy.bytes==32 && event.lastPublishedCopy.copySequence &&
        event.lastPublishedCopy.sourceResource==reinterpret_cast<uintptr_t>(source.Get()) &&
        event.lastPublishedCopy.producerList==reinterpret_cast<uintptr_t>(fixture.list.Get()),"Actual copy publication labels or extent were lost");
    const auto published=event.lastPublishedCopy.copySequence;
    Require(intent::ObserveCopy(fixture.list.Get(),destination.Get(),136,source.Get(),0,8),"Event partial overwrite precondition failed");
    Require(intent::QueryCacheEvents(destination,128,8,event) && event.lastPublishedCopy.copySequence==published &&
        event.lastInvalidated.reason==uint64_t(intent::CacheEventReason::OverwrittenPayload) && event.lastInvalidated.offset==128 &&
        event.lastInvalidated.bytes==32 && event.lastInvalidated.copySequence==published && !event.lastEvicted.eventSequence,
        "Removed old copy extent was confused with the disjoint new write extent");
    intent::Candidate candidate;Require(!fixture.Capture(destination,128,8,candidate),"Metadata receipt revived bytes removed by a partial overwrite");
    Require(!intent::ObserveCopy(fixture.list.Get(),destination.Get(),128,unknown.Get(),0,8) && intent::QueryCacheEvents(destination,128,8,event) &&
        event.lastInvalidated.reason==uint64_t(intent::CacheEventReason::RejectedBase)+uint64_t(intent::DiagnosticOutcome::SourceDefault) &&
        event.lastInvalidated.sourceResource==reinterpret_cast<uintptr_t>(unknown.Get()) && !event.lastInvalidated.copySequence,
        "Actual rejected DEFAULT writer lost its interval/outcome receipt");
    const auto count=event.storedEvents;std::atomic<uint32_t> disabled{};
    intent::ObserveCopy(fixture.list.Get(),destination.Get(),128,source.Get(),0,8,&disabled);
    Require(intent::QueryCacheEvents(destination,128,8,event) && event.storedEvents==count,"Disabled sampler added event-history work");
    intent::Clear();Require(intent::QueryCacheEvents(destination,128,8,event) && !event.storedEvents && event.droppedEvents &&
        !event.lastPublishedCopy.eventSequence && !event.lastInvalidated.eventSequence,"Clear retained metadata storage or pretended discarded history was complete");
    Require(!intent::QueryCacheEvents(destination,1020,8,event) && !intent::QueryCacheEvents(destination,0,intent::MaxCopyBytes+1,event),
        "Event query admitted unbounded/out-of-resource spans");
}
static void SmallMaterialsUnderGlobalPressure(Fixture& fixture){
    intent::Clear();auto materialBuffer=fixture.Buffer(512);const auto material=Pattern(292,41),large=Pattern(intent::MaxCopyBytes,87);
    Require(intent::RecordIntendedCopy(fixture.list.Get(),materialBuffer.Get(),0,0,material),"Global material precondition failed");
    std::vector<ComPtr<ID3D12Resource>> buffers;intent::Candidate candidate;intent::GenericCoverageDiagnostic coverage;
    for(size_t index=0;index<intent::MaxGlobalBytes/intent::MaxResourceBytes;++index){
        buffers.push_back(fixture.Buffer(intent::MaxResourceBytes));
        for(size_t offset=0;offset<intent::MaxResourceBytes;offset+=large.size())
            Require(intent::RecordIntendedCopy(fixture.list.Get(),buffers.back().Get(),offset,0,large),"Global ordinary pressure failed");
    }
    Require(fixture.Capture(materialBuffer,0,material.size(),candidate) && Equal(candidate.Bytes(),material) &&
        intent::QueryGenericCoverage(materialBuffer,0,material.size(),coverage) && !coverage.genericEvictions &&
        intent::GetStatistics().livePayloadBytes<=intent::MaxGlobalBytes,
        "Global ordinary pressure evicted tiny static constants or exceeded the global budget");
    candidate={};Require(!fixture.Capture(buffers.front(),0,large.size(),candidate),"Global pressure did not evict the oldest eligible ordinary copy");
    Require(fixture.Capture(buffers.back(),intent::MaxResourceBytes-large.size(),large.size(),candidate) && Equal(candidate.Bytes(),large),
        "Global priority failed to admit new copies");
    candidate={};intent::Invalidate(materialBuffer.Get(),0,1);
    Require(!fixture.Capture(materialBuffer,0,material.size(),candidate),"Global retention bypassed actual overwrite invalidation");
    intent::Clear();Require(!intent::GetStatistics().livePayloadBytes,"Global small-copy priority leaked payload bytes");
}
int main(){try{Fixture fixture;InterleavedPoolCopies(fixture);DisjointAndContainedViews(fixture);OverwritesAndImmutableCandidates(fixture);UnknownWritersAndBounds(fixture);
    ExactResourceEvenAtSameAddress(fixture);PrivateLifetimeAndClear(fixture);PerResourceBudgetsAndEviction(fixture);GlobalBudgetAndCapturedAccounting(fixture);ClearGenerationAndResume(fixture);DispatchGate(fixture);
    HintedLargeCopyAndCrossBoundaryViews(fixture);LargeHintRejectionsAndRanges(fixture);LargeExactResourceAliasAndGate(fixture);LargeRetainedBudgetAndCleanup(fixture);
    ExactResourceDiagnosticFacts(fixture);WholeBufferCopyResourceIntent(fixture);
    GenericCoverageAndPresentSequences(fixture);GenericObservedInvalidationAndClear(fixture);GenericEvictionCounters(fixture);
    SmallCopyRetentionAndMixedInvalidation(fixture);SmallRangeCapAndSharedByteBudget(fixture);PooledMaterialCopyRetention(fixture);ExactIntervalEventMetadata(fixture);
    SmallMaterialsUnderGlobalPressure(fixture);
    intent::Clear();Require(!intent::CurrentBytes() && !intent::RangeCount(),"Final current-cache cleanup failed");
    // These fixtures create WARP resources and sample CPU UPLOAD memory only.
    // No GPU copy, queue execution, fence wait or GPU contents assertion occurs.
    std::puts("source_resource_intent_test: OK");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"source_resource_intent_test: %s\n",error.what());return 1;}}
