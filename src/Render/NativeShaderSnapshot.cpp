#include "Render/NativeShaderSnapshot.hpp"
#include "Render/NativeStereoProbe.hpp"
#include "Render/RenderProbeScope.hpp"
#include "Render/ViewInstancedPipeline.hpp"
#include <windows.h>
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <cstring>
#include <utility>

namespace cvr::stereo::shader_snapshot {
using Microsoft::WRL::ComPtr;
namespace {
constexpr GUID rootKey{0x7cb1bec1,0x9757,0x434b,{0xb2,0xcb,0xb1,0x25,0xad,0x4b,0x41,0x36}};
constexpr GUID sourceKey{0x7cb1bec1,0x9757,0x434b,{0xb2,0xcb,0xb1,0x25,0xad,0x4b,0x41,0x37}};
constexpr uint64_t MaxRetainedBytes=512ull*1024*1024;
std::atomic<uint64_t> nextId{1},retainedBytes{};
std::atomic<UINT> roots{},sources{};

template<class T,const GUID* Key> struct Owner final:IUnknown {
    std::atomic<ULONG> refs{1};
    std::shared_ptr<const T> value;
    explicit Owner(std::shared_ptr<const T> data):value(std::move(data)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=__uuidof(IUnknown) && id!=*Key)return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto remaining=--refs;if(!remaining)delete this;return remaining;}
};
using RootOwner=Owner<std::vector<uint8_t>,&rootKey>;
using SourceOwner=Owner<Snapshot,&sourceKey>;

template<class T> ComPtr<T> Get(ID3D12Object* object,REFGUID key){
    ComPtr<IUnknown> unknown;ComPtr<T> result;UINT bytes=sizeof(IUnknown*);
    if(object && SUCCEEDED(object->GetPrivateData(key,&bytes,unknown.GetAddressOf())) && bytes==sizeof(IUnknown*) && unknown)
        unknown->QueryInterface(key,reinterpret_cast<void**>(result.GetAddressOf()));
    return result;
}
bool Observe(){return !probe::internalCommands && native_probe::ObserveBindings();}
bool Read(const void* source,void* destination,size_t bytes){
    const auto address=reinterpret_cast<uintptr_t>(source);SIZE_T copied{};
    return source && bytes && address<=UINTPTR_MAX-bytes &&
        ReadProcessMemory(GetCurrentProcess(),source,destination,bytes,&copied) && copied==bytes;
}
bool Copy(const void* source,size_t bytes,std::vector<uint8_t>& result){
    result.resize(bytes);return Read(source,result.data(),bytes);
}
bool Semantic(const char* source,std::string& result){
    if(!source)return false;
    std::array<char,256> bytes{};
    if(Read(source,bytes.data(),bytes.size())){
        const auto* end=static_cast<const char*>(std::memchr(bytes.data(),0,bytes.size()));
        if(!end || end==bytes.data())return false;result.assign(bytes.data(),size_t(end-bytes.data()));return true;
    }
    for(size_t i=0;i<256;++i){char c{};const auto address=reinterpret_cast<uintptr_t>(source);
        if(address>UINTPTR_MAX-i || !Read(reinterpret_cast<const void*>(address+i),&c,1))return false;
        if(!c)return !result.empty();result.push_back(c);
    }
    return false;
}
bool Reserve(std::atomic<UINT>& count,uint64_t bytes){
    UINT current=count.load(std::memory_order_relaxed);
    do{if(current>=MaxSources)return false;}while(!count.compare_exchange_weak(current,current+1,std::memory_order_relaxed));
    auto used=retainedBytes.load(std::memory_order_relaxed);
    do{if(bytes>MaxRetainedBytes || used>MaxRetainedBytes-bytes){count.fetch_sub(1,std::memory_order_relaxed);return false;}}
    while(!retainedBytes.compare_exchange_weak(used,used+bytes,std::memory_order_relaxed));
    return true;
}
template<class T> std::shared_ptr<T> Allocate(std::atomic<UINT>& count,uint64_t bytes){
    if(!Reserve(count,bytes))return {};
    auto* counter=&count;
    auto release=[counter,bytes](T* value){delete value;counter->fetch_sub(1,std::memory_order_relaxed);retainedBytes.fetch_sub(bytes,std::memory_order_relaxed);};
    T* raw{};
    try{raw=new T;}catch(...){release(nullptr);throw;}
    return std::shared_ptr<T>(raw,release);
}
uint64_t Id(){
    auto id=nextId.load(std::memory_order_relaxed);
    do{if(id==UINT64_MAX)return 0;}while(!nextId.compare_exchange_weak(id,id+1,std::memory_order_relaxed));
    return id;
}
}

void RootCreated(ID3D12RootSignature* root,const void* bytes,size_t size){
    if(!Observe() || !root)return;
    try{
        if(Get<RootOwner>(root,rootKey))return;
        if(!bytes || !size || size>MaxRootBytes)return;
        auto data=Allocate<std::vector<uint8_t>>(roots,size);if(!data || !Copy(bytes,size,*data))return;
        ComPtr<RootOwner> owner;owner.Attach(new RootOwner(std::move(data)));
        root->SetPrivateDataInterface(rootKey,owner.Get());
    }catch(...){return;}
}
namespace {
void CaptureGraphics(ID3D12PipelineState* pipeline,ID3D12RootSignature* root,D3D12_SHADER_BYTECODE vs,
    D3D12_SHADER_BYTECODE ps,const D3D12_INPUT_LAYOUT_DESC& input,CreationContract creation){
    if(!Observe() || !pipeline)return;
    try{
        if(Get<SourceOwner>(pipeline,sourceKey))return;
        if(!root || !vs.pShaderBytecode || !vs.BytecodeLength || vs.BytecodeLength>MaxShaderBytes ||
            ps.BytecodeLength>MaxShaderBytes || (ps.BytecodeLength && !ps.pShaderBytecode) ||
            input.NumElements>MaxInputs || (input.NumElements && !input.pInputElementDescs))return;
        const auto serialized=Get<RootOwner>(root,rootKey);if(!serialized || !serialized->value || serialized->value->empty())return;
        std::array<D3D12_INPUT_ELEMENT_DESC,MaxInputs> elements{};
        if(input.NumElements && !Read(input.pInputElementDescs,elements.data(),input.NumElements*sizeof(elements[0])))return;
        const uint64_t bytes=vs.BytecodeLength+ps.BytecodeLength+serialized->value->size()+uint64_t(input.NumElements)*(sizeof(Input)+256);
        auto snapshot=Allocate<Snapshot>(sources,bytes);if(!snapshot)return;
        if(!Copy(vs.pShaderBytecode,vs.BytecodeLength,snapshot->vs) ||
            (ps.BytecodeLength && !Copy(ps.pShaderBytecode,ps.BytecodeLength,snapshot->ps)))return;
        snapshot->root=*serialized->value;snapshot->input.reserve(input.NumElements);snapshot->creation=creation;
        for(UINT i=0;i<input.NumElements;++i){const auto& source=elements[i];Input value;
            if(!Semantic(source.SemanticName,value.semantic))return;
            value.index=source.SemanticIndex;value.format=UINT(source.Format);value.slot=source.InputSlot;
            value.offset=source.AlignedByteOffset;value.classification=UINT(source.InputSlotClass);value.step=source.InstanceDataStepRate;
            snapshot->input.push_back(std::move(value));
        }
        snapshot->id=Id();if(!snapshot->id)return;
        ComPtr<SourceOwner> owner;owner.Attach(new SourceOwner(std::move(snapshot)));
        pipeline->SetPrivateDataInterface(sourceKey,owner.Get());
    }catch(...){return;}
}
}
CreationContract DescribeCreation(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& source)noexcept{
    if(!source.pRootSignature || !source.VS.pShaderBytecode || !source.VS.BytecodeLength ||
        (source.PS.BytecodeLength && !source.PS.pShaderBytecode))return {};
    return {source.HS.BytecodeLength || source.DS.BytecodeLength || source.GS.BytecodeLength?
        StageCompleteness::OtherStages:StageCompleteness::VsPsOnly,
        source.StreamOutput.NumEntries || source.StreamOutput.NumStrides?StreamOutputPresence::Present:StreamOutputPresence::Absent};
}
CreationContract DescribeStreamCreation(const PipelineStreamInfo& source)noexcept{
    if(!source.root || !source.vs.pShaderBytecode || !source.vs.BytecodeLength ||
        (source.ps.BytecodeLength && !source.ps.pShaderBytecode))return {};
    return {source.otherStages?StageCompleteness::OtherStages:StageCompleteness::VsPsOnly,
        source.streamOutput?StreamOutputPresence::Present:StreamOutputPresence::Absent};
}
void GraphicsCreated(ID3D12PipelineState* pipeline,ID3D12RootSignature* root,D3D12_SHADER_BYTECODE vs,
    D3D12_SHADER_BYTECODE ps,const D3D12_INPUT_LAYOUT_DESC& input){
    CaptureGraphics(pipeline,root,vs,ps,input,{});
}
void GraphicsCreated(ID3D12PipelineState* pipeline,const D3D12_GRAPHICS_PIPELINE_STATE_DESC& source){
    CaptureGraphics(pipeline,source.pRootSignature,source.VS,source.PS,source.InputLayout,DescribeCreation(source));
}
void GraphicsStreamCreated(ID3D12PipelineState* pipeline,const PipelineStreamInfo& source){
    CaptureGraphics(pipeline,source.root,source.vs,source.ps,source.input,DescribeStreamCreation(source));
}
std::shared_ptr<const Snapshot> Capture(ID3D12PipelineState* pipeline){
    if(!Observe() || !pipeline)return {};
    try{const auto owner=Get<SourceOwner>(pipeline,sourceKey);return owner?owner->value:nullptr;}
    catch(...){return {};}
}
}
