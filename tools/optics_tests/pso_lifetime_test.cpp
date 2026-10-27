// Real D3D12/WARP objects; never launches or attaches to the game.
#include "Render/PipelineVariants.hpp"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <unordered_set>
#include <vector>

using Microsoft::WRL::ComPtr;
using Index=cvr::render::PipelineVariantIndex<1>;
constexpr GUID sightKey{0x93e581cd,0x1498,0x454d,{0x8b,0x9c,0x73,0x6d,0xf4,0x09,0x31,0x7b}};
constexpr GUID glassKey{0x445b6076,0x2f13,0x40e5,{0xa7,0x3b,0x11,0xd6,0x89,0x8f,0x42,0x6a}};
Index sightIndex{sightKey},glassIndex{glassKey};
constexpr GUID witnessKey{0x975157b1,0xa82b,0x43c7,{0xab,0x9d,0x33,0xa4,0xfe,0x81,0x15,0x6b}};
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Hr(HRESULT hr){if(FAILED(hr)){printf("HRESULT %08X\n",unsigned(hr));throw std::runtime_error("D3D12 operation failed");}}

struct Witness final : IUnknown {
    std::atomic<ULONG> refs{1};bool& destroyed;
    explicit Witness(bool& value):destroyed(value){}
    ~Witness(){destroyed=true;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=__uuidof(IUnknown))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{const auto left=--refs;if(!left)delete this;return left;}
};
void Watch(ID3D12PipelineState* pso,bool& destroyed){
    auto* witness=new Witness(destroyed);
    const auto hr=pso->SetPrivateDataInterface(witnessKey,witness);witness->Release();Hr(hr);
}

struct Fixture {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3DBlob> shaders[4];
    Fixture(){
        ComPtr<IDXGIFactory4> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> adapter;Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        for(unsigned value=1;value<=4;++value){
            char source[256];sprintf_s(source,"RWStructuredBuffer<uint> output:register(u0); [numthreads(1,1,1)] void main(){output[0]=%uu;}",value);
            ComPtr<ID3DBlob> errors;
            Hr(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shaders[value-1],&errors));
        }
        D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=1;desc.pParameters=&parameter;
        ComPtr<ID3DBlob> blob,errors;Hr(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors));
        Hr(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    }
    ComPtr<ID3D12PipelineState> Make(unsigned value){
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=root.Get();
        desc.CS={shaders[value-1]->GetBufferPointer(),shaders[value-1]->GetBufferSize()};
        ComPtr<ID3D12PipelineState> result;Hr(device->CreateComputePipelineState(&desc,IID_PPV_ARGS(&result)));return result;
    }
    unsigned Run(ID3D12PipelineState* pso){
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width=4;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;
        desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ComPtr<ID3D12Resource> output,readback;
        Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)));
        heap.Type=D3D12_HEAP_TYPE_READBACK;desc.Flags=D3D12_RESOURCE_FLAG_NONE;
        Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
        ComPtr<ID3D12CommandAllocator> allocator;Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> list;
        Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),pso,IID_PPV_ARGS(&list)));
        list->SetComputeRootSignature(root.Get());list->SetComputeRootUnorderedAccessView(0,output->GetGPUVirtualAddress());list->Dispatch(1,1,1);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={output.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1,&barrier);list->CopyBufferRegion(readback.Get(),0,output.Get(),0,4);Hr(list->Close());
        D3D12_COMMAND_QUEUE_DESC queueDesc{};ComPtr<ID3D12CommandQueue> queue;
        Hr(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)));
        ComPtr<ID3D12Fence> fence;Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);Hr(queue->Signal(fence.Get(),1));
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(event!=nullptr,"CreateEvent");
        const auto hr=fence->SetEventOnCompletion(1,event);
        const auto wait=SUCCEEDED(hr)?WaitForSingleObject(event,10000):WAIT_FAILED;CloseHandle(event);
        Hr(hr);Check(wait==WAIT_OBJECT_0,"GPU timeout");
        void* data{};D3D12_RANGE range{0,4},empty{};Hr(readback->Map(0,&range,&data));
        const unsigned result=*static_cast<unsigned*>(data);readback->Unmap(0,&empty);return result;
    }
};

void Ownership(Fixture& f){
    auto original=f.Make(1);
    bool variantGone=false,maskGone=false,duplicateGone=false,nullGone=false;
    auto variant=f.Make(2),mask=f.Make(3);Watch(variant.Get(),variantGone);Watch(mask.Get(),maskGone);
    auto* first=variant.Get();ID3D12PipelineState* created[]{variant.Detach()};
    Hr(sightIndex.Store(original.Get(),created,mask.Detach()));
    const auto* owner=sightIndex.Find(original.Get());
    Check(owner && owner->variants[0].Get()==first,"Stored variant missing");
    Check(!glassIndex.Find(original.Get()),"Sight data leaked into glass classification");
    auto duplicate=f.Make(4);Watch(duplicate.Get(),duplicateGone);
    ID3D12PipelineState* extra[]{duplicate.Detach()};
    Check(sightIndex.Store(original.Get(),extra,nullptr)==S_FALSE,"Live PSO owner replaced");
    Check(duplicateGone && sightIndex.Find(original.Get())==owner,"Duplicate ownership failure");
    auto orphan=f.Make(4);Watch(orphan.Get(),nullGone);ID3D12PipelineState* noOriginal[]{orphan.Detach()};
    Check(sightIndex.Store(nullptr,noOriginal,nullptr)==E_POINTER && nullGone,"Failed attachment leaked variant");
    // A draw hook may still hold the destroyed original's address: the index answers without touching it.
    const ID3D12PipelineState* dead=original.Get();
    original.Reset();Check(variantGone && maskGone,"Variants outlived their original");
    Check(!sightIndex.Find(dead) && sightIndex.Live()==0,"Destroyed original still in the index");
    auto empty=f.Make(1);ID3D12PipelineState* none[]{nullptr};Hr(glassIndex.Store(empty.Get(),none,nullptr));
    Check(glassIndex.Find(empty.Get())!=nullptr,"Glass without auxiliary shaders lost classification");
    Check(!glassIndex.Find(nullptr),"Null lookup");
    empty.Reset();Check(glassIndex.Live()==0,"Empty glass entry outlived its original");
    puts("PASS ownership, duplicate registration, failed attachment, independent keys, empty glass, dead lookup");
}

bool ReusedAddress(Fixture& f,Index& index,const char* name){
    std::unordered_set<ID3D12PipelineState*> old;
    std::vector<ComPtr<ID3D12PipelineState>> originals;
    for(unsigned i=0;i<256;++i){
        auto p=f.Make(1);old.insert(p.Get());ID3D12PipelineState* created[]{f.Make(2).Detach()};
        Hr(index.Store(p.Get(),created,f.Make(2).Detach()));originals.push_back(std::move(p));
    }
    originals.clear();
    Check(index.Live()==0,"Destroyed originals still in the index");
    ComPtr<ID3D12PipelineState> reused;std::vector<ComPtr<ID3D12PipelineState>> hold;
    for(unsigned i=0;i<4096;++i){
        auto p=f.Make(3);
        if(old.contains(p.Get())){reused=std::move(p);break;}
        hold.push_back(std::move(p));
    }
    if(!reused){printf("INCONCLUSIVE %s: allocator did not reuse an address\n",name);return false;}
    Check(!index.Find(reused.Get()),"New PSO inherited stale variants");
    Check(f.Run(reused.Get())==3,"New native PSO executed wrong shader");
    ID3D12PipelineState* fresh[]{f.Make(4).Detach()};
    Hr(index.Store(reused.Get(),fresh,f.Make(4).Detach()));
    const auto* owner=index.Find(reused.Get());
    Check(owner && f.Run(owner->variants[0].Get())==4 && f.Run(owner->mask.Get())==4,"New registration kept old variants");
    printf("PASS %s: recycled address stays native, new variant/mask execute shader 4\n",name);return true;
}
int main(){
    try{Fixture f;Ownership(f);const bool sight=ReusedAddress(f,sightIndex,"sight"),glass=ReusedAddress(f,glassIndex,"glass");return sight && glass?0:77;}
    catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}
}
