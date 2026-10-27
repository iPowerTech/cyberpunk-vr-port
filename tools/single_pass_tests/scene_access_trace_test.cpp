#include "Render/NativeSceneAccess.hpp"
#include "Render/RegisteredConstantUpload.hpp"
#include "Render/NativeTextureRead.hpp"
#include "Render/NativeBreadcrumbWrite.hpp"
#include "Render/StereoDescriptorAudit.hpp"
#include "Render/StereoSceneState.hpp"
#include "Render/RenderProbeScope.hpp"
#include <dxgi1_6.h>
#include <atomic>
#include <cstdio>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace trace=cvr::stereo::scene_access_trace;
namespace child=cvr::stereo::scene_child;
namespace native=cvr::stereo::native_recording;
namespace fixture {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Hr(HRESULT value,const char* message){Require(SUCCEEDED(value),message);}
struct Owner final:IUnknown {
    std::atomic<ULONG> refs{1};std::atomic<uint32_t>* destroyed{};
    explicit Owner(std::atomic<uint32_t>* counter=nullptr):destroyed(counter){}
    ~Owner(){if(destroyed)++*destroyed;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** result)override{
        if(!result)return E_POINTER;*result=nullptr;
        if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*result=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto left=--refs;if(!left)delete this;return left;}
};
struct Attribution {ID3D12GraphicsCommandList* list{};child::Identity child;native::Lease native;};
std::array<Attribution,3> at;
ComPtr<ID3D12Resource> target;
bool rearmAt{},rearmTargets{},metadataMissing{};
ComPtr<ID3D12Resource> breadcrumbResource;
bool breadcrumbCapture=true,breadcrumbDescribe=true,rearmBreadcrumbCapture{},rearmBreadcrumbDescribe{};
uint32_t breadcrumbCorrupt{};
constexpr uintptr_t BreadcrumbCaller=0x123400;
void Rearm(){trace::Finish(true);trace::Arm();}

struct Harness {
    ComPtr<ID3D12Device> device;
    std::array<ComPtr<ID3D12CommandAllocator>,3> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>,3> lists;
    ComPtr<ID3D12Resource> buffer,color;
    Harness(){
        ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;
        Hr(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"DXGI factory unavailable");
        Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"WARP adapter unavailable");
        Hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"WARP device unavailable");
        for(uint32_t i=0;i<lists.size();++i){
            Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocators[i])),"Allocator creation failed");
            Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocators[i].Get(),nullptr,IID_PPV_ARGS(&lists[i])),"List creation failed");
        }
        buffer=Buffer();
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=desc.Height=4;
        desc.DepthOrArraySize=desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&color)),"Texture creation failed");
    }
    ComPtr<ID3D12Resource> Buffer(){
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=4096;
        desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> result;
        Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&result)),"Buffer creation failed");return result;
    }
    void Fresh(){
        rearmAt=rearmTargets=metadataMissing=false;target=color;
        breadcrumbResource=buffer;breadcrumbCapture=breadcrumbDescribe=true;
        rearmBreadcrumbCapture=rearmBreadcrumbDescribe=false;breadcrumbCorrupt=0;
        for(uint32_t i=0;i<at.size();++i){auto& value=at[i];value={};value.list=lists[i].Get();
            value.native.token.Attach(new Owner);value.native.generation=i+1;value.native.context=0x2000+i*0x100;
            value.native.list=uint64_t(value.list);value.native.registry=0x3000;
            auto& identity=value.child;identity.consumerId=11;identity.epoch=12;identity.pairGeneration=13;identity.frameOwner=0x4000;
            identity.sourceGraph=0x5000;identity.executionGraph=0x6000;identity.rootObject=0x7000+i*0x100;
            identity.childObject=0x8000+i*0x100;identity.childVtable=0x9000;identity.childWork=0xA000;
            identity.workContext=0xB000+i*0x100;identity.invocation=i+1;identity.rootHandle=i;
            identity.view=i?0u:1u;identity.section=i?1u:2u;identity.childSlot=i?0u:6u;identity.thread=GetCurrentThreadId();
        }
        trace::Arm();Require(trace::Active(),"Collector did not arm");
    }
    void Read(uint32_t side,ID3D12Resource* resource=nullptr){trace::Operand(lists.at(side).Get(),resource?resource:buffer.Get(),false,3,0,0,64);}
    void Close(uint32_t side,HRESULT result=S_OK){trace::Close(lists.at(side).Get(),result);}
    void CompletePair(){Read(0);Read(1);Close(0);Close(1);trace::Finish();}
};
uint64_t Stat(uint32_t index){return CyberpunkVR_SceneAccessStats[index];}
const trace::Event& Event(uint32_t index){
    Require(!trace::Active() && index<Stat(6) && CyberpunkVR_SceneAccessEvents,"Published event missing");
    return reinterpret_cast<const trace::Event*>(CyberpunkVR_SceneAccessEvents)[index];
}
const std::array<uint64_t,16>& Recording(uint32_t index){
    Require(!trace::Active() && index<Stat(4) && CyberpunkVR_SceneAccessRecordings,"Published recording missing");
    return reinterpret_cast<const std::array<uint64_t,16>*>(CyberpunkVR_SceneAccessRecordings)[index];
}
void Finished(uint32_t failure,uint32_t selected){
    Require(!trace::Active() && Stat(2)==1 && Stat(8)==selected && Stat(14)==failure && !Stat(15) &&
        !(CyberpunkVR_SceneAccessSeq.load()&1),"Finished status or permission invalid");
}
}

// The fixture owns real WARP resources/lists but does not create a queue or
// execute GPU commands. Only native attribution and metadata providers are faked.
namespace cvr::stereo::scene_child {
bool DescribeCurrentAtList(ID3D12GraphicsCommandList* list,Identity& output,native_recording::Lease& recording)noexcept{
    output={};recording={};
    for(const auto& value:fixture::at)if(value.list==list){output=value.child;recording=value.native;
        if(fixture::rearmAt){fixture::rearmAt=false;fixture::Rearm();}return true;}
    return false;
}
}
namespace cvr::stereo::descriptors {
bool GetHeldResourceCookie(const ComPtr<ID3D12Resource>& resource,uint64_t& cookie)noexcept{
    cookie=resource?uint64_t(resource.Get()):0;return cookie!=0;
}
}
namespace cvr::stereo::constants {
bool QueryResourceCreation(const ComPtr<ID3D12Resource>& resource,ResourceCreationInfo& output){
    output={};if(!resource || fixture::metadataMissing)return false;output.resource=resource;output.kind=1;return true;
}
}
namespace cvr::stereo::texture_read {
bool QueryObservedAllocation(const ComPtr<ID3D12Resource>&,constants::ResourceCreationInfo& output){output={};return false;}
}
namespace cvr::stereo::scene_state {
bool CaptureBoundResources(UINT count,const D3D12_CPU_DESCRIPTOR_HANDLE* colors,BOOL,const D3D12_CPU_DESCRIPTOR_HANDLE* depth,
    std::array<ComPtr<ID3D12Resource>,9>& output){
    output={};if(count>1 || (count && !colors) || !fixture::target)return false;
    if(count)output[0]=fixture::target;if(depth)output[8]=fixture::target;
    if(fixture::rearmTargets){fixture::rearmTargets=false;fixture::Rearm();}return true;
}
}
namespace cvr::stereo::breadcrumb_write {
struct Lease::Data {Snapshot snapshot;};
bool Capture(ID3D12GraphicsCommandList2* list,UINT count,const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER* parameters,
             const D3D12_WRITEBUFFERIMMEDIATE_MODE* modes,uintptr_t caller,Lease& output){
    output={};
    if(!fixture::breadcrumbCapture || !list || count!=1 || !parameters || !modes ||
        caller!=fixture::BreadcrumbCaller || !fixture::breadcrumbResource ||
        (modes[0]!=D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN && modes[0]!=D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT) ||
        parameters[0].Value!=uint32_t(modes[0]))return false;
    auto data=std::make_shared<Lease::Data>();auto& snapshot=data->snapshot;
    snapshot.creation.resource=fixture::breadcrumbResource;snapshot.creation.kind=1;snapshot.resourceDesc=fixture::breadcrumbResource->GetDesc();
    snapshot.destination=parameters[0].Dest;snapshot.value=parameters[0].Value;snapshot.mode=uint32_t(modes[0]);
    snapshot.provenance=Provenance::NativeFactory;
    if(fixture::breadcrumbCorrupt==1)snapshot.creation.resource.Reset();
    if(fixture::breadcrumbCorrupt==2)snapshot.resourceDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    if(fixture::breadcrumbCorrupt==3)++snapshot.resourceDesc.Width;
    if(fixture::breadcrumbCorrupt==4)snapshot.creation.heapOffset=8;
    output.data_=std::move(data);
    if(fixture::rearmBreadcrumbCapture){fixture::rearmBreadcrumbCapture=false;fixture::Rearm();}
    return true;
}
bool Describe(const Lease& lease,Snapshot& output){
    output={};if(!fixture::breadcrumbDescribe || !lease.data_)return false;output=lease.data_->snapshot;
    if(fixture::rearmBreadcrumbDescribe){fixture::rearmBreadcrumbDescribe=false;fixture::Rearm();}return true;
}
}

namespace fixture {
void Lifecycle(Harness& h){
    h.Fresh();h.CompletePair();Finished(0,2);Require(Stat(6)==2 && Recording(0)[3] && Recording(1)[3],"Closed pair not published");
    const auto finishedSequence=CyberpunkVR_SceneAccessSeq.load();
    trace::Finish();Finished(0,2);trace::Finish(true);Finished(0,2);
    Require(CyberpunkVR_SceneAccessSeq.load()==finishedSequence,"Repeated Finish republished the frozen capture");
    h.Fresh();h.Read(0);h.Close(0);trace::Finish();Finished(9,1);Require(Stat(6)==1,"Partial rows not published");
    h.Fresh();h.Read(0);h.Read(1);h.Close(0);trace::Finish();Finished(9,2);
    Require(!Recording(1)[3] && Recording(1)[4]==uint32_t(E_PENDING),"Unclosed recording status lost");
    h.Fresh();h.Read(0);h.Read(1);h.Close(0,E_FAIL);h.Close(1);trace::Finish();Finished(7,2);
    Require(Recording(0)[4]==uint32_t(E_FAIL),"Failed close status lost");
    h.Fresh();h.Read(0);h.Read(1);h.Close(0);h.Close(1);trace::Finish(true);Finished(8,2);
    trace::Finish();Finished(8,2);trace::Finish(true);Finished(8,2);
    h.Fresh();for(uint32_t i=0;i<3;++i){h.Read(i);h.Close(i);}trace::Finish();Finished(9,3);
    h.Fresh();trace::Finish();Finished(9,0);
    h.Fresh();h.Read(0);h.Read(1);h.Close(0);h.Read(0);h.Close(1);trace::Finish();Finished(4,2);
    h.Fresh();metadataMissing=true;h.CompletePair();Finished(0,2);Require(Stat(11)==1,"Missing allocation metadata was hidden");
}
void EpochIsolation(Harness& h){
    h.Fresh();h.Read(0);const auto before=Stat(3);rearmAt=true;h.Read(0);
    Require(trace::Active() && Stat(3)==before+1 && !Stat(4) && !Stat(5) && !Stat(6) && !Stat(7) && !Stat(8) && !Stat(14),
        "Old attribution entered a newly armed capture");
    trace::Finish(true);Finished(8,0);
    const D3D12_CPU_DESCRIPTOR_HANDLE handle{0x100};
    for(const bool clear:{false,true}){
        h.Fresh();const auto epoch=Stat(3);rearmTargets=true;
        if(clear)trace::ClearView(h.lists[0].Get(),handle,false);
        else trace::Targets(h.lists[0].Get(),1,&handle,FALSE,nullptr);
        Require(trace::Active() && Stat(3)==epoch+1 && !Stat(4) && !Stat(5) && !Stat(6) && !Stat(14),
            "Pre-arm target resolution entered a new capture");
        trace::Finish(true);Finished(8,0);
    }
}
void ParentIdentity(Harness& h){
    h.Fresh();h.Read(0);++at[0].child.childObject;++at[0].child.childVtable;++at[0].child.childWork;
    ++at[0].child.childSlot;++at[0].child.invocation;++at[0].child.parentInvocation;
    h.Read(0);h.Read(1);h.Close(0);h.Close(1);trace::Finish();Finished(0,2);
    Require(Stat(7)==3 && Event(0).child!=Event(1).child,"Valid sibling child was rejected");
    const auto reject=[&](auto mutation){h.Fresh();h.Read(0);mutation(at[0]);h.Read(0);h.Read(1);h.Close(0);h.Close(1);trace::Finish();
        Finished(3,2);Require(Event(1).child==UINT32_MAX && Stat(10)==1 && Stat(7)==2,"Mixed parent attribution was not rejected");};
    reject([](auto& a){++a.child.consumerId;});reject([](auto& a){++a.child.epoch;});
    reject([](auto& a){++a.child.pairGeneration;});reject([](auto& a){++a.child.frameOwner;});
    reject([](auto& a){++a.child.sourceGraph;});reject([](auto& a){++a.child.executionGraph;});
    reject([](auto& a){++a.child.rootObject;});reject([](auto& a){++a.child.workContext;});
    reject([](auto& a){++a.child.rootHandle;});reject([](auto& a){a.child.view=0;});
    reject([](auto& a){a.child.section=1;});reject([](auto& a){++a.native.list;});
    reject([](auto& a){++a.native.generation;});reject([](auto& a){++a.native.context;});
    reject([](auto& a){++a.native.registry;});reject([](auto& a){a.native.token.Attach(new Owner);});
}
void PartialTargets(Harness& h){
    h.Fresh();trace::Shader(h.lists[0].Get(),true);h.Read(1);h.Close(0);h.Close(1);trace::Finish();Finished(0,2);
    Require(Stat(6)==3 && Stat(9)==1 && Event(0).kind==uint32_t(trace::Kind::Shader) &&
        Event(1).kind==uint32_t(trace::Kind::Unknown) && Event(1).tag==8 && !Recording(0)[2],"Partial recording falsely knew empty OM state");
    h.Fresh();trace::Reset(h.lists[0].Get());trace::Shader(h.lists[0].Get(),true);h.Read(1);h.Close(0);h.Close(1);trace::Finish();Finished(0,2);
    Require(Stat(6)==2 && Recording(0)[2] && Event(0).kind==uint32_t(trace::Kind::Shader),"Observed reset did not establish empty OM state");
    h.Fresh();const D3D12_CPU_DESCRIPTOR_HANDLE handle{0x100};trace::Targets(h.lists[0].Get(),1,&handle,FALSE,nullptr);
    trace::Shader(h.lists[0].Get(),true);h.Read(1);h.Close(0);h.Close(1);trace::Finish();Finished(0,2);
    Require(Stat(6)==4 && Event(0).kind==uint32_t(trace::Kind::Targets) && Event(1).kind==uint32_t(trace::Kind::Shader) &&
        Event(2).kind==uint32_t(trace::Kind::Write) && Event(0).resource==Event(2).resource,"Actual OM bind did not establish target writes");
}
void Lifetime(Harness& h){
    static constexpr GUID markerKey{0xb3c408b1,0x5ab4,0x49ab,{0xbb,0x63,0x1c,0x4e,0xf9,0x11,0x57,0x66}};
    std::atomic<uint32_t> destroyed{};auto resource=h.Buffer();
    ComPtr<IUnknown> marker;marker.Attach(new Owner(&destroyed));
    Hr(resource->SetPrivateDataInterface(markerKey,marker.Get()),"Lifetime marker failed");marker.Reset();
    h.Fresh();h.Read(0,resource.Get());h.Read(1,resource.Get());h.Close(0);h.Close(1);trace::Finish();Finished(0,2);
    Require(Stat(5)==1,"Same actual resource was retained as duplicate rows");resource.Reset();
    Require(!destroyed.load(),"Finish released the actual resource before readers completed");
    trace::Arm();Require(destroyed.load()==1,"Next Arm did not release the finished capture resource");trace::Finish(true);Finished(8,0);
}
void ImmediateWrites(Harness& h){
    ComPtr<ID3D12GraphicsCommandList2> list;Hr(h.lists[0].As(&list),"List2 interface unavailable");
    const auto begin=[&]{h.Fresh();at[0].list=list.Get();at[0].native.list=uint64_t(list.Get());};
    const auto finish=[&]{h.Read(1);trace::Close(list.Get(),S_OK);h.Close(1);trace::Finish();Finished(0,2);};
    const auto write=[&](uint64_t offset=20){
        const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER parameter{breadcrumbResource->GetGPUVirtualAddress()+offset,1};
        const D3D12_WRITEBUFFERIMMEDIATE_MODE mode=D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN;
        trace::ImmediateWrite(list.Get(),1,&parameter,&mode,BreadcrumbCaller);
    };
    begin();write();finish();
    Require(Stat(6)==2 && Event(0).kind==uint32_t(trace::Kind::Write) && Event(0).tag==13 &&
        Event(0).offset==20 && Event(0).bytes==4 && Event(0).subresource==0 && Event(0).resource!=trace::NoResource &&
        Event(0).child!=UINT32_MAX && !Event(0).internal,"Authenticated marker did not become an exact four-byte write");
    begin();const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER outgoing{breadcrumbResource->GetGPUVirtualAddress()+24,2};
    const D3D12_WRITEBUFFERIMMEDIATE_MODE outMode=D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT;
    trace::ImmediateWrite(list.Get(),1,&outgoing,&outMode,BreadcrumbCaller);finish();
    Require(Event(0).kind==uint32_t(trace::Kind::Write) && Event(0).offset==24 && Event(0).bytes==4,
        "Authenticated outgoing marker stayed unknown");
    begin();write(h.buffer->GetDesc().Width-4);finish();
    Require(Event(0).kind==uint32_t(trace::Kind::Write) && Event(0).offset==h.buffer->GetDesc().Width-4,
        "Last complete buffer DWORD was rejected");
    begin();metadataMissing=true;write();finish();
    Require(Event(0).kind==uint32_t(trace::Kind::Write) && !Stat(11),"Authenticated breadcrumb metadata did not upgrade an unknown resource");
    const auto* resourceRows=reinterpret_cast<const std::array<uint64_t,16>*>(CyberpunkVR_SceneAccessResources);
    Require(resourceRows && resourceRows[Event(0).resource][2]==1 && resourceRows[Event(0).resource][14]==1,
        "Authenticated creation metadata was not published");
    begin();metadataMissing=true;trace::Operand(list.Get(),h.buffer.Get(),false,3,0,0,64);
    Require(Stat(11)==1,"Metadata-upgrade fixture did not begin unknown");write();finish();
    Require(!Stat(11) && Event(1).kind==uint32_t(trace::Kind::Write) && Event(0).resource==Event(1).resource,
        "Existing resource row was not upgraded in place");
    for(uint32_t invalid=0;invalid<13;++invalid){begin();
        D3D12_WRITEBUFFERIMMEDIATE_PARAMETER parameter{breadcrumbResource->GetGPUVirtualAddress()+20,1};
        auto mode=D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN;UINT count=1;auto caller=BreadcrumbCaller;
        if(invalid==0)breadcrumbCapture=false;
        if(invalid==1)breadcrumbDescribe=false;
        if(invalid==2)++caller;
        if(invalid==3)count=2;
        if(invalid==4)mode=D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT;
        if(invalid==5)parameter.Value=42;
        if(invalid==6)parameter.Dest=breadcrumbResource->GetGPUVirtualAddress()-4;
        if(invalid==7)parameter.Dest=breadcrumbResource->GetGPUVirtualAddress()+breadcrumbResource->GetDesc().Width;
        if(invalid==8)++parameter.Dest;
        if(invalid>=9)breadcrumbCorrupt=invalid-8;
        trace::ImmediateWrite(list.Get(),count,&parameter,&mode,caller);finish();
        Require(Event(0).kind==uint32_t(trace::Kind::Unknown) && Event(0).tag==13 && Event(0).resource==trace::NoResource &&
            Event(0).bytes==UINT64_MAX,"Unauthenticated or out-of-bounds immediate write was declared known");
    }
    begin();trace::ImmediateWrite(list.Get(),0,nullptr,nullptr,0);
    Require(!Stat(6) && !Stat(4),"Zero immediate writes emitted an event");trace::Finish(true);Finished(8,0);
    for(uint32_t rearm=0;rearm<3;++rearm){begin();const auto previous=Stat(3);
        rearmBreadcrumbCapture=rearm==0;rearmBreadcrumbDescribe=rearm==1;rearmAt=rearm==2;
        write();Require(trace::Active() && Stat(3)==previous+1 && !Stat(4) && !Stat(5) && !Stat(6) && !Stat(7) && !Stat(14),
            "Pre-arm breadcrumb resolution or attribution entered a new capture");
        trace::Finish(true);Finished(8,0);
    }
    static constexpr GUID key{0xda1e6f8f,0x9c60,0x431e,{0xa7,0x5c,0xbe,0xb3,0x67,0x2e,0x45,0xac}};
    std::atomic<uint32_t> destroyed{};begin();auto resource=h.Buffer();breadcrumbResource=resource;
    ComPtr<IUnknown> marker;marker.Attach(new Owner(&destroyed));
    Hr(resource->SetPrivateDataInterface(key,marker.Get()),"Breadcrumb lifetime marker failed");marker.Reset();
    write();breadcrumbResource.Reset();resource.Reset();
    Require(!destroyed.load(),"Immediate-write receipt failed to retain the actual resource");finish();
    Require(!destroyed.load(),"Finished journal released breadcrumb before consumption");
    trace::Arm();Require(destroyed.load()==1,"Next Arm retained finished breadcrumb unnecessarily");trace::Finish(true);Finished(8,0);
}
}
int main()try{
    fixture::Harness harness;fixture::Lifecycle(harness);fixture::EpochIsolation(harness);
    fixture::ParentIdentity(harness);fixture::PartialTargets(harness);fixture::Lifetime(harness);fixture::ImmediateWrites(harness);
    std::puts("PASS passive scene access epochs, parent isolation, exact breadcrumb writes, partial OM, closes and WARP resource lifetime; no GPU queue");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
