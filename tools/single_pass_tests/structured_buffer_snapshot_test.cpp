#include "Render/NativeStructuredBufferSnapshot.hpp"
#include "Render/CommandResources.hpp"
#include <dxgi1_6.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace snapshot=cvr::stereo::structured_buffer_snapshot;
namespace policy=cvr::stereo::structured_buffer_snapshot::detail;
// WARP has no game executable, native getter, recording or graph-parent proof.
// These fail-closed stubs prevent this fixture from fabricating a live receipt.
namespace cvr::stereo::source_srv {
bool Compatible()noexcept{return false;}
Status RevalidateAtSource(ID3D12GraphicsCommandList*,uint64_t,UINT,UINT,const SourceSrvBacking&,SourceSrvBacking&)noexcept{return Status::MissingObservation;}
}
namespace cvr::stereo::native_recording {
bool Compatible(){return false;}
bool ValidateRecording(const Lease&,ID3D12GraphicsCommandList*,cvr::stereo::camera_upload::Context){return false;}
bool ValidateSubmission(const Lease&,ID3D12CommandQueue*){return false;}
}
namespace cvr::stereo::preparation {bool RevalidateGraphParent(const GraphParentRecord&){return false;}}
namespace cvr::stereo::indirect_copy {bool Compatible(){return false;}}
namespace cvr::stereo::constants {bool QueryResourceCreation(const ComPtr<ID3D12Resource>&,ResourceCreationInfo&){return false;}}
namespace cvr::stereo::camera_upload {Context CurrentContext(uintptr_t){return {};}}
namespace cvr::stereo::descriptors {bool MatchesDescriptor(const DescriptorStamp&){return false;}}

static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Check(HRESULT value){Require(SUCCEEDED(value),"WARP fixture API failure");}
static D3D12_RESOURCE_DESC Description(uint64_t bytes){D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;return desc;}
static policy::Admission Good(){
    policy::Admission value;value.resource=value.nativeResource=8;value.gpu=value.nativeGpu=0x100000;value.desc=Description(policy::SourceBytes);
    value.nativeBytes=value.viewBytes=policy::SourceBytes;value.sourceOffset=128;value.bytes=256;value.heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    value.heap.CreationNodeMask=value.heap.VisibleNodeMask=1;value.creationKind=1;
    value.view.Format=DXGI_FORMAT_UNKNOWN;value.view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;value.view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    value.view.Buffer.NumElements=UINT(policy::SourceBytes/policy::Stride);value.view.Buffer.StructureByteStride=UINT(policy::Stride);
    value.state={D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,2,0};value.handle=17;value.stage=1;value.shaderRegister=5;value.parameter=23;value.offset=5;
    value.sourceCurrent=value.creationKnown=value.unprotected=value.sameDevice=value.recordingCurrent=true;return value;
}
static void Admissions(){
    Require(policy::Admit(Good())==snapshot::Failure::None,"Dedicated committed read-only source rejected");
    auto value=Good();value.nativeKind=value.state.kind=3;Require(policy::Admit(value)==snapshot::Failure::None,"Observed tracked class3 was rejected");
    value=Good();value.heapFlags=policy::AllowedHeapFlags;Require(policy::Admit(value)==snapshot::Failure::None,"Implicit buffer-only heap exclusions rejected");
    for(UINT kind:{0u,2u}){value=Good();value.creationKind=kind;Require(policy::Admit(value)==snapshot::Failure::Creation,"Unobserved/reserved or placed source admitted");}
    value=Good();value.creationHeap=9;Require(policy::Admit(value)==snapshot::Failure::Creation,"External heap admitted as dedicated committed storage");
    value=Good();value.creationOffset=65536;Require(policy::Admit(value)==snapshot::Failure::Creation,"Placed offset borrowed committed lifetime");
    value=Good();value.creationBytes=policy::SourceBytes;Require(policy::Admit(value)==snapshot::Failure::Creation,"Allocation envelope borrowed committed lifetime");
    value=Good();value.creationKnown=false;Require(policy::Admit(value)==snapshot::Failure::Creation,"Missing API creation receipt accepted");
    value=Good();value.unprotected=false;Require(policy::Admit(value)==snapshot::Failure::Creation,"Protected or unknown protection session admitted");
    for(UINT flags:{UINT(D3D12_HEAP_FLAG_SHARED),UINT(D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER),UINT(D3D12_HEAP_FLAG_HARDWARE_PROTECTED),
        UINT(D3D12_HEAP_FLAG_ALLOW_DISPLAY),UINT(D3D12_HEAP_FLAG_CREATE_NOT_RESIDENT),UINT(D3D12_HEAP_FLAG_DENY_BUFFERS)}){
        value=Good();value.heapFlags=flags;Require(policy::Admit(value)==snapshot::Failure::Backing,"Unobserved shared/protected/nonresident heap form admitted");}
    value=Good();value.heap.Type=D3D12_HEAP_TYPE_UPLOAD;Require(policy::Admit(value)==snapshot::Failure::Backing,"UPLOAD source admitted");
    value=Good();value.desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;Require(policy::Admit(value)==snapshot::Failure::Backing,"Writable resource shape admitted");
    value=Good();value.desc.Width/=2;Require(policy::Admit(value)==snapshot::Failure::Backing,"Unobserved source width admitted");
    value=Good();value.nativeGpu+=128;value.nativeBytes-=128;Require(policy::Admit(value)==snapshot::Failure::Backing,"Native shared/suballocated slice borrowed full committed backing");
    value=Good();value.nativeResource=9;Require(policy::Admit(value)==snapshot::Failure::Range,"Foreign native resource borrowed the held COM range");
    for(UINT status:{1u,3u,4u,5u,6u,7u,8u,9u}){value=Good();value.state.status=status;
        Require(policy::Admit(value)==snapshot::Failure::State,"Unknown/pending/fixed-read status inferred current native state");}
    for(UINT kind:{1u,2u,4u}){value=Good();value.nativeKind=value.state.kind=kind;Require(policy::Admit(value)==snapshot::Failure::State,"Unobserved fixed/native class admitted");}
    value=Good();value.state.value=D3D12_RESOURCE_STATE_COMMON;Require(policy::Admit(value)==snapshot::Failure::State,"Initial COMMON substituted for current shader-read state");
    value=Good();value.state.value=D3D12_RESOURCE_STATE_COPY_DEST;Require(policy::Admit(value)==snapshot::Failure::State,"Writable native state admitted");
    value=Good();value.state.value=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;Require(policy::Admit(value)==snapshot::Failure::State,"Pixel t5 borrowed a vertex-only state");
    value=Good();value.view.Buffer.StructureByteStride=64;Require(policy::Admit(value)==snapshot::Failure::Range,"Unobserved structured stride admitted");
    value=Good();value.sourceOffset=1;Require(policy::Admit(value)==snapshot::Failure::Range,"Partial unaligned structured record admitted");
    value=Good();value.bytes=policy::MaxCopyBytes+128;Require(policy::Admit(value)==snapshot::Failure::Range,"More than4096 bytes admitted");
    value=Good();value.sourceOffset=policy::SourceBytes-128;Require(policy::Admit(value)==snapshot::Failure::Range,"Copy outside held COM resource admitted");
    value=Good();value.gpu=value.nativeGpu=UINT64_MAX-policy::SourceBytes+1;Require(policy::Admit(value)==snapshot::Failure::Range,"GPU address extent overflow admitted");
    value=Good();value.view.Buffer.FirstElement=16;value.view.Buffer.NumElements-=16;value.viewOffset=16*128;value.viewBytes-=16*128;
    Require(policy::Admit(value)==snapshot::Failure::Range,"Copy outside actual SRV subview admitted");
}
static void FullDomainAndReuse(){
    auto full=Good();full.extent=snapshot::Extent::FullOriginalView;full.layout=snapshot::Layout::OriginalIndices;
    full.sourceOffset=0;full.bytes=policy::SourceBytes;
    Require(policy::Admit(full)==snapshot::Failure::None,"Exact full original T5 domain rejected");
    auto changed=full;changed.extent=snapshot::Extent::SelectedRange;
    Require(policy::Admit(changed)==snapshot::Failure::Range,"Default selected mode acquired a full-domain copy");
    changed=full;changed.layout=snapshot::Layout::Dense;
    Require(policy::Admit(changed)==snapshot::Failure::Range,"Dense arena admitted full original-index view");
    changed=full;changed.extent=static_cast<snapshot::Extent>(2);
    Require(policy::Admit(changed)==snapshot::Failure::Range,"Unknown extent mode admitted");
    changed=full;changed.sourceOffset=128;changed.bytes-=128;
    Require(policy::Admit(changed)==snapshot::Failure::Range,"Partial domain borrowed full-view admission");
    changed=full;changed.view.Buffer.FirstElement=1;changed.view.Buffer.NumElements--;changed.viewOffset=128;changed.viewBytes-=128;
    Require(policy::Admit(changed)==snapshot::Failure::Range,"Shifted native view borrowed full-view admission");
    changed=full;changed.view.Buffer.NumElements--;
    Require(policy::Admit(changed)==snapshot::Failure::Range,"Truncated original view borrowed full-view admission");
    changed=full;changed.recordingCurrent=false;
    Require(policy::Admit(changed)==snapshot::Failure::Source,"Full mode bypassed fresh native recording authorization");
    changed=full;changed.sourceCurrent=false;
    Require(policy::Admit(changed)==snapshot::Failure::Source,"Full mode bypassed actual source authorization");
    changed=full;changed.state.value=D3D12_RESOURCE_STATE_COPY_DEST;
    Require(policy::Admit(changed)==snapshot::Failure::State,"Full mode inferred a readable source through writes");
    uint64_t cursor{},offset{},end{},viewEnd{};
    for(UINT version=0;version<8;++version){
        Require(policy::ReserveExtent(snapshot::Extent::FullOriginalView,snapshot::Layout::OriginalIndices,cursor,policy::MaxArenaBytes,
            0,policy::SourceBytes,offset,end,viewEnd) && offset==uint64_t(version)*policy::SourceBytes &&
            end==offset+policy::SourceBytes && viewEnd==end,"Full version reservation overlaps or truncates the domain");cursor=end;
    }
    Require(cursor==policy::MaxArenaBytes && !policy::ReserveExtent(snapshot::Extent::FullOriginalView,snapshot::Layout::OriginalIndices,
        cursor,policy::MaxArenaBytes,0,policy::SourceBytes,offset,end,viewEnd) && !offset && !end && !viewEnd,
        "Ninth full version exceeded the unchanged eight-MiB arena cap");
    Require(!policy::Reserve(0,policy::MaxArenaBytes,policy::SourceBytes,offset,end) &&
        !policy::ReserveView(0,policy::MaxArenaBytes,0,policy::SourceBytes,offset,end,viewEnd),
        "Selected-range reservation silently widened its four-KiB limit");
    Require(!policy::ReserveExtent(snapshot::Extent::FullOriginalView,snapshot::Layout::Dense,0,policy::MaxArenaBytes,
        0,policy::SourceBytes,offset,end,viewEnd),"Full copy reserved a dense layout");
    namespace windows=cvr::stereo::copy_windows;
    const uint64_t list=11,generation=12,resource=13,gpu=0x100000;
    auto window=windows::Create(list,generation);auto otherArena=windows::Create(list,generation);auto otherGeneration=windows::Create(list,generation+1);
    Require(window && otherArena && otherGeneration,"No-write windows could not be created");
    policy::FullVersion prior{list,generation,resource,gpu,0,20,true};
    const windows::Span span{resource,gpu,policy::SourceBytes,0,policy::SourceBytes,0,20};window->Publish(span);
    auto token=window->Find(resource,0,policy::SourceBytes,21);
    const auto reusable=[&](const policy::FullVersion& version,const windows::Span& value,uint64_t occurrence=21){
        return policy::ReuseMatches(version,value,list,generation,resource,gpu,occurrence,policy::SourceBytes,policy::MaxArenaBytes);
    };
    Require(windows::Valid(token) && reusable(prior,token->span),"Completed full copy was not reusable at a later same-recording occurrence");
    Require(!otherArena->Find(resource,0,policy::SourceBytes,21) && !otherGeneration->Find(resource,0,policy::SourceBytes,21),
        "Full copy escaped its own arena or recording generation");
    Require(!window->Find(resource,0,policy::SourceBytes,20) && !reusable(prior,span,20),"Same occurrence reused its own unexecuted copy");
    auto bad=prior;bad.recorded=false;Require(!reusable(bad,span),"Numeric window entry authenticated an unrecorded copy");
    bad=prior;bad.list++;Require(!reusable(bad,span),"Foreign list's full copy reused");
    bad=prior;bad.generation++;Require(!reusable(bad,span),"Foreign generation's full copy reused");
    bad=prior;bad.resource++;Require(!reusable(bad,span),"Foreign resource's full copy reused");
    bad=prior;bad.gpu+=128;Require(!reusable(bad,span),"Changed resource GPU identity reused");
    bad=prior;bad.destination=policy::SourceBytes;Require(!reusable(bad,span),"Unrecorded destination region reused");
    auto badSpan=span;badSpan.bytes=4096;Require(!reusable(prior,badSpan),"Selected copy was misidentified as a complete domain");
    badSpan=span;badSpan.backing+=128;Require(!reusable(prior,badSpan),"Different backing extent reused");
    Require(!policy::Epoch({list,generation,21,false},list,generation,21),"Valid no-write window manufactured a read epoch");
    window->Write(resource+1);Require(windows::Valid(token),"Unrelated resource write invalidated the full copy");
    window->Address(gpu+policy::SourceBytes,4);Require(windows::Valid(token),"Adjacent GPU address invalidated the full copy");
    window->Address(gpu+policy::SourceBytes-4,4);Require(!windows::Valid(token) && !window->Find(resource,0,policy::SourceBytes,22),
        "A write inside the full domain failed to invalidate reuse");
    auto next=span;next.destination=policy::SourceBytes;next.occurrence=22;window->Publish(next);
    auto second=window->Find(resource,0,policy::SourceBytes,23);Require(windows::Valid(second) && second->span.destination==policy::SourceBytes,
        "A fresh post-write full copy reused stale private bytes");
    window->Close();Require(!windows::Valid(second) && !window->Find(resource,0,policy::SourceBytes,24),"Closed native generation retained reuse permission");
}
static void StatesAndEpochs(){
    const UINT pixel=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,copy=D3D12_RESOURCE_STATE_COPY_SOURCE,vertex=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Require(!policy::ReadMask({pixel,2,0},0,true) && policy::ReadMask({pixel|copy,2,0},0,true),"Copy readiness was inferred from shader-read permission");
    uint32_t required{};
    Require(policy::RecoveryMask(pixel,{copy|vertex,2,0},0,required) && required==(pixel|copy|vertex),"Recovery dropped accumulated/original read bits");
    Require(!policy::RecoveryMask(pixel,{D3D12_RESOURCE_STATE_COPY_DEST,2,0},0,required) && !required,"Recovery guessed through writable state");
    Require(!policy::RecoveryMask(pixel,{pixel,4,0},0,required) && !policy::RecoveryMask(pixel,{pixel,5,0},0,required),"Recovery fabricated pending/unknown state");
    snapshot::ReadEpochCertificate epoch{11,12,13,false};Require(!policy::Epoch(epoch,11,12,13),"Tuple manufactured original GPU-read epoch");
    epoch.certified=true;Require(policy::Epoch(epoch,11,12,13) && !policy::Epoch(epoch,11,12,14) && !policy::Epoch(epoch,11,15,13),"Certified epoch revived another occurrence/generation");
    uint64_t offset{},end{};Require(policy::Reserve(128,512,256,offset,end) && offset==128 && end==384,"Bounded private offset reservation failed");
    Require(!policy::Reserve(128,256,256,offset,end) && !offset && !end,"Partial reservation was published");
    Require(!policy::Reserve(1,4096,128,offset,end) && !policy::Reserve(0,4096,4097,offset,end),"Unaligned/oversized arena range admitted");
    uint64_t viewEnd{};
    Require(policy::ReserveView(0,policy::MaxArenaBytes,436736,2048,offset,end,viewEnd) && offset==436736 && end==438784 &&
        viewEnd==policy::SourceBytes,"Native original-index reservation rebased the shader domain");
    Require(policy::ReserveView(end,policy::MaxArenaBytes,436736,2048,offset,end,viewEnd) && offset==438784 && viewEnd==policy::SourceBytes+2048,
        "Consecutive versions overwrite their selected records");
    for(const auto cursor:{1ull,policy::MaxArenaBytes,UINT64_MAX})Require(!policy::ReserveView(cursor,policy::MaxArenaBytes,436736,2048,offset,end,viewEnd) &&
        !offset && !end && !viewEnd,"Invalid native view range published an allocation");
    Require(!policy::ReserveView(0,policy::SourceBytes-128,436736,2048,offset,end,viewEnd),"Truncated full SRV domain accepted");
    Require(!policy::ReserveView(0,policy::MaxArenaBytes,policy::SourceBytes-128,256,offset,end,viewEnd),"Selected native range exceeds original domain");
}
static void PendingRecoveryIdentity(){
    using cvr::stereo::source_cbv_details::Context;using cvr::stereo::source_cbv_details::Observation;
    const cvr::stereo::geometry::NativeBuffer saved{0x1000,0x2000,policy::SourceBytes,0x3000,0x4000,17,0};
    std::array<uint8_t,0xB0> record{};record[6]=2;const auto bytes=uint32_t(saved.bytes);
    std::memcpy(record.data(),&bytes,4);std::memcpy(record.data()+8,&saved.gpu,8);std::memcpy(record.data()+0x10,&saved.resource,8);
    const Context before{Observation::Known,0x4000,0x5000,0x6000,0,2,0};
    auto pending=before;pending.pending=3;
    const auto check=[&](const auto& native,const auto& raw,const auto& context,uint64_t address){
        return policy::RecoveryIdentity(saved,record,before,0x7000,native,raw,context,address);
    };
    Require(check(saved,record,pending,0x7000),"Genuine pending context could not reach guarded flush preflight");
    Require(!policy::ReadMask({0,4,0},0),"Recovery pending preflight granted a current readable state");
    auto flushed=pending;flushed.pending=0;Require(check(saved,record,flushed,0x7000),"Flushed exact context identity was rejected");
    for(UINT field=0;field<7;++field){auto changed=pending;
        if(field==0)changed.address++;if(field==1)changed.list++;if(field==2)changed.state++;if(field==3)changed.kind=1;
        if(field==4)changed.submitted=1;if(field==5)changed.observation=Observation::Changed;if(field==6)changed.observation=Observation::Unreadable;
        Require(!check(saved,record,changed,0x7000),"Recovery accepted changed/stale context or submitted generation");
    }
    for(UINT field=0;field<7;++field){auto changed=saved;
        if(field==0)changed.resource++;if(field==1)changed.gpu++;if(field==2)changed.bytes--;if(field==3)changed.registry++;
        if(field==4)changed.context++;if(field==5)changed.handle++;if(field==6)changed.kind=3;
        Require(!check(changed,record,pending,0x7000),"Recovery accepted a replaced native resource/handle/B0 identity");
    }
    auto changed=record;changed[0x20]=1;
    Require(!check(saved,changed,pending,0x7000) && !check(saved,record,pending,0x7001),"Recovery accepted a changed native record/address");
}
class Sentinel final:public IUnknown {
    std::atomic<ULONG> refs_{1};std::atomic<UINT>& destroyed_;
public:
    explicit Sentinel(std::atomic<UINT>& destroyed):destroyed_(destroyed){}
    ~Sentinel(){++destroyed_;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** output)override{
        if(!output)return E_POINTER;*output=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*output=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto count=--refs_;if(!count)delete this;return count;}
};
static void WarpStorageAndFailures(){
    ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> adapter;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    // Real committed/placed API objects supply shape/lifetime facts only. The
    // known native state below is a pure predicate input, never a live receipt.
    D3D12_HEAP_PROPERTIES sourceHeap{};sourceHeap.Type=D3D12_HEAP_TYPE_DEFAULT;const auto sourceDesc=Description(policy::SourceBytes);
    ComPtr<ID3D12Resource> committed;Check(device->CreateCommittedResource(&sourceHeap,D3D12_HEAP_FLAG_NONE,&sourceDesc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&committed)));
    ComPtr<ID3D12Resource1> extended;Check(committed.As(&extended));ComPtr<IUnknown> protectedSession;
    const auto protectedResult=extended->GetProtectedResourceSession(IID_PPV_ARGS(&protectedSession));
    std::printf("WARP unprotected session HRESULT=%08X\n",UINT(protectedResult));
    Require(policy::NoProtectedSession(protectedResult,bool(protectedSession)),"Real unprotected WARP resource was rejected");
    Require(!policy::NoProtectedSession(E_FAIL,false) && !policy::NoProtectedSession(S_OK,true) &&
        !policy::NoProtectedSession(DXGI_ERROR_NOT_FOUND,true),"Protection query unknown failure or live session was admitted");
    auto describe=[&](const ComPtr<ID3D12Resource>& resource,UINT creationKind){auto value=Good();D3D12_HEAP_FLAGS flags{};
        value.resource=value.nativeResource=reinterpret_cast<uintptr_t>(resource.Get());value.gpu=value.nativeGpu=resource->GetGPUVirtualAddress();value.desc=resource->GetDesc();
        Check(resource->GetHeapProperties(&value.heap,&flags));value.heapFlags=UINT(flags);value.creationKind=creationKind;return value;
    };
    Require(policy::Admit(describe(committed,1))==snapshot::Failure::None,"Actual committed WARP shape rejected by the pure predicate");
    const auto sourceGpu=committed->GetGPUVirtualAddress();ComPtr<ID3D12Resource> heldSource=committed;committed.Reset();
    Require(heldSource->GetGPUVirtualAddress()==sourceGpu && heldSource->GetDesc().Width==policy::SourceBytes,"Held committed source lost its immutable GPU VA/implicit backing");
    const auto allocation=device->GetResourceAllocationInfo(0,1,&sourceDesc);D3D12_HEAP_DESC placedHeapDesc{};
    placedHeapDesc.SizeInBytes=allocation.SizeInBytes;placedHeapDesc.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;placedHeapDesc.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    ComPtr<ID3D12Heap> placedHeap;ComPtr<ID3D12Resource> placed;Check(device->CreateHeap(&placedHeapDesc,IID_PPV_ARGS(&placedHeap)));
    Check(device->CreatePlacedResource(placedHeap.Get(),0,&sourceDesc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&placed)));
    Require(policy::Admit(describe(placed,2))==snapshot::Failure::Creation,"Actual placed WARP object borrowed committed admission");
    Require(!CyberpunkVR_StructuredBufferSnapshotsEnable,"Structured snapshots were enabled by default");
    snapshot::Request request;request.source.recording.list=reinterpret_cast<uintptr_t>(list.Get());request.source.recording.generation=17;
    request.parameter=23;request.offset=5;request.bytes=128;
    snapshot::ReadEpochCertificate epoch{reinterpret_cast<uintptr_t>(list.Get()),17,19,false};cvr::stereo::preparation::GraphParentRecord parent;
    snapshot::PreparedCopy prepared;snapshot::PrivateRange range;
    snapshot::SnapshotArena empty;Require(!empty.Finalize(list.Get()) && !empty.Finalized() && !empty.Resource() && !empty.Bytes(),"Empty arena finalized");
    Require(!snapshot::PrepareAtActualCall(list.Get(),1,request,parent,epoch,empty,prepared) && snapshot::LastFailure()==snapshot::Failure::Disabled,"Disabled module attempted native ownership preparation");
    snapshot::SnapshotArena arena;Check(arena.Create(device.Get(),list.Get(),17,4096));
    Require(arena.Resource() && arena.Resource()->GetDesc().Width==4096 && !arena.Bytes() && !arena.Finalized(),"Private committed arena shape/lifecycle differs");
    Require(FAILED(arena.Create(device.Get(),list.Get(),17,4096)) && !arena.Finalize(list.Get()),"Duplicate/empty pending arena finalized");
    CyberpunkVR_StructuredBufferSnapshotsEnable=1;
    {snapshot::ActualCallScope scope(list.Get(),17,19);Require(scope.Active(),"Original occurrence scope was not active");
        Require(!snapshot::PrepareAtActualCall(list.Get(),1,request,parent,epoch,arena,prepared) && snapshot::LastFailure()==snapshot::Failure::Scope,
            "Uncertified actual call manufactured source-copy proof");
        epoch.certified=true;Require(!snapshot::PrepareAtActualCall(list.Get(),1,request,parent,epoch,arena,prepared) && snapshot::LastFailure()==snapshot::Failure::Code,
            "WARP/fake origin bypassed the native executable fingerprints");
        {snapshot::ActualCallScope nested(list.Get(),17,20);Require(nested.Active() && !scope.Active(),"Nested source revived an older scope");}
        Require(!scope.Active(),"Old source scope revived after nested occurrence");
    }
    Require(!prepared.owner && !range.resource && !arena.Bytes() && !snapshot::RecordAtActualCall(list.Get(),prepared,range) &&
        !snapshot::ValidateAtActualCall(list.Get(),prepared) && !snapshot::ValidateSourceSubmission(prepared,nullptr),"Rejected origin published ownership/private bytes/submission proof");
    snapshot::SnapshotArena fullArena;Check(fullArena.Create(device.Get(),list.Get(),17,policy::MaxArenaBytes,snapshot::Layout::OriginalIndices));
    auto fullRequest=request;fullRequest.extent=snapshot::Extent::FullOriginalView;fullRequest.sourceOffset=0;fullRequest.bytes=UINT(policy::SourceBytes);
    {snapshot::ActualCallScope scope(list.Get(),17,21);epoch.occurrence=21;
        Require(!snapshot::PrepareAtActualCall(list.Get(),1,fullRequest,parent,epoch,fullArena,prepared) &&
            snapshot::LastFailure()==snapshot::Failure::Code && !prepared.owner && !prepared.reused && !prepared.gpuCopiedBytes && !fullArena.Bytes(),
            "Full mode fabricated native authorization or actual copied-byte accounting");
    }
    fullArena.Abort();Require(!fullArena.Finalize(list.Get()),"Unauthorized full arena finalized");
    arena.Abort();Require(!arena.Finalized() && !arena.Finalize(list.Get()),"Aborted arena became usable");CyberpunkVR_StructuredBufferSnapshotsEnable=0;
    std::atomic<UINT> destroyed{};constexpr GUID lifetimeGuid{0xb1dfd149,0xee84,0x4f0b,{0x8c,0x17,0x7a,0x05,0x53,0x8f,0xc1,0x20}};
    {snapshot::SnapshotArena held;Check(held.Create(device.Get(),list.Get(),18,4096));auto resource=held.Resource();
        ComPtr<IUnknown> sentinel;sentinel.Attach(new Sentinel(destroyed));Check(resource->SetPrivateDataInterface(lifetimeGuid,sentinel.Get()));
        Require(cvr::gpu::KeepCommandResources(list.Get(),{resource.Get()}),"Private arena storage lacked command-resource ownership");held.Abort();
    }
    Require(!destroyed,"Arena/caller release destroyed recorded private storage");cvr::gpu::ResetCommandResources(list.Get());
    Require(destroyed==1,"Reset leaked private storage after all owned references ended");Check(list->Close());
    // Resource creation/storage/lifetime checks only: no live native receipt,
    // GPU copy, queue execution, fence wait or GPU contents claim is fabricated.
}
int main()try{Admissions();FullDomainAndReuse();StatesAndEpochs();PendingRecoveryIdentity();WarpStorageAndFailures();std::puts("PASS committed structured snapshot predicates, full-domain extent/reuse isolation, exact pending-recovery identity, real WARP arena storage, scoped refusal and ownership; live native state/read epoch remain required");return 0;}
catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
