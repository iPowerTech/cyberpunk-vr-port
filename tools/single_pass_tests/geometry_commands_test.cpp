#include "Render/StereoGeometryCommands.hpp"
#include "Render/InstanceRebase.hpp"
#include "Render/NativeIndirectPolicy.hpp"
#include "Render/CommandResources.hpp"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
using namespace cvr::stereo;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Check(HRESULT value){Require(SUCCEEDED(value),"D3D12 setup failed");}
static ComPtr<ID3D12Resource> Upload(ID3D12Device* device,const void* data,size_t bytes) {
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&resource)));
    void* mapped{};Check(resource->Map(0,nullptr,&mapped));std::memcpy(mapped,data,bytes);resource->Unmap(0,nullptr);return resource;
}
static geometry::BufferLease Lease(ID3D12Resource* resource) {
    const auto base=resource->GetGPUVirtualAddress(),bytes=resource->GetDesc().Width;
    return geometry::AcquireBuffer({reinterpret_cast<uintptr_t>(resource),base,bytes},base,bytes);
}
namespace {bool sourcePolicyEnabled=true;}
namespace cvr::stereo::native_probe {bool ObserveBindings(){return sourcePolicyEnabled;}}
namespace {
struct SourceDevice final:IUnknown {
    ULONG refs=1;
    ID3D12Device* Device(){return reinterpret_cast<ID3D12Device*>(this);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown) && id!=__uuidof(ID3D12Device))return E_NOINTERFACE;
        *out=Device();AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs;}
};
struct SourceBuffer final:ID3D12Resource {
    SourceDevice& device;ULONG refs=1;UINT64 address{},bytes{};
    D3D12_RESOURCE_DIMENSION dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    SourceBuffer(SourceDevice& owner,UINT64 gpu,UINT64 size):device(owner),address(gpu),bytes(size){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown) && id!=__uuidof(ID3D12Resource))return E_NOINTERFACE;
        *out=static_cast<ID3D12Resource*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id,void** out)override{return device.QueryInterface(id,out);}
    HRESULT STDMETHODCALLTYPE Map(UINT,const D3D12_RANGE*,void**)override{return E_NOTIMPL;}
    void STDMETHODCALLTYPE Unmap(UINT,const D3D12_RANGE*)override{}
    D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc()override{
        D3D12_RESOURCE_DESC desc{};desc.Dimension=dimension;desc.Width=bytes;
        desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;return desc;
    }
    D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress()override{return address;}
    HRESULT STDMETHODCALLTYPE WriteToSubresource(UINT,const D3D12_BOX*,const void*,UINT,UINT)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ReadFromSubresource(void*,UINT,UINT,UINT,const D3D12_BOX*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES* heap,D3D12_HEAP_FLAGS* flags)override{
        if(!heap || !flags)return E_POINTER;*heap={};heap->Type=D3D12_HEAP_TYPE_DEFAULT;*flags=D3D12_HEAP_FLAG_NONE;return S_OK;
    }
    geometry::BufferLease Lease(){return {this,address,bytes,D3D12_HEAP_TYPE_DEFAULT,0};}
};
struct SourceSignature final:ID3D12CommandSignature {
    SourceDevice& device;ULONG refs=1;GUID key{};indirect_policy::detail::Receipt receipt{};bool observed{};
    explicit SourceSignature(SourceDevice& owner):device(owner){}
    void Observe(D3D12_INDIRECT_ARGUMENT_TYPE type,UINT stride,UINT nodes=0){
        D3D12_INDIRECT_ARGUMENT_DESC argument{};argument.Type=type;
        const D3D12_COMMAND_SIGNATURE_DESC desc{stride,1,&argument,nodes};indirect_policy::Created(this,desc);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown) && id!=__uuidof(ID3D12CommandSignature))return E_NOINTERFACE;
        *out=static_cast<ID3D12CommandSignature*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID requested,UINT* bytes,void* data)override{
        if(!bytes || !data)return E_POINTER;if(!observed || !IsEqualGUID(requested,key))return DXGI_ERROR_NOT_FOUND;
        if(*bytes<sizeof(receipt))return E_INVALIDARG;*bytes=sizeof(receipt);std::memcpy(data,&receipt,sizeof(receipt));return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID requested,UINT bytes,const void* data)override{
        if(bytes!=sizeof(receipt) || !data)return E_INVALIDARG;key=requested;std::memcpy(&receipt,data,bytes);observed=true;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id,void** out)override{return device.QueryInterface(id,out);}
};
struct SourceEmission {
    D3D12_INDEX_BUFFER_VIEW index{};
    std::array<D3D12_VERTEX_BUFFER_VIEW,16> vertices{};
    UINT mask{},indices{},instances{},first{};INT base{};UINT start{};
    bool indexBound{},indirect{};ID3D12CommandSignature* signature{};UINT maxCommandCount{};
    ID3D12Resource* arguments{};UINT64 argumentOffset{};ID3D12Resource* countBuffer{};UINT64 countOffset{};
};
// Only the exercised COM slots exist. No factory, driver or GPU is involved.
struct SourceList {
    void** vtable{};std::array<void*,60> methods{};
    SourceDevice& device;ULONG refs=1;ComPtr<IUnknown> kept;
    D3D12_COMMAND_LIST_TYPE type=D3D12_COMMAND_LIST_TYPE_DIRECT;bool allowKeep=true;
    SourceEmission current;std::vector<SourceEmission> emitted;
    UINT indexCalls{},vertexCalls{},indirectCalls{};
    explicit SourceList(SourceDevice& owner):device(owner){
        vtable=methods.data();methods[0]=reinterpret_cast<void*>(&Query);methods[1]=reinterpret_cast<void*>(&Add);
        methods[2]=reinterpret_cast<void*>(&Release);methods[5]=reinterpret_cast<void*>(&Keep);
        methods[7]=reinterpret_cast<void*>(&Device);methods[8]=reinterpret_cast<void*>(&Type);
        methods[13]=reinterpret_cast<void*>(&Draw);methods[43]=reinterpret_cast<void*>(&Index);methods[44]=reinterpret_cast<void*>(&Vertices);
        methods[59]=reinterpret_cast<void*>(&Indirect);
    }
    ~SourceList(){cvr::gpu::ResetCommandResources(List());}
    ID3D12GraphicsCommandList* List(){return reinterpret_cast<ID3D12GraphicsCommandList*>(this);}
    static HRESULT STDMETHODCALLTYPE Query(SourceList* self,REFIID id,void** out){
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=__uuidof(IUnknown) && id!=__uuidof(ID3D12CommandList) && id!=__uuidof(ID3D12GraphicsCommandList))return E_NOINTERFACE;
        *out=self->List();++self->refs;return S_OK;
    }
    static ULONG STDMETHODCALLTYPE Add(SourceList* self){return ++self->refs;}
    static ULONG STDMETHODCALLTYPE Release(SourceList* self){return --self->refs;}
    static HRESULT STDMETHODCALLTYPE Keep(SourceList* self,REFGUID,const IUnknown* owner){
        if(!self->allowKeep)return E_FAIL;self->kept=const_cast<IUnknown*>(owner);return S_OK;
    }
    static HRESULT STDMETHODCALLTYPE Device(SourceList* self,REFIID id,void** out){return self->device.QueryInterface(id,out);}
    static D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE Type(SourceList* self){return self->type;}
    static void STDMETHODCALLTYPE Index(SourceList* self,const D3D12_INDEX_BUFFER_VIEW* view){
        Require(view!=nullptr,"Exact source emitted a null index view");self->current.index=*view;self->current.indexBound=true;++self->indexCalls;
    }
    static void STDMETHODCALLTYPE Vertices(SourceList* self,UINT start,UINT count,const D3D12_VERTEX_BUFFER_VIEW* views){
        Require(start<16 && count==1 && views,"Exact source changed IA slot binding shape");
        self->current.vertices[start]=*views;self->current.mask|=1u<<start;++self->vertexCalls;
    }
    static void STDMETHODCALLTYPE Draw(SourceList* self,UINT indices,UINT instances,UINT first,INT base,UINT start){
        self->current.indices=indices;self->current.instances=instances;self->current.first=first;self->current.base=base;self->current.start=start;
        self->emitted.push_back(self->current);self->current={};
    }
    static void STDMETHODCALLTYPE Indirect(SourceList* self,ID3D12CommandSignature* signature,UINT maximum,ID3D12Resource* arguments,
        UINT64 argumentOffset,ID3D12Resource* countBuffer,UINT64 countOffset){
        self->current.indirect=true;self->current.signature=signature;self->current.maxCommandCount=maximum;
        self->current.arguments=arguments;self->current.argumentOffset=argumentOffset;
        self->current.countBuffer=countBuffer;self->current.countOffset=countOffset;++self->indirectCalls;
        self->emitted.push_back(self->current);self->current={};
    }
};
void SourceTests(){
    SourceDevice device,foreignDevice;
    SourceBuffer index(device,0x10000,4096),mesh(device,0x20000,4096),damage(device,0x30000,4096),
        instance48(device,0x40000,4096),instance64(device,0x50000,4096),highSlot(device,0x60000,4096),foreign(foreignDevice,0x70000,4096);
    std::array<geometry::DrawResources,3> sources;
    for(UINT i=0;i<2;++i){
        auto& source=sources[i];auto& draw=source.draw;const UINT stride=i?64u:48u;
        source.index=index.Lease();source.vertices[0]=mesh.Lease();source.vertices[7]=i?instance64.Lease():instance48.Lease();
        draw.flags=128;draw.inputSlotMask=i?129u:0x8091u;draw.indices=i?9u:6u;draw.instances=i?2u:3u;
        draw.firstIndex=i?3u:2u;draw.baseVertex=i?7:-5;draw.firstInstance=i?9u:4u;
        draw.index={index.address+(i?16:8),256,i?DXGI_FORMAT_R32_UINT:DXGI_FORMAT_R16_UINT};
        draw.vertices[0]={mesh.address+32,256,16};
        draw.vertices[7]={source.vertices[7].gpu+128,1024,stride};
        if(!i){source.vertices[4]=damage.Lease();source.vertices[15]=highSlot.Lease();
            draw.vertices[4]={damage.address+64,512,8};draw.vertices[15]={highSlot.address+512,256,4};}
    }
    sources[2]=sources[0];const auto expected=std::array{sources[0].draw,sources[1].draw,sources[2].draw};
    auto reject=[&](const geometry::DrawResources& bad,const char* why){
        StereoGeometryCommands rejected;const std::array inputs{sources[0],bad};
        Require(FAILED(rejected.CreateSources(device.Device(),inputs)),why);
        Require(!rejected.SourceCount() && !rejected.DrawCount() && !rejected.InstanceBytes(),"Rejected source construction published a prefix");
    };
    auto bad=sources[0];bad.draw.flags=0;reject(bad,"Unknown consumed IA mask accepted");
    bad=sources[0];bad.draw.inputSlotMask|=1u<<16;reject(bad,"Unsupported consumed high IA slot accepted");
    bad=sources[0];bad.draw.inputSlotMask&=~128u;reject(bad,"Missing native instance stream accepted");
    bad=sources[0];bad.draw.indices=0;reject(bad,"Empty source index count accepted");
    bad=sources[0];bad.draw.instances=0;reject(bad,"Empty source instance count accepted");
    bad=sources[0];bad.draw.index.Format=DXGI_FORMAT_R8_UINT;reject(bad,"Invalid index format accepted");
    bad=sources[0];++bad.draw.index.BufferLocation;reject(bad,"Misaligned index view accepted");
    bad=sources[0];bad.draw.index.SizeInBytes=8192;reject(bad,"Oversized index view accepted");
    bad=sources[0];bad.draw.firstIndex=UINT_MAX;reject(bad,"Original firstIndex overflow accepted");
    bad=sources[0];bad.draw.indices=UINT_MAX;reject(bad,"Original index count overflow accepted");
    bad=sources[0];bad.draw.firstInstance=UINT_MAX;reject(bad,"Original firstInstance overflow accepted");
    bad=sources[0];bad.draw.instances=UINT_MAX;reject(bad,"Original instance count overflow accepted");
    bad=sources[0];bad.draw.vertices[7].StrideInBytes=32;reject(bad,"Unsupported source instance stride accepted");
    bad=sources[0];bad.draw.vertices[7].SizeInBytes=(bad.draw.firstInstance+bad.draw.instances)*48-1;reject(bad,"Incomplete original instance batch accepted");
    bad=sources[0];bad.draw.vertices[4].BufferLocation=damage.address+4096;reject(bad,"Damage stream range escaped its lease");
    bad=sources[0];bad.vertices[15].resource.Reset();reject(bad,"Unowned consumed high IA slot accepted");
    bad=sources[0];bad.index.resource.Reset();reject(bad,"Unowned index buffer accepted");
    bad=sources[0];bad.vertices[4].gpu+=32;reject(bad,"Lease GPU base mismatched its actual resource");
    bad=sources[0];bad.vertices[4].bytes=8192;reject(bad,"Lease bytes exceeded actual resource bounds");
    bad=sources[0];bad.vertices[4].gpu=UINT64_MAX-4;reject(bad,"Overflowing source lease accepted");
    bad=sources[0];bad.vertices[4]=foreign.Lease();bad.draw.vertices[4].BufferLocation=foreign.address+64;reject(bad,"Foreign resource device accepted");
    damage.dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;reject(sources[0],"Nonbuffer IA resource accepted");damage.dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    StereoGeometryCommands empty;
    Require(FAILED(empty.CreateSources(nullptr,sources)) && FAILED(empty.CreateSources(device.Device(),{})),"Missing device/empty source stream accepted");
    std::vector<geometry::DrawResources> tooMany(StereoGeometryCommands::MaxSourceCommands+1);
    Require(FAILED(empty.CreateSources(device.Device(),tooMany)),"Source command limit ignored");
    auto commands=std::make_unique<StereoGeometryCommands>();Check(commands->CreateSources(device.Device(),sources));
    Require(commands->SourceCount()==3 && !commands->DrawCount() && !commands->InstanceBytes(),"Exact sources were paired, compacted or deduplicated");
    Require(FAILED(commands->CreateSources(device.Device(),sources)),"Exact source storage was reinitialized");
    GeometryCommandPair pair;
    Require(FAILED(commands->Create(device.Device(),{&pair,1})),"Paired initialization overwrote exact source storage");
    SourceList list(device),wrongList(foreignDevice);
    Require(!commands->RecordSourceGeometry(nullptr,0) && !commands->RecordSourceGeometry(list.List(),3) &&
        !commands->RecordSourceGeometry(wrongList.List(),0) && !commands->RecordGeometry(list.List(),0),"Invalid source recording/mixed initialization accepted");
    list.type=D3D12_COMMAND_LIST_TYPE_COPY;Require(!commands->RecordSourceGeometry(list.List(),0),"COPY destination accepted");list.type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    list.allowKeep=false;Require(!commands->RecordSourceGeometry(list.List(),0),"Recording continued after keepalive failure");list.allowKeep=true;
    Require(!list.indexCalls && !list.vertexCalls && list.emitted.empty(),"Rejected recording emitted geometry");
    for(auto& source:sources){source.draw={};source.index={};source.vertices={};source.instances.assign(1024,0xFF);}
    bad={};
    for(UINT i=0;i<expected.size();++i)Require(commands->RecordSourceGeometry(list.List(),i),"Exact source draw did not record");
    Require(list.emitted.size()==3 && list.indexCalls==3 && list.vertexCalls==10,"Exact source command/IA counts changed");
    for(UINT i=0;i<expected.size();++i){const auto& actual=list.emitted[i];const auto& draw=expected[i];
        Require(actual.indices==draw.indices && actual.instances==draw.instances && actual.first==draw.firstIndex &&
            actual.base==draw.baseVertex && actual.start==draw.firstInstance && actual.mask==draw.inputSlotMask,"Exact source original draw arguments or mask changed");
        Require(!std::memcmp(&actual.index,&draw.index,sizeof(draw.index)),"Exact source original index view changed");
        for(UINT slot=0;slot<16;++slot)if(draw.inputSlotMask&(1u<<slot))
            Require(!std::memcmp(&actual.vertices[slot],&draw.vertices[slot],sizeof(draw.vertices[slot])),"Exact source original vertex/instance view changed");
    }
    ID3D12CommandList* submitted=list.List();auto flight=cvr::gpu::PrepareCommandResources(1,&submitted);
    Require(flight.batches.size()==1,"Exact source storage was not retained on its destination recording");
    commands.reset();cvr::gpu::ResetCommandResources(list.List());
    Require(index.refs>1 && mesh.refs>1 && damage.refs>1 && instance48.refs>1 && instance64.refs>1 && highSlot.refs>1,
        "Exact source owners expired while destination submission batch retained them");
    flight.batches.clear();
    Require(index.refs==1 && mesh.refs==1 && damage.refs==1 && instance48.refs==1 && instance64.refs==1 && highSlot.refs==1 && foreign.refs==1,
        "Exact source owners leaked after helper, source and destination references retired");
    Require(device.refs==1 && foreignDevice.refs==1,"Exact source device ownership leaked");
}
void MixedSourceTests(){
    SourceDevice device,foreignDevice;
    SourceBuffer index(device,0x10000,4096),mesh(device,0x20000,4096),damage(device,0x30000,4096),
        instance48(device,0x40000,4096),instance64(device,0x50000,4096),arguments(device,0x60000,256),counts(device,0x70000,128),foreign(foreignDevice,0x80000,4096);
    SourceSignature indexed(device),draw(device),unknown(device),changingRoot(device),changingIa(device),multi(device),
        invalidStride(device),invalidNodes(device),foreignSignature(foreignDevice);
    indexed.Observe(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED,32);draw.Observe(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW,24,1);
    changingRoot.Observe(D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT,32);changingIa.Observe(D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW,32);
    invalidStride.Observe(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED,18);invalidNodes.Observe(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED,32,2);
    foreignSignature.Observe(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED,32);
    std::array<D3D12_INDIRECT_ARGUMENT_DESC,2> operations{};operations[0].Type=D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    operations[1].Type=D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
    const D3D12_COMMAND_SIGNATURE_DESC changing{32,2,operations.data(),0};indirect_policy::Created(&multi,changing);
    Require(indirect_policy::DrawKind(&indexed)==1 && indirect_policy::PureDrawStride(&indexed)==32 &&
        indirect_policy::DrawKind(&draw)==2 && indirect_policy::PureDrawStride(&draw)==24,"Actual native pure signature receipt was not used");
    geometry::DrawResources direct;direct.index=index.Lease();direct.vertices[0]=mesh.Lease();
    direct.vertices[4]=damage.Lease();direct.vertices[7]=instance48.Lease();
    auto& native=direct.draw;native.flags=128;native.inputSlotMask=145;native.indices=6;native.instances=3;
    native.firstIndex=2;native.baseVertex=-3;native.firstInstance=4;native.index={index.address+8,128,DXGI_FORMAT_R16_UINT};
    native.vertices[0]={mesh.address+32,256,16};native.vertices[4]={damage.address+64,512,8};native.vertices[7]={instance48.address+128,1024,48};
    std::array<GeometrySourceCommand,7> sources;
    sources[0].geometry=direct;
    sources[1].geometry=direct;sources[1].indirect=PureIndirectSource{&indexed,3,&arguments,64,&counts,16};
    sources[1].geometry.draw.indices=sources[1].geometry.draw.instances=sources[1].geometry.draw.firstIndex=sources[1].geometry.draw.firstInstance=UINT_MAX;
    sources[2].geometry=direct;sources[2].geometry.vertices[7]=instance64.Lease();
    sources[2].geometry.draw.vertices[7]={instance64.address+256,1024,64};sources[2].geometry.draw.firstInstance=9;sources[2].geometry.draw.instances=2;
    sources[3].geometry=sources[2].geometry;sources[3].geometry.index={};sources[3].geometry.draw.inputSlotMask=129;
    sources[3].indirect=PureIndirectSource{&draw,2,&arguments,128,nullptr,64};
    sources[4]=sources[1];
    sources[5].geometry.draw.flags=128;sources[5].indirect=PureIndirectSource{&draw,0,&arguments,arguments.bytes,nullptr,32};
    sources[6].geometry=direct;sources[6].indirect=PureIndirectSource{&draw,1,&arguments,16,&counts,0};
    std::array<SourceEmission,7> expected{};
    for(UINT i=0;i<sources.size();++i){const auto& source=sources[i];const auto& drawState=source.geometry.draw;auto& entry=expected[i];
        entry.index=drawState.index;entry.vertices=drawState.vertices;entry.mask=drawState.inputSlotMask;entry.indexBound=bool(source.geometry.index.resource);
        entry.indices=drawState.indices;entry.instances=drawState.instances;entry.first=drawState.firstIndex;entry.base=drawState.baseVertex;entry.start=drawState.firstInstance;
        if(source.indirect){const auto& indirect=*source.indirect;entry.indirect=true;entry.signature=indirect.signature.Get();
            entry.maxCommandCount=indirect.maxCommandCount;entry.arguments=indirect.arguments.Get();entry.argumentOffset=indirect.argumentOffset;
            entry.countBuffer=indirect.countBuffer.Get();entry.countOffset=indirect.countOffset;}
    }
    auto reject=[&](const GeometrySourceCommand& bad,const char* why){
        StereoGeometryCommands rejected;const std::array inputs{sources[0],bad};
        Require(FAILED(rejected.CreateMixedSources(device.Device(),inputs)),why);
        Require(!rejected.SourceCount(),"Rejected mixed source construction published a prefix");
    };
    auto bad=sources[1];bad.indirect->signature.Reset();reject(bad,"Missing native indirect signature accepted");
    for(auto* rejected:{&unknown,&changingRoot,&changingIa,&multi,&invalidStride,&invalidNodes,&foreignSignature}){
        bad=sources[1];bad.indirect->signature=rejected;reject(bad,"Unobserved/mutating/malformed/foreign indirect signature accepted");
    }
    bad=sources[1];bad.indirect->arguments.Reset();reject(bad,"Missing native indirect arguments accepted");
    bad=sources[1];bad.indirect->arguments=&foreign;reject(bad,"Foreign indirect argument buffer accepted");
    bad=sources[1];bad.indirect->countBuffer=&foreign;reject(bad,"Foreign indirect count buffer accepted");
    bad=sources[1];bad.indirect->argumentOffset=65;reject(bad,"Unaligned indirect argument offset accepted");
    bad=sources[1];bad.indirect->countOffset=17;reject(bad,"Unaligned indirect count offset accepted");
    bad=sources[1];bad.indirect->argumentOffset=196;reject(bad,"Indirect argument bounds ignored actual padded stride");
    bad=sources[1];bad.indirect->argumentOffset=UINT64_MAX-3;reject(bad,"Overflowing indirect argument offset accepted");
    bad=sources[1];bad.indirect->maxCommandCount=UINT_MAX;reject(bad,"Indirect argument count escaped source buffer");
    bad=sources[1];bad.indirect->countOffset=counts.bytes;reject(bad,"Indirect count value escaped source buffer");
    bad=sources[1];bad.geometry.index.resource.Reset();reject(bad,"Pure indexed indirect lost IB ownership");
    bad=sources[1];bad.geometry.vertices[7].resource.Reset();reject(bad,"Indirect instance stream lost ownership");
    bad=sources[1];bad.geometry.draw.inputSlotMask|=1u<<16;reject(bad,"Indirect used an unsupported IA slot");
    arguments.dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;reject(sources[1],"Nonbuffer indirect arguments accepted");arguments.dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    counts.dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;reject(sources[1],"Nonbuffer indirect count accepted");counts.dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    sourcePolicyEnabled=false;reject(sources[1],"Unobserved native signature policy accepted");sourcePolicyEnabled=true;
    auto commands=std::make_unique<StereoGeometryCommands>();Check(commands->CreateMixedSources(device.Device(),sources));
    Require(commands->SourceCount()==sources.size() && !commands->DrawCount() && !commands->InstanceBytes(),"Mixed source occurrences were dropped, paired or deduplicated");
    Require(FAILED(commands->CreateMixedSources(device.Device(),sources)) && FAILED(commands->CreateSources(device.Device(),{&direct,1})),"Mixed source storage was reinitialized");
    SourceList list(device),wrongList(foreignDevice);
    Require(!commands->RecordSourceGeometry(wrongList.List(),1),"Foreign indirect destination accepted");
    const auto original=indexed.receipt;indexed.receipt.stride=64;
    Require(!commands->RecordSourceGeometry(list.List(),1),"Changed native signature receipt replayed");indexed.receipt=original;
    sourcePolicyEnabled=false;Require(!commands->RecordSourceGeometry(list.List(),1),"Lost native signature observation replayed");sourcePolicyEnabled=true;
    list.allowKeep=false;Require(!commands->RecordSourceGeometry(list.List(),1),"Indirect replay survived keepalive failure");list.allowKeep=true;
    Require(!list.indexCalls && !list.vertexCalls && !list.indirectCalls && list.emitted.empty(),"Rejected indirect replay emitted commands");
    for(auto& source:sources)source={};bad={};direct={};
    for(UINT i=0;i<expected.size();++i)Require(commands->RecordSourceGeometry(list.List(),i),"Mixed source command did not record");
    Require(list.emitted.size()==7 && list.indexCalls==5 && list.vertexCalls==17 && list.indirectCalls==5,"Mixed command order/count or optional IA bindings changed");
    for(UINT i=0;i<expected.size();++i){const auto& actual=list.emitted[i];const auto& entry=expected[i];
        Require(actual.indirect==entry.indirect && actual.mask==entry.mask && actual.indexBound==entry.indexBound,"Mixed source order/kind/IA mask changed");
        if(entry.indexBound)Require(!std::memcmp(&actual.index,&entry.index,sizeof(entry.index)),"Mixed source original index view changed");
        for(UINT slot=0;slot<16;++slot)if(entry.mask&(1u<<slot))
            Require(!std::memcmp(&actual.vertices[slot],&entry.vertices[slot],sizeof(entry.vertices[slot])),"Mixed source original vertex/instance view changed");
        if(entry.indirect)Require(actual.signature==entry.signature && actual.maxCommandCount==entry.maxCommandCount &&
            actual.arguments==entry.arguments && actual.argumentOffset==entry.argumentOffset && actual.countBuffer==entry.countBuffer && actual.countOffset==entry.countOffset,
            "Exact ExecuteIndirect operands changed");
        else Require(actual.indices==entry.indices && actual.instances==entry.instances && actual.first==entry.first && actual.base==entry.base && actual.start==entry.start,
            "Mixed indexed direct ABI changed");
    }
    ID3D12CommandList* submitted=list.List();auto flight=cvr::gpu::PrepareCommandResources(1,&submitted);
    Require(flight.batches.size()==1,"Mixed source storage was not retained by destination recording");
    commands.reset();cvr::gpu::ResetCommandResources(list.List());
    Require(index.refs>1 && mesh.refs>1 && damage.refs>1 && instance48.refs>1 && instance64.refs>1 && arguments.refs>1 && counts.refs>1 && indexed.refs>1 && draw.refs>1,
        "Mixed indirect/source owners expired while destination batch retained them");
    flight.batches.clear();
    Require(index.refs==1 && mesh.refs==1 && damage.refs==1 && instance48.refs==1 && instance64.refs==1 && arguments.refs==1 && counts.refs==1 && foreign.refs==1 &&
        indexed.refs==1 && draw.refs==1 && unknown.refs==1 && changingRoot.refs==1 && changingIa.refs==1 && multi.refs==1 && invalidStride.refs==1 && invalidNodes.refs==1 && foreignSignature.refs==1,
        "Mixed source owners leaked after destination batch retirement");
    Require(device.refs==1 && foreignDevice.refs==1,"Mixed source device ownership leaked");
}
bool DenyIndirectCopy(ID3D12GraphicsCommandList*,const IndirectCopyProof&,UINT64,UINT64){return false;}
void SnapshotProofTests(){
    SourceDevice device;SourceBuffer arguments(device,0x10000,256),counts(device,0x20000,16),other(device,0x30000,256);
    SourceSignature signature(device);signature.Observe(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED,32);
    SourceList list(device);const PureIndirectSource source{&signature,2,&arguments,8,&counts,4};
    IndirectCopyProof argumentProof{&arguments,D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,7,11,&arguments};
    IndirectCopyProof countProof{&counts,D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,7,11,&counts};
    CapturedIndirectSource output;output.receipt.commandPosition=999;
    auto reject=[&](const PureIndirectSource& input,const IndirectCopyProof& args,const IndirectCopyProof* count,ValidateIndirectCopy validator,const char* why,HRESULT expected=E_INVALIDARG){
        Require(StereoGeometryCommands::CaptureIndirectSource(list.List(),input,args,count,validator,output)==expected,why);
        Require(!output.owner && !output.replay.arguments && output.receipt.commandPosition==999,"Rejected snapshot changed output");
    };
    reject(source,argumentProof,&countProof,nullptr,"Snapshot accepted absent provenance validator");
    reject(source,argumentProof,&countProof,DenyIndirectCopy,"Snapshot copied after caller denied source admission",E_ACCESSDENIED);
    auto bad=argumentProof;bad.state.reset();reject(source,bad,&countProof,DenyIndirectCopy,"Snapshot inferred an unknown source state");
    bad=argumentProof;bad.recordingGeneration=0;reject(source,bad,&countProof,DenyIndirectCopy,"Snapshot accepted missing source generation");
    bad=argumentProof;bad.commandPosition=0;reject(source,bad,&countProof,DenyIndirectCopy,"Snapshot accepted missing source position");
    bad=argumentProof;bad.allocationOwner.Reset();reject(source,bad,&countProof,DenyIndirectCopy,"Snapshot accepted missing allocation owner");
    bad=argumentProof;bad.resource=&other;reject(source,bad,&countProof,DenyIndirectCopy,"Snapshot copy proof changed source resource");
    bad=countProof;++bad.commandPosition;reject(source,argumentProof,&bad,DenyIndirectCopy,"Argument/count snapshot positions differed");
    bad=countProof;++bad.recordingGeneration;reject(source,argumentProof,&bad,DenyIndirectCopy,"Argument/count snapshot generations differed");
    reject(source,argumentProof,nullptr,DenyIndirectCopy,"Snapshot omitted a present count buffer proof");
    auto noCount=source;noCount.countBuffer.Reset();reject(noCount,argumentProof,&countProof,DenyIndirectCopy,"Snapshot invented an absent count source");
    auto oversized=source;oversized.maxCommandCount=UINT_MAX;reject(oversized,argumentProof,&countProof,DenyIndirectCopy,"Snapshot payload budget overflow accepted");
    auto shared=source;shared.countBuffer=&arguments;bad=argumentProof;bad.state=D3D12_RESOURCE_STATE_COPY_SOURCE;
    reject(shared,argumentProof,&bad,DenyIndirectCopy,"Shared source snapshots declared conflicting states");
    Require(!list.indexCalls && !list.vertexCalls && !list.indirectCalls && list.emitted.empty(),"Rejected snapshot emitted geometry");
}
bool ValidateFixtureIndirectCopy(ID3D12GraphicsCommandList* list,const IndirectCopyProof& proof,UINT64 offset,UINT64 bytes){
    if(!list || !proof.resource || !proof.state || *proof.state!=D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT || proof.recordingGeneration!=1 ||
        !proof.commandPosition || proof.allocationOwner.Get()!=static_cast<IUnknown*>(proof.resource.Get()) || offset%4)return false;
    const auto desc=proof.resource->GetDesc();
    return desc.Dimension==D3D12_RESOURCE_DIMENSION_BUFFER && offset<=desc.Width && bytes<=desc.Width-offset;
}
}
int main(int argc,char** argv) try {
    SourceTests();MixedSourceTests();SnapshotProofTests();
    if(argc==2 && !std::strcmp(argv[1],"--source-only")){
        std::puts("PASS exact indexed/pure-indirect ABI, IA48/64/native stride receipts, bounds/device/copy-proof rejection and recording lifetime (CPU only)");return 0;
    }
    D3D12_INPUT_ELEMENT_DESC elements[]{
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"INSTANCE",0,DXGI_FORMAT_R32G32B32A32_FLOAT,7,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1}};
    std::array<uint8_t,64> container{};std::memcpy(container.data(),"DXBC",4);
    auto word=[&](size_t offset,uint32_t value){std::memcpy(container.data()+offset,&value,4);};
    word(24,64);word(28,2);word(32,40);word(36,52);std::memcpy(container.data()+40,"DXIL",4);word(44,4);word(48,0x10060);
    std::memcpy(container.data()+52,"SFI0",4);word(56,4);
    Require(!SupportsInstanceRebase({container.data(),container.size()},{elements,2}),"Malformed shader feature part accepted");
    word(28,1);Require(SupportsInstanceRebase({container.data(),container.size()},{elements,2}),"SM6.0 instance contract rejected");
    word(48,0x10068);Require(!SupportsInstanceRebase({container.data(),container.size()},{elements,2}),"SM6.8 base-instance semantic not excluded");word(48,0x10060);
    elements[1].InstanceDataStepRate=2;Require(!SupportsInstanceRebase({container.data(),container.size()},{elements,2}),"Instance step rate was ignored");elements[1].InstanceDataStepRate=1;
    ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> adapter;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    static constexpr char code[]=R"(
struct Out {float4 position:SV_Position;float4 color:COLOR0;};
Out VS(float2 vertex:POSITION,float4 instance:INSTANCE0,uint id:SV_InstanceID) {
    Out o;o.position=float4(vertex*.35+instance.xy,0,1);o.color=float4((id+1)*.25,instance.z,.75,1);return o;
}
Out MixedVS(float2 vertex:POSITION,float4 instance:INSTANCE0,float2 damage:DAMAGE0,uint id:SV_InstanceID) {
    Out o;o.position=float4((vertex+damage)*.35+instance.xy,0,1);o.color=float4((id+1)*.25,instance.z,.75,.5);return o;
}
float4 PS(Out i):SV_Target{return i.color;}
)";
    ComPtr<ID3DBlob> vs,mixedVs,ps,error;Check(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"VS","vs_5_0",0,0,&vs,&error));
    Check(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"MixedVS","vs_5_0",0,0,&mixedVs,&error));
    Check(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"PS","ps_5_0",0,0,&ps,&error));
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};rootDesc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> rootBytes;Check(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&rootBytes,&error));ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0,rootBytes->GetBufferPointer(),rootBytes->GetBufferSize(),IID_PPV_ARGS(&root)));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};pipelineDesc.pRootSignature=root.Get();pipelineDesc.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pipelineDesc.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pipelineDesc.InputLayout={elements,2};pipelineDesc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineDesc.SampleMask=UINT_MAX;pipelineDesc.SampleDesc.Count=1;pipelineDesc.NumRenderTargets=1;pipelineDesc.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
    pipelineDesc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pipelineDesc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pipelineDesc.RasterizerState.DepthClipEnable=TRUE;
    auto& blend=pipelineDesc.BlendState.RenderTarget[0];blend.RenderTargetWriteMask=15;blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;
    ComPtr<ID3D12PipelineState> pipeline;Check(device->CreateGraphicsPipelineState(&pipelineDesc,IID_PPV_ARGS(&pipeline)));
    const D3D12_INPUT_ELEMENT_DESC mixedElements[]{elements[0],elements[1],
        {"DAMAGE",0,DXGI_FORMAT_R32G32_FLOAT,4,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    auto mixedDesc=pipelineDesc;mixedDesc.VS={mixedVs->GetBufferPointer(),mixedVs->GetBufferSize()};mixedDesc.InputLayout={mixedElements,3};
    auto& mixedBlend=mixedDesc.BlendState.RenderTarget[0];mixedBlend.BlendEnable=TRUE;
    mixedBlend.SrcBlend=D3D12_BLEND_SRC_ALPHA;mixedBlend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
    mixedBlend.SrcBlendAlpha=D3D12_BLEND_ONE;mixedBlend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
    ComPtr<ID3D12PipelineState> mixedPipeline;Check(device->CreateGraphicsPipelineState(&mixedDesc,IID_PPV_ARGS(&mixedPipeline)));
    const float vertices[]{-1,-1,0,1,1,-1};const uint16_t indices[]{0,1,2};
    auto vertex=Upload(device.Get(),vertices,sizeof(vertices)),index=Upload(device.Get(),indices,sizeof(indices));
    std::array<float,24> instances{};instances[0]=-.5f;instances[2]=.25f;instances[12]=.5f;instances[14]=.8f;
    GeometryCommandPair pair;pair.instanceRebaseSafe=true;std::array<ComPtr<ID3D12Resource>,2> originalInstances;
    for(UINT eye=0;eye<2;++eye) {
        auto& source=pair.eyes[eye];auto& draw=source.draw;const UINT first=eye?9:3;
        std::vector<uint8_t> nativeInstances((first+2)*48);std::memcpy(nativeInstances.data()+first*48,instances.data(),sizeof(instances));
        originalInstances[eye]=Upload(device.Get(),nativeInstances.data(),nativeInstances.size());
        source.index=Lease(index.Get());source.vertices[0]=Lease(vertex.Get());source.vertices[7]=Lease(originalInstances[eye].Get());
        source.instances.assign(reinterpret_cast<const uint8_t*>(instances.data()),reinterpret_cast<const uint8_t*>(instances.data())+sizeof(instances));
        draw.side=1-eye;draw.frame=1;draw.pipeline=reinterpret_cast<uintptr_t>(pipeline.Get());draw.root=reinterpret_cast<uintptr_t>(root.Get());
        draw.indices=3;draw.instances=2;draw.firstInstance=first;draw.inputSlotMask=129;draw.flags=191;
        draw.index={index->GetGPUVirtualAddress(),sizeof(indices),DXGI_FORMAT_R16_UINT};
        draw.vertices[0]={vertex->GetGPUVirtualAddress(),sizeof(vertices),8};draw.vertices[7]={originalInstances[eye]->GetGPUVirtualAddress(),UINT(nativeInstances.size()),48};
        draw.viewport={0,0,128,64,0,1};draw.scissor={0,0,128,64};
    }
    auto prepared=std::make_unique<StereoGeometryCommands>();std::array<GeometryCommandPair,2> pairs{pair,pair};Check(prepared->Create(device.Get(),pairs));
    Require(prepared->DrawCount()==2 && prepared->InstanceBytes()==96,"Instance data was not deduplicated");
    Require(!prepared->SourceCount(),"Paired commands were exposed as exact sources");
    auto invalid=pair;invalid.eyes[1].instances.back()^=1;StereoGeometryCommands rejected;
    Require(FAILED(rejected.Create(device.Get(),{&invalid,1})),"Different tail instance bytes merged");
    invalid=pair;invalid.instanceRebaseSafe=false;Require(FAILED(rejected.Create(device.Get(),{&invalid,1})),"Unknown shader contract accepted");
    for(auto& input:pairs)for(auto& eye:input.eyes)std::fill(eye.instances.begin(),eye.instances.end(),0xFF);
    // Offset prefixes make a changed view, base vertex or first instance visible.
    const float mixedVertices[]{99,99,88,88,-2,-2,-1,-1,0,1,1,-1};
    const float damageVertices[]{99,99,88,88,77,77,.03f,-.02f,.03f,-.02f,.03f,-.02f};
    const uint16_t mixedIndices[]{0,0,0,0,0,0,1,2};
    auto mixedVertex=Upload(device.Get(),mixedVertices,sizeof(mixedVertices));
    auto mixedDamage=Upload(device.Get(),damageVertices,sizeof(damageVertices));
    auto mixedIndex=Upload(device.Get(),mixedIndices,sizeof(mixedIndices));
    std::array<float,12*12> data48{};std::array<float,12*16> data64{};
    for(UINT i=0;i<12;++i){data48[i*12]=data48[i*12+1]=data64[i*16]=data64[i*16+1]=10;}
    auto instance=[&](auto& data,UINT stride,UINT at,float x,float y,float color){data[at*stride]=x;data[at*stride+1]=y;data[at*stride+2]=color;};
    instance(data48,12,4,-.55f,-.05f,.25f);instance(data48,12,5,.05f,.02f,.5f);
    instance(data48,12,8,.35f,-.1f,.8f);instance(data48,12,10,-.15f,.5f,.3f);
    instance(data64,16,4,-.3f,-.4f,.1f);instance(data64,16,5,.3f,0,.65f);
    instance(data64,16,7,-.1f,-.35f,.3f);instance(data64,16,10,.55f,.35f,.6f);
    auto mixed48=Upload(device.Get(),data48.data(),sizeof(data48)),mixed64=Upload(device.Get(),data64.data(),sizeof(data64));
    D3D12_INDIRECT_ARGUMENT_DESC indexedOperation{};indexedOperation.Type=D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    const D3D12_COMMAND_SIGNATURE_DESC indexedSignatureDesc{32,1,&indexedOperation,0};ComPtr<ID3D12CommandSignature> indexedSignature;
    Check(device->CreateCommandSignature(&indexedSignatureDesc,nullptr,IID_PPV_ARGS(&indexedSignature)));
    indirect_policy::Created(indexedSignature.Get(),indexedSignatureDesc);
    D3D12_INDIRECT_ARGUMENT_DESC drawOperation{};drawOperation.Type=D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    const D3D12_COMMAND_SIGNATURE_DESC drawSignatureDesc{24,1,&drawOperation,0};ComPtr<ID3D12CommandSignature> drawSignature;
    Check(device->CreateCommandSignature(&drawSignatureDesc,nullptr,IID_PPV_ARGS(&drawSignature)));
    indirect_policy::Created(drawSignature.Get(),drawSignatureDesc);
    Require(indirect_policy::DrawKind(indexedSignature.Get())==1 && indirect_policy::PureDrawStride(indexedSignature.Get())==32 &&
        indirect_policy::DrawKind(drawSignature.Get())==2 && indirect_policy::PureDrawStride(drawSignature.Get())==24,"Genuine GPU indirect receipts lost padded strides");
    constexpr UINT IndexedOffset=8,DrawOffset=12,CountOffset=4;
    const std::array<D3D12_DRAW_INDEXED_ARGUMENTS,2> indexedArguments{{{3,1,1,1,7},{3,1,1,1,9}}};
    std::array<uint8_t,IndexedOffset+2*32> indexedBytes{};
    for(UINT i=0;i<indexedArguments.size();++i)std::memcpy(indexedBytes.data()+IndexedOffset+i*32,&indexedArguments[i],sizeof(indexedArguments[i]));
    const std::array<D3D12_DRAW_ARGUMENTS,2> drawArguments{{{3,1,1,5},{3,1,1,8}}};
    std::array<uint8_t,DrawOffset+2*24> drawBytes{};
    for(UINT i=0;i<drawArguments.size();++i)std::memcpy(drawBytes.data()+DrawOffset+i*24,&drawArguments[i],sizeof(drawArguments[i]));
    const std::array<UINT,3> countData{0xDEADBEEF,1,0xDEADBEEF};
    auto indexedBuffer=Upload(device.Get(),indexedBytes.data(),indexedBytes.size());
    auto drawBuffer=Upload(device.Get(),drawBytes.data(),drawBytes.size());
    auto countBuffer=Upload(device.Get(),countData.data(),sizeof(countData));
    std::array<GeometrySourceCommand,6> mixedSources;
    auto& direct48=mixedSources[0].geometry;auto& d48=direct48.draw;
    direct48.index=Lease(mixedIndex.Get());direct48.vertices[0]=Lease(mixedVertex.Get());
    direct48.vertices[4]=Lease(mixedDamage.Get());direct48.vertices[7]=Lease(mixed48.Get());
    d48.flags=128;d48.inputSlotMask=145;d48.indices=3;d48.instances=2;d48.firstIndex=1;d48.baseVertex=1;d48.firstInstance=3;
    d48.index={mixedIndex->GetGPUVirtualAddress()+8,8,DXGI_FORMAT_R16_UINT};
    d48.vertices[0]={mixedVertex->GetGPUVirtualAddress()+16,32,8};d48.vertices[4]={mixedDamage->GetGPUVirtualAddress()+16,32,8};
    d48.vertices[7]={mixed48->GetGPUVirtualAddress()+48,UINT(sizeof(data48)-48),48};
    mixedSources[1].geometry=direct48;
    mixedSources[1].indirect=PureIndirectSource{indexedSignature,2,indexedBuffer,IndexedOffset,countBuffer,CountOffset};
    mixedSources[2].geometry=direct48;auto& direct64=mixedSources[2].geometry;
    direct64.vertices[7]=Lease(mixed64.Get());direct64.draw.vertices[7]={mixed64->GetGPUVirtualAddress()+128,UINT(sizeof(data64)-128),64};
    direct64.draw.firstInstance=2;
    mixedSources[3].geometry=direct64;mixedSources[3].geometry.index={};mixedSources[3].geometry.draw.index={};
    mixedSources[3].indirect=PureIndirectSource{drawSignature,2,drawBuffer,DrawOffset,nullptr,0};
    mixedSources[4]=mixedSources[1];mixedSources[5]=mixedSources[0];
    auto mixedPrepared=std::make_unique<StereoGeometryCommands>();Check(mixedPrepared->CreateMixedSources(device.Get(),mixedSources));
    Require(mixedPrepared->SourceCount()==6 && !mixedPrepared->DrawCount() && !mixedPrepared->InstanceBytes(),"GPU mixed source occurrences were compacted or deduplicated");
    D3D12_RESOURCE_DESC image{};image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;image.Width=128;image.Height=64;image.DepthOrArraySize=image.MipLevels=image.SampleDesc.Count=1;
    image.Format=DXGI_FORMAT_R8G8B8A8_UNORM;image.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;std::array<ComPtr<ID3D12Resource>,9> targets;
    ComPtr<ID3D12DescriptorHeap> rtvs;D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,UINT(targets.size()),D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs)));
    auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart();const auto increment=device->GetDescriptorHandleIncrementSize(hd.Type);
    for(auto& target:targets){Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target)));device->CreateRenderTargetView(target.Get(),nullptr,rtv);rtv.ptr+=increment;}
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> commands;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),pipeline.Get(),IID_PPV_ARGS(&commands)));
    commands->SetGraphicsRootSignature(root.Get());commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);commands->RSSetViewports(1,&pair.eyes[0].draw.viewport);commands->RSSetScissorRects(1,&pair.eyes[0].draw.scissor);
    rtv=rtvs->GetCPUDescriptorHandleForHeapStart();const float clear[4]{};
    for(UINT mode=0;mode<3;++mode) {
        commands->OMSetRenderTargets(1,&rtv,FALSE,nullptr);commands->ClearRenderTargetView(rtv,clear,0,nullptr);
        if(mode<2){const auto& d=pair.eyes[mode].draw;commands->IASetIndexBuffer(&d.index);commands->IASetVertexBuffers(0,1,&d.vertices[0]);commands->IASetVertexBuffers(7,1,&d.vertices[7]);commands->DrawIndexedInstanced(3,2,0,0,d.firstInstance);}
        else Require(prepared->RecordGeometry(commands.Get(),1),"Prepared geometry command did not record");
        rtv.ptr+=increment;
    }
    commands->SetPipelineState(mixedPipeline.Get());
    auto nativeSource=[&](const GeometrySourceCommand& source){const auto& draw=source.geometry.draw;
        commands->IASetIndexBuffer(source.geometry.index.resource?&draw.index:nullptr);
        for(UINT slot=0;slot<16;++slot)if(draw.inputSlotMask&(1u<<slot))commands->IASetVertexBuffers(slot,1,&draw.vertices[slot]);
        if(source.indirect){const auto& indirect=*source.indirect;
            commands->ExecuteIndirect(indirect.signature.Get(),indirect.maxCommandCount,indirect.arguments.Get(),indirect.argumentOffset,indirect.countBuffer.Get(),indirect.countOffset);
        }else commands->DrawIndexedInstanced(draw.indices,draw.instances,draw.firstIndex,draw.baseVertex,draw.firstInstance);
    };
    for(UINT mode=0;mode<2;++mode){
        commands->OMSetRenderTargets(1,&rtv,FALSE,nullptr);commands->ClearRenderTargetView(rtv,clear,0,nullptr);
        for(UINT i=0;i<mixedSources.size();++i){
            if(mode)Require(mixedPrepared->RecordSourceGeometry(commands.Get(),i),"GPU mixed source command did not record");
            else nativeSource(mixedSources[i]);
        }
        rtv.ptr+=increment;
    }
    auto mutableBuffer=[&](UINT64 bytes){D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
        desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> resource;
        Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&resource)));return resource;
    };
    auto mutableArguments=mutableBuffer(indexedBytes.size()),mutableCount=mutableBuffer(sizeof(countData));
    auto changedBytes=indexedBytes;const std::array<D3D12_DRAW_INDEXED_ARGUMENTS,2> changedArguments{{{3,1,1,1,9},{3,1,1,1,7}}};
    for(UINT i=0;i<changedArguments.size();++i)std::memcpy(changedBytes.data()+IndexedOffset+i*32,&changedArguments[i],sizeof(changedArguments[i]));
    const std::array<UINT,3> changedCount{0xDEADBEEF,2,0xDEADBEEF};
    auto changedArgumentUpload=Upload(device.Get(),changedBytes.data(),changedBytes.size());
    auto changedCountUpload=Upload(device.Get(),changedCount.data(),sizeof(changedCount));
    Require(cvr::gpu::KeepCommandResources(commands.Get(),{mutableArguments.Get(),mutableCount.Get(),indexedBuffer.Get(),countBuffer.Get(),
        changedArgumentUpload.Get(),changedCountUpload.Get()}),"Mutable indirect fixture inputs were not retained before copying");
    commands->CopyBufferRegion(mutableArguments.Get(),0,indexedBuffer.Get(),0,indexedBytes.size());
    commands->CopyBufferRegion(mutableCount.Get(),0,countBuffer.Get(),0,sizeof(countData));
    std::array<D3D12_RESOURCE_BARRIER,2> mutableStates{};
    for(UINT i=0;i<2;++i){auto& barrier=mutableStates[i];barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={i?mutableCount.Get():mutableArguments.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT};}
    commands->ResourceBarrier(2,mutableStates.data());
    GeometrySourceCommand mutableSource;mutableSource.geometry=mixedSources[1].geometry;
    mutableSource.indirect=PureIndirectSource{indexedSignature,2,mutableArguments,IndexedOffset,mutableCount,CountOffset};
    commands->OMSetRenderTargets(1,&rtv,FALSE,nullptr);commands->ClearRenderTargetView(rtv,clear,0,nullptr);nativeSource(mutableSource);rtv.ptr+=increment;
    IndirectCopyProof argumentProof{mutableArguments,D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,1,1,mutableArguments};
    IndirectCopyProof countProof{mutableCount,D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,1,1,mutableCount};
    CapturedIndirectSource beforeOverwrite;
    Check(StereoGeometryCommands::CaptureIndirectSource(commands.Get(),*mutableSource.indirect,argumentProof,&countProof,ValidateFixtureIndirectCopy,beforeOverwrite));
    Require(beforeOverwrite.replay.argumentOffset==0 && beforeOverwrite.replay.countOffset==0 && beforeOverwrite.receipt.sourceOffsets==std::array<UINT64,2>{IndexedOffset,CountOffset} &&
        beforeOverwrite.receipt.copiedBytes==std::array<UINT64,2>{64,4} && beforeOverwrite.receipt.recordingGeneration==1 && beforeOverwrite.receipt.commandPosition==1,
        "GPU snapshot lost exact ranges, relocation or source provenance");
    // The source changes on the GPU after capture, before either owned replay.
    for(auto& barrier:mutableStates)std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
    commands->ResourceBarrier(2,mutableStates.data());
    commands->CopyBufferRegion(mutableArguments.Get(),0,changedArgumentUpload.Get(),0,changedBytes.size());
    commands->CopyBufferRegion(mutableCount.Get(),0,changedCountUpload.Get(),0,sizeof(changedCount));
    for(auto& barrier:mutableStates)std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
    commands->ResourceBarrier(2,mutableStates.data());
    argumentProof.commandPosition=countProof.commandPosition=2;CapturedIndirectSource afterOverwrite;
    Check(StereoGeometryCommands::CaptureIndirectSource(commands.Get(),*mutableSource.indirect,argumentProof,&countProof,ValidateFixtureIndirectCopy,afterOverwrite));
    Require(afterOverwrite.replay.arguments!=beforeOverwrite.replay.arguments && afterOverwrite.replay.countBuffer!=beforeOverwrite.replay.countBuffer &&
        afterOverwrite.owner!=beforeOverwrite.owner,"Indirect snapshots deduplicated different source positions");
    auto zeroSource=*mutableSource.indirect;zeroSource.maxCommandCount=0;zeroSource.argumentOffset=indexedBytes.size();zeroSource.countBuffer.Reset();zeroSource.countOffset=0;
    argumentProof.commandPosition=3;CapturedIndirectSource zeroSnapshot;
    Check(StereoGeometryCommands::CaptureIndirectSource(commands.Get(),zeroSource,argumentProof,nullptr,ValidateFixtureIndirectCopy,zeroSnapshot));
    Require(zeroSnapshot.receipt.copiedBytes==std::array<UINT64,2>{0,0} && zeroSnapshot.replay.arguments->GetDesc().Width>=32 && !zeroSnapshot.replay.countBuffer,
        "Zero-maximum snapshot read arguments or lost its valid private buffer");
    std::array<GeometrySourceCommand,2> frozenSources;
    for(auto& source:frozenSources)source.geometry=mutableSource.geometry;
    frozenSources[0].indirect=beforeOverwrite.replay;frozenSources[1].indirect=zeroSnapshot.replay;
    auto frozenPrepared=std::make_unique<StereoGeometryCommands>();Check(frozenPrepared->CreateMixedSources(device.Get(),frozenSources));
    GeometrySourceCommand freshSource;freshSource.geometry=mutableSource.geometry;freshSource.indirect=afterOverwrite.replay;
    auto freshPrepared=std::make_unique<StereoGeometryCommands>();Check(freshPrepared->CreateMixedSources(device.Get(),{&freshSource,1}));
    commands->OMSetRenderTargets(1,&rtv,FALSE,nullptr);commands->ClearRenderTargetView(rtv,clear,0,nullptr);
    Require(frozenPrepared->RecordSourceGeometry(commands.Get(),0) && frozenPrepared->RecordSourceGeometry(commands.Get(),1),"Frozen/zero indirect source replay failed");rtv.ptr+=increment;
    commands->OMSetRenderTargets(1,&rtv,FALSE,nullptr);commands->ClearRenderTargetView(rtv,clear,0,nullptr);nativeSource(mutableSource);rtv.ptr+=increment;
    commands->OMSetRenderTargets(1,&rtv,FALSE,nullptr);commands->ClearRenderTargetView(rtv,clear,0,nullptr);
    Require(freshPrepared->RecordSourceGeometry(commands.Get(),0),"Second indirect source version replay failed");
    frozenPrepared.reset();freshPrepared.reset();for(auto& source:frozenSources)source={};freshSource={};mutableSource={};
    beforeOverwrite={};afterOverwrite={};zeroSnapshot={};zeroSource={};argumentProof={};countProof={};
    mutableArguments.Reset();mutableCount.Reset();changedArgumentUpload.Reset();changedCountUpload.Reset();
    prepared.reset();mixedPrepared.reset();for(auto& source:mixedSources)source={};
    mixedVertex.Reset();mixedDamage.Reset();mixedIndex.Reset();mixed48.Reset();mixed64.Reset();
    indexedBuffer.Reset();drawBuffer.Reset();countBuffer.Reset();indexedSignature.Reset();drawSignature.Reset();
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,9> layouts;UINT64 total{};
    for(UINT i=0;i<targets.size();++i){UINT64 bytes{};device->GetCopyableFootprints(&image,0,1,total,&layouts[i],nullptr,nullptr,&bytes);total+=bytes;}
    D3D12_RESOURCE_DESC rb{};rb.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rb.Width=total;rb.Height=rb.DepthOrArraySize=rb.MipLevels=rb.SampleDesc.Count=1;rb.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> readback;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rb,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    for(UINT i=0;i<targets.size();++i){D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={targets[i].Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};commands->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=targets[i].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=layouts[i];commands->CopyTextureRegion(&to,0,0,0,&from,nullptr);}
    Check(commands->Close());ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));ID3D12CommandList* list=commands.Get();
    auto flight=cvr::gpu::PrepareCommandResources(1,&list);queue->ExecuteCommandLists(1,&list);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(flight));
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));Check(queue->Signal(fence.Get(),1));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event");
    Check(fence->SetEventOnCompletion(1,event));const auto wait=WaitForSingleObject(event,5000);CloseHandle(event);Require(wait==WAIT_OBJECT_0,"GPU comparison timeout");
    void* mapped{};Check(readback->Map(0,nullptr,&mapped));size_t nonblack{},mixedNonblack{},snapshotNonblack{},changedNonblack{},snapshotDifferent{};
    for(UINT y=0;y<64;++y){const auto* first=static_cast<uint8_t*>(mapped)+layouts[0].Offset+size_t(y)*layouts[0].Footprint.RowPitch;
        for(UINT i=1;i<3;++i)Require(!std::memcmp(first,static_cast<uint8_t*>(mapped)+layouts[i].Offset+size_t(y)*layouts[i].Footprint.RowPitch,128*4),"Rebased geometry/SV_InstanceID differs on GPU");
        const auto* mixed=static_cast<uint8_t*>(mapped)+layouts[3].Offset+size_t(y)*layouts[3].Footprint.RowPitch;
        Require(!std::memcmp(mixed,static_cast<uint8_t*>(mapped)+layouts[4].Offset+size_t(y)*layouts[4].Footprint.RowPitch,128*4),"Native direct/ExecuteIndirect differs from exact owned mixed replay on GPU");
        const auto* snapshot=static_cast<uint8_t*>(mapped)+layouts[5].Offset+size_t(y)*layouts[5].Footprint.RowPitch;
        const auto* changed=static_cast<uint8_t*>(mapped)+layouts[7].Offset+size_t(y)*layouts[7].Footprint.RowPitch;
        Require(!std::memcmp(snapshot,static_cast<uint8_t*>(mapped)+layouts[6].Offset+size_t(y)*layouts[6].Footprint.RowPitch,128*4),"Frozen GPU arguments/count changed after source overwrite");
        Require(!std::memcmp(changed,static_cast<uint8_t*>(mapped)+layouts[8].Offset+size_t(y)*layouts[8].Footprint.RowPitch,128*4),"Second GPU snapshot reused the earlier source version");
        for(UINT x=0;x<128*4;++x){nonblack+=first[x]!=0;mixedNonblack+=mixed[x]!=0;snapshotNonblack+=snapshot[x]!=0;changedNonblack+=changed[x]!=0;snapshotDifferent+=snapshot[x]!=changed[x];}}
    readback->Unmap(0,nullptr);Require(nonblack>1000 && mixedNonblack>1000 && snapshotNonblack>1000 && changedNonblack>1000 && snapshotDifferent>100,
        "Blank GPU comparison or source overwrite was not distinguishable");cvr::gpu::ResetCommandResources(commands.Get());cvr::gpu::CollectCommandResources();
    std::puts("PASS paired/mixed GPU byte equality, independent GPU-only frozen argument/count versions across overwrite, zero maximum, original IA/instance ABI and fenced lifetime");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
