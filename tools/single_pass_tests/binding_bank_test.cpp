#include "Render/StereoBindingBank.hpp"
#include "Render/CommandResources.hpp"
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
using namespace cvr::stereo;
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void Check(HRESULT hr){if(FAILED(hr)){std::fprintf(stderr,"HRESULT %08X\n",UINT(hr));throw std::runtime_error("D3D12 failure");}}
static void Failure(const StereoBindingBank& bank,SourcePassFailureStage stage,uint64_t bits,UINT input=UINT_MAX,UINT parameter=UINT_MAX){
    const auto diagnostic=bank.LastSourcePassFailure(),again=bank.LastSourcePassFailure();
    Require(diagnostic.stage==UINT(stage) && diagnostic.bits==bits && diagnostic.input==input && diagnostic.parameter==parameter &&
        again.stage==diagnostic.stage && again.bits==diagnostic.bits && again.input==diagnostic.input && again.parameter==diagnostic.parameter,
        "Source failure getter lost the exact reject or mutated its diagnostic");
}
static void Cleared(const StereoBindingBank& bank){Failure(bank,SourcePassFailureStage::None,0);}
class Sentinel final:public IUnknown {
    std::atomic<ULONG> refs{1};std::atomic<UINT>& destroyed;
public:
    explicit Sentinel(std::atomic<UINT>& value):destroyed(value){}
    ~Sentinel(){++destroyed;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*p=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
};
static void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};list->ResourceBarrier(1,&barrier);
}
static void StorageOverflow(ID3D12Device* device){
    std::atomic<UINT> destroyed{};ComPtr<IUnknown> sentinel;sentinel.Attach(new Sentinel(destroyed));
    const D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,UINT_MAX,0,200,0};D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameter.DescriptorTable={1,&range};
    const D3D12_ROOT_SIGNATURE_DESC layout{1,&parameter,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> blob,error;
    Check(D3D12SerializeRootSignature(&layout,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    const auto bytes=std::span<const uint8_t>{static_cast<const uint8_t*>(blob->GetBufferPointer()),blob->GetBufferSize()};
    const D3D12_DESCRIPTOR_HEAP_DESC desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,256,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};ComPtr<ID3D12DescriptorHeap> cpu;
    Check(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&cpu)));const UINT stride=device->GetDescriptorHandleIncrementSize(desc.Type);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Buffer.NumElements=1;
    for(UINT n=0;n<256;++n)device->CreateShaderResourceView(nullptr,&srv,{cpu->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(n)*stride});
    std::vector<SharedBankTable> tables;UINT64 requested{};
    for(UINT extent=256;extent>=128 && requested<=1000000;--extent)for(UINT offset=0;offset+extent<=256 && requested<=1000000;++offset){
        SharedBankTable table;table.count=1;table.offset=offset;table.cpuHeap=cpu;table.leases={sentinel};
        table.storageScope=tables.size()+1;table.storageExtent=extent;tables.push_back(std::move(table));requested+=extent;}
    Require(requested>1000000 && tables.size()<32768,"Storage overflow fixture did not exceed the unchanged cap");
    std::vector<SourceBankDraw> draws;draws.reserve(tables.size());for(const auto& table:tables)draws.push_back({root,bytes,{&table,1}});
    StereoBindingBank rejected;Require(FAILED(rejected.CreateSourcePass(device,draws)) && !rejected.DrawCount() && !rejected.DescriptorCount(),
        "Unique storage extents bypassed the 1M cap or published partial state");
    Failure(rejected,SourcePassFailureStage::Capacity,uint64_t(SourcePassFailureReason::DescriptorCapacity)|uint64_t(SourcePassFailureReason::ResourceCapacity),UINT(tables.size()-1),0);
    const SourceBankDraw retry{root,bytes,{tables.data(),1}};Check(rejected.CreateSourcePass(device,{&retry,1}));
    Cleared(rejected);
    Require(rejected.DescriptorCount()==tables[0].storageExtent && rejected.DrawCount()==1 && !destroyed,
        "Failed storage allocation poisoned bank retry or dropped current owners");
}
static void SourcePass(ID3D12Device* device,ID3D12CommandQueue* queue,bool unbounded=false,bool sharedStorage=false){
    const UINT tableCount=unbounded?5u:3u;
    std::array<D3D12_DESCRIPTOR_RANGE,36> ranges{};std::array<D3D12_ROOT_PARAMETER,36> parameters{};
    for(UINT i=0;i<parameters.size();++i){ranges[i]={D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,100+i,99,0};
        parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[i].DescriptorTable={1,&ranges[i]};}
    ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,1,0,0,0};parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_DESCRIPTOR_RANGE mixed[]{
        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,0,0,0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND}};
    if(unbounded){
        ranges[0].NumDescriptors=UINT_MAX;
        for(UINT i=13;i<=17;++i)ranges[i]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,UINT_MAX,0,100+i,0};
        for(UINT i=31;i<=35;++i)ranges[i]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,UINT_MAX,0,100+i,0};
        ranges[32]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,UINT_MAX,0,88,0};
        mixed[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
        mixed[2]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,UINT_MAX,0,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
    }
    parameters[33].DescriptorTable={3,mixed};parameters[33].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    const D3D12_ROOT_SIGNATURE_DESC layout{UINT(parameters.size()),parameters.data(),0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> blob,error;Check(D3D12SerializeRootSignature(&layout,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));
    ComPtr<ID3D12RootSignature> root;Check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    const auto bytes=std::span<const uint8_t>{static_cast<const uint8_t*>(blob->GetBufferPointer()),blob->GetBufferSize()};
    auto buffer=[&](UINT64 size,D3D12_HEAP_TYPE type,D3D12_RESOURCE_FLAGS flags,D3D12_RESOURCE_STATES state){
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;
        d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=flags;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> result;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&result)));return result;
    };
    auto constants=buffer(256,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto upload=buffer(4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto unordered=buffer(4,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
    auto write=[&](ID3D12Resource* resource,float value){void* memory{};D3D12_RANGE none{};Check(resource->Map(0,&none,&memory));
        std::memcpy(memory,&value,4);const D3D12_RANGE written{0,4};resource->Unmap(0,&written);};
    write(constants.Get(),6);write(upload.Get(),2);
    D3D12_HEAP_PROPERTIES memory{};memory.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC image{};image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;image.Width=image.Height=1;
    image.DepthOrArraySize=image.MipLevels=image.SampleDesc.Count=1;image.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;image.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> texture,target;Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&texture)));
    image.Width=2;Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target)));
    ComPtr<ID3D12DescriptorHeap> cpu,samplers,visible,rtvs;
    D3D12_DESCRIPTOR_HEAP_DESC h{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,tableCount*2,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
    Check(device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&cpu)));h.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;Check(device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&visible)));
    h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;h.NumDescriptors=2;h.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;Check(device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&samplers)));
    h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;Check(device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&rtvs)));
    const auto cpuStart=cpu->GetCPUDescriptorHandleForHeapStart();const UINT stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto at=[&](UINT index){return D3D12_CPU_DESCRIPTOR_HANDLE{cpuStart.ptr+SIZE_T(index)*stride};};
    const D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{constants->GetGPUVirtualAddress(),256};device->CreateConstantBufferView(&cbv,at(0));
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=image.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if(unbounded)for(UINT n=2;n<tableCount;++n)device->CreateShaderResourceView(texture.Get(),&srv,at(n));
    else device->CreateShaderResourceView(texture.Get(),&srv,at(1));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Buffer.NumElements=1;uav.Buffer.StructureByteStride=4;
    device->CreateUnorderedAccessView(unordered.Get(),nullptr,&uav,at(unbounded?1:2));
    device->CopyDescriptorsSimple(tableCount,at(tableCount),at(0),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;sampler.MaxLOD=D3D12_FLOAT32_MAX;device->CreateSampler(&sampler,samplers->GetCPUDescriptorHandleForHeapStart());
    auto textureRtv=rtvs->GetCPUDescriptorHandleForHeapStart(),targetRtv=textureRtv;targetRtv.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    device->CreateRenderTargetView(texture.Get(),nullptr,textureRtv);device->CreateRenderTargetView(target.Get(),nullptr,targetRtv);
    std::atomic<UINT> destroyed{},secondDestroyed{};ComPtr<IUnknown> sentinel,secondSentinel;sentinel.Attach(new Sentinel(destroyed));
    std::array<SharedBankTable,2> tables{};tables[0].parameter=33;tables[0].count=tableCount;tables[0].cpuHeap=cpu;tables[0].leases={constants,texture,unordered,sentinel};
    tables[1].parameter=0;tables[1].count=1;tables[1].cpuHeap=samplers;
    const SourceBankDraw input{root,bytes,tables};auto bank=std::make_unique<StereoBindingBank>();
    Cleared(*bank);
    Require(FAILED(bank->CreateSourcePass(nullptr,{&input,1})) && FAILED(bank->CreateSourcePass(device,{})),"Missing source device/draws accepted");
    Failure(*bank,SourcePassFailureStage::Arguments,uint64_t(SourcePassFailureReason::EmptyInputs));
    Require(FAILED(bank->CreateSourcePass(nullptr,{&input,1})),"Missing source device accepted on retry");
    Failure(*bank,SourcePassFailureStage::Arguments,uint64_t(SourcePassFailureReason::MissingDevice));
    {auto missing=input;missing.root.Reset();Require(FAILED(bank->CreateSourcePass(device,{&missing,1})),"Missing original root accepted");
        Failure(*bank,SourcePassFailureStage::Input,uint64_t(SourcePassFailureReason::RootDevice),0);
        missing=input;missing.serializedRoot={};Require(FAILED(bank->CreateSourcePass(device,{&missing,1})),"Missing original root bytes accepted");
        Failure(*bank,SourcePassFailureStage::RootBytes,uint64_t(SourcePassFailureReason::BlobExtent),0);}
    for(UINT failure=0;failure<12;++failure){auto changed=tables;
        switch(failure){case 0:changed[0].parameter=UINT(parameters.size());break;case 1:changed[1].parameter=33;break;
        case 2:changed[0].count=2;break;case 3:changed[0].offset=tableCount*2;break;
        case 4:changed[0].cpuHeap=visible;break;case 5:changed[0].leases.clear();break;
        case 6:changed[0].identity=10;break;case 7:changed[1].cpuHeap=cpu;break;
        case 8:changed[0].count=0;break;case 9:changed[0].count=UINT_MAX;break;
        case 10:changed[0].offset=UINT_MAX;break;case 11:changed[1].count=2049;break;}
        const SourceBankDraw invalid{root,bytes,changed};Require(FAILED(bank->CreateSourcePass(device,{&invalid,1})),"Invalid original table capture accepted");
        Require(!bank->DrawCount() && !bank->DescriptorCount(),"Failed source creation published partial ownership");
        const auto diagnostic=bank->LastSourcePassFailure();Require(diagnostic.stage && diagnostic.bits && diagnostic.input==0 &&
            diagnostic.parameter==(failure==7 || failure==11?changed[1].parameter:changed[0].parameter),"Table failure lacked its exact source/parameter");
    }
    {
        D3D12_ROOT_PARAMETER p{};p.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p.Constants={0,0,1};
        D3D12_ROOT_SIGNATURE_DESC d{1,&p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> rejected;
        Check(D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,&rejected,&error));
        SourceBankDraw invalid=input;invalid.serializedRoot={static_cast<const uint8_t*>(rejected->GetBufferPointer()),rejected->GetBufferSize()};
        Require(FAILED(bank->CreateSourcePass(device,{&invalid,1})),"Unsupported original root constants accepted");
        Failure(*bank,SourcePassFailureStage::RootLayout,uint64_t(SourcePassFailureReason::RootParameterKind),0,0);
        d={0,nullptr,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE};Check(D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,&rejected,&error));
        invalid.serializedRoot={static_cast<const uint8_t*>(rejected->GetBufferPointer()),rejected->GetBufferSize()};
        Require(FAILED(bank->CreateSourcePass(device,{&invalid,1})),"Local original root accepted");
        Failure(*bank,SourcePassFailureStage::RootLayout,uint64_t(SourcePassFailureReason::RootFlags),0);
    }
    if(unbounded){
        SharedBankTable single=tables[0];single.parameter=32;single.offset=2;single.count=3;
        const SourceBankDraw singleRange{root,bytes,{&single,1}};StereoBindingBank singleBank;
        Check(singleBank.CreateSourcePass(device,{&singleRange,1}));
        Require(singleBank.DescriptorCount()==3,"Single unbounded SRV range did not honor its admitted finite extent");
        auto shorter=tables;shorter[0].count=4;
        const SourceBankDraw variable[]{{root,bytes,shorter},input};StereoBindingBank variableBank;
        Check(variableBank.CreateSourcePass(device,variable));
        Require(variableBank.DescriptorCount()==9,"Independent source occurrences did not retain their own finite tail extents");
        {
            auto storage=tables;storage[0].storageScope=700;storage[0].storageExtent=tableCount;
            auto shorterStorage=storage;shorterStorage[0].count=tableCount-1;
            const SourceBankDraw sharedDraws[]{{root,bytes,shorterStorage},{root,bytes,storage}};StereoBindingBank shared;
            Check(shared.CreateSourcePass(device,sharedDraws));
            Require(shared.DescriptorCount()==tableCount && shared.DrawCount()==2 && shorterStorage[0].count==tableCount-1 && storage[0].count==tableCount,
                "Explicit union storage summed or rewrote per-source admitted counts");
            auto anotherLabel=storage;anotherLabel[0].storageScope=701;
            const SourceBankDraw aliases[]{{root,bytes,shorterStorage},{root,bytes,anotherLabel}};StereoBindingBank actualRange;
            Check(actualRange.CreateSourcePass(device,aliases));Require(actualRange.DescriptorCount()==tableCount,"Storage sharing trusted labels instead of the actual held range");
            auto otherLayout=layout;otherLayout.Flags=D3D12_ROOT_SIGNATURE_FLAGS(otherLayout.Flags^D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);
            ComPtr<ID3DBlob> otherBlob;Check(D3D12SerializeRootSignature(&otherLayout,D3D_ROOT_SIGNATURE_VERSION_1,&otherBlob,&error));
            ComPtr<ID3D12RootSignature> otherRoot;Check(device->CreateRootSignature(0,otherBlob->GetBufferPointer(),otherBlob->GetBufferSize(),IID_PPV_ARGS(&otherRoot)));
            Require(otherRoot!=root,"Root conflict fixture reused an interned identical root");
            const std::span<const uint8_t> otherBytes{static_cast<const uint8_t*>(otherBlob->GetBufferPointer()),otherBlob->GetBufferSize()};
            ComPtr<ID3D12DescriptorHeap> otherHeap;const auto heapDesc=cpu->GetDesc();Check(device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&otherHeap)));
            device->CopyDescriptorsSimple(tableCount,otherHeap->GetCPUDescriptorHandleForHeapStart(),at(0),heapDesc.Type);
            for(UINT conflict=0;conflict<14;++conflict){auto changed=storage;auto changedRoot=root;
                switch(conflict){case 0:changed[0].storageScope=0;break;case 1:changed[0].storageExtent=0;break;
                case 2:changed[0].storageExtent=tableCount-1;break;case 3:changed[0].storageExtent=tableCount+1;break;
                case 4:changed[0].offset=tableCount;break;case 5:changed[0].cpuHeap=otherHeap;break;
                case 6:changed[0].parameter=32;break;case 7:changedRoot=otherRoot;break;
                case 8:changed[0].identity=20;changed[0].version=1;break;case 9:changed[0].storageExtent=UINT_MAX;break;
                case 10:changed[0].storageExtent=heapDesc.NumDescriptors+1;break;case 11:changed[0].count=tableCount+1;break;
                case 12:changed[0].leases.clear();break;case 13:changed[0].storageExtent=1000001;break;}
                const SourceBankDraw bad[]{{root,bytes,shorterStorage},{changedRoot,conflict==7?otherBytes:bytes,changed}};StereoBindingBank rejected;
                const auto rejectedResult=rejected.CreateSourcePass(device,bad);
                if(SUCCEEDED(rejectedResult) || rejected.DrawCount() || rejected.DescriptorCount())
                    std::fprintf(stderr,"Storage conflict %u: HRESULT %08X draws %u descriptors %u\n",conflict,UINT(rejectedResult),rejected.DrawCount(),rejected.DescriptorCount());
                Require(FAILED(rejectedResult) && !rejected.DrawCount() && !rejected.DescriptorCount(),
                    "Conflicting/partial storage scope accepted or published partial bank");
                const auto diagnostic=rejected.LastSourcePassFailure();Require(diagnostic.stage && diagnostic.bits && diagnostic.input==1 &&
                    diagnostic.parameter==changed[0].parameter,"Storage rejection lost its source/parameter");
                if(conflict==4 || conflict==5 || conflict==6 || conflict==7)Failure(rejected,SourcePassFailureStage::Storage,
                    uint64_t(SourcePassFailureReason::StorageConflict),1,changed[0].parameter);
                Check(rejected.CreateSourcePass(device,sharedDraws));Require(rejected.DescriptorCount()==tableCount,"Storage failure poisoned a later valid transaction");
                Cleared(rejected);
            }
        }
        if(!sharedStorage)StorageOverflow(device);
        auto rejectLayout=[&](std::span<const D3D12_DESCRIPTOR_RANGE> candidate,const char* why){
            const auto original=parameters[33].DescriptorTable;
            parameters[33].DescriptorTable={UINT(candidate.size()),candidate.data()};
            ComPtr<ID3DBlob> rejected;const auto serialized=D3D12SerializeRootSignature(&layout,D3D_ROOT_SIGNATURE_VERSION_1,&rejected,&error);
            parameters[33].DescriptorTable=original;
            if(FAILED(serialized))return;
            const SourceBankDraw invalid{root,{static_cast<const uint8_t*>(rejected->GetBufferPointer()),rejected->GetBufferSize()},tables};
            StereoBindingBank rejectedBank;
            Require(FAILED(rejectedBank.CreateSourcePass(device,{&invalid,1})) && !rejectedBank.DrawCount(),why);
        };
        std::array<D3D12_DESCRIPTOR_RANGE,3> candidate{};std::copy(std::begin(mixed),std::end(mixed),candidate.begin());
        candidate[2].OffsetInDescriptorsFromTableStart=1;
        rejectLayout(candidate,"Unbounded tail aliased its finite prefix");
        std::copy(std::begin(mixed),std::end(mixed),candidate.begin());candidate[1].NumDescriptors=UINT_MAX;
        rejectLayout(candidate,"Multiple unbounded ranges/APPEND after an unbounded range accepted");
        std::copy(std::begin(mixed),std::end(mixed),candidate.begin());std::swap(candidate[1],candidate[2]);
        rejectLayout(candidate,"Finite APPEND after an unbounded range accepted");
        std::copy(std::begin(mixed),std::end(mixed),candidate.begin());candidate[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        rejectLayout(candidate,"Mixed sampler/resource unbounded table accepted");
        std::copy(std::begin(mixed),std::end(mixed),candidate.begin());candidate[2].BaseShaderRegister=UINT_MAX-1;
        rejectLayout(candidate,"Actual admitted unbounded shader register range overflow accepted");
        const D3D12_DESCRIPTOR_RANGE aliases[]{{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,1,0,0}};
        const auto original=parameters[33].DescriptorTable;parameters[33].DescriptorTable={2,aliases};
        ComPtr<ID3DBlob> aliasBlob;Check(D3D12SerializeRootSignature(&layout,D3D_ROOT_SIGNATURE_VERSION_1,&aliasBlob,&error));
        parameters[33].DescriptorTable=original;
        ComPtr<ID3D12RootSignature> aliasRoot;Check(device->CreateRootSignature(0,aliasBlob->GetBufferPointer(),aliasBlob->GetBufferSize(),IID_PPV_ARGS(&aliasRoot)));
        auto aliasTables=tables;aliasTables[0].count=1;
        const SourceBankDraw aliasDraw{aliasRoot,{static_cast<const uint8_t*>(aliasBlob->GetBufferPointer()),aliasBlob->GetBufferSize()},aliasTables};
        StereoBindingBank aliasBank;Check(aliasBank.CreateSourcePass(device,{&aliasDraw,1}));
        Require(aliasBank.DescriptorCount()==1,"Existing finite descriptor aliases were unnecessarily rejected");
    }
    {
        std::array<D3D12_DESCRIPTOR_RANGE,64> wideRanges{};std::array<D3D12_ROOT_PARAMETER,64> wideParameters{};
        for(UINT i=0;i<wideParameters.size();++i){wideRanges[i]={D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,100+i,99,0};
            wideParameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;wideParameters[i].DescriptorTable={1,&wideRanges[i]};}
        wideRanges[0]=ranges[0];wideParameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        wideParameters[63].DescriptorTable={3,mixed};wideParameters[63].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        const D3D12_ROOT_SIGNATURE_DESC d{64,wideParameters.data(),0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        ComPtr<ID3DBlob> signature;Check(D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error));
        ComPtr<ID3D12RootSignature> wideRoot;Check(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&wideRoot)));
        auto wideTables=tables;wideTables[0].parameter=63;
        const SourceBankDraw wide{wideRoot,{static_cast<const uint8_t*>(signature->GetBufferPointer()),signature->GetBufferSize()},wideTables};
        StereoBindingBank highest;Check(highest.CreateSourcePass(device,{&wide,1}));Require(highest.DescriptorCount()==tableCount,"Original parameter63 was truncated to a 32-bit table mask");
    }
    {
        auto stable=tables;stable[0].identity=20;stable[0].version=1;stable[1].identity=21;stable[1].version=1;
        const SourceBankDraw draws[]{{root,bytes,stable},{root,bytes,stable}};StereoBindingBank repeated;
        Check(repeated.CreateSourcePass(device,draws));Require(repeated.DescriptorCount()==tableCount && repeated.DrawCount()==2,"Explicit stable original table was not deduplicated");
        for(UINT conflict=0;conflict<4;++conflict){auto changed=stable;
            if(conflict==0)changed[0].version=2;if(conflict==1)changed[0].offset=tableCount;if(conflict==2)changed[1].identity=20;
            if(conflict==3)changed[0].count=tableCount-1;
            const SourceBankDraw bad[]{{root,bytes,stable},{root,bytes,changed}};StereoBindingBank rejected;
            Require(FAILED(rejected.CreateSourcePass(device,bad)) && !rejected.DrawCount(),"Conflicting stable source identity reused descriptors");
            if(conflict==0)Failure(rejected,SourcePassFailureStage::Storage,uint64_t(SourcePassFailureReason::LegacyConflict),1,33);
        }
        std::vector<uint8_t> different(bytes.begin(),bytes.end());different.back()^=1;
        const SourceBankDraw bad[]{{root,bytes,tables},{root,different,tables}};StereoBindingBank rejected;
        Require(FAILED(rejected.CreateSourcePass(device,bad)),"One original root accepted conflicting serialized layouts");
        Failure(rejected,SourcePassFailureStage::RootBytes,uint64_t(SourcePassFailureReason::BlobMismatch),1);
    }
    {
        const SourceBankDraw samplerDraw{root,bytes,{tables.data()+1,1}};
        std::vector<SourceBankDraw> crowded(2049,samplerDraw);StereoBindingBank rejected;
        Require(FAILED(rejected.CreateSourcePass(device,crowded)) && !rejected.DrawCount(),"Dynamic sampler heap capacity overflow accepted");
        Failure(rejected,SourcePassFailureStage::Capacity,uint64_t(SourcePassFailureReason::DescriptorCapacity)|uint64_t(SourcePassFailureReason::SamplerCapacity),2048,0);
        auto stable=tables[1];stable.identity=30;stable.version=1;
        for(auto& draw:crowded)draw.tables={&stable,1};StereoBindingBank shared;
        Check(shared.CreateSourcePass(device,crowded));Require(shared.DrawCount()==2049,"Explicit sampler identity was not shared within heap capacity");
    }
    auto firstTables=tables,secondTables=tables;
    if(sharedStorage){Require(unbounded,"Different-count GPU union fixture requires an unbounded tail");
        firstTables[0].count=tableCount-1;firstTables[0].storageScope=secondTables[0].storageScope=900;
        firstTables[0].storageExtent=secondTables[0].storageExtent=tableCount;
        secondSentinel.Attach(new Sentinel(secondDestroyed));secondTables[0].leases={constants,texture,unordered,secondSentinel};}
    const SourceBankDraw draws[]{{root,bytes,firstTables},{root,bytes,secondTables}};Check(bank->CreateSourcePass(device,draws));
    Cleared(*bank);
    Require(bank->DrawCount()==2 && bank->DescriptorCount()==tableCount*(sharedStorage?1:2) && !bank->SnapshotCount() && !bank->ConstantBytes(),
        "Source occurrences collapsed by pointer or original inputs were frozen");
    if(sharedStorage)Require(firstTables[0].count==4 && secondTables[0].count==5,"GPU union fixture changed source counts");
    Require(FAILED(bank->CreateSourcePass(device,draws)),"Immutable source bank was recreated");
    Failure(*bank,SourcePassFailureStage::Arguments,uint64_t(SourcePassFailureReason::AlreadyInitialized));
    write(constants.Get(),11);device->CreateConstantBufferView(nullptr,at(0));
    if(unbounded)for(UINT n=2;n<tableCount;++n)device->CreateShaderResourceView(nullptr,&srv,at(n));
    else device->CreateShaderResourceView(nullptr,&srv,at(1));
    device->CreateUnorderedAccessView(nullptr,nullptr,&uav,at(unbounded?1:2));
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_BORDER;sampler.BorderColor[0]=.75f;device->CreateSampler(&sampler,samplers->GetCPUDescriptorHandleForHeapStart());
    const char* shader=unbounded?R"(
cbuffer Original : register(b0){float value;}; Texture2D<float4> sources[]:register(t0);
RWStructuredBuffer<float> unorderedSource:register(u0); SamplerState originalSampler:register(s0);
float4 VS(uint id:SV_VertexID):SV_Position{return float4(id==2?3:-1,id==1?3:-1,0,1);}
float4 PS(float4 position:SV_Position):SV_Target{uint index=min((uint)position.x+1,2u);
return float4(value+sources[NonUniformResourceIndex(index)].SampleLevel(originalSampler,float2(2,2),0).r*index+unorderedSource[0],0,0,1);})":R"(
cbuffer Original : register(b0){float value;}; Texture2D<float4> source:register(t0);
RWStructuredBuffer<float> unorderedSource:register(u0); SamplerState originalSampler:register(s0);
float4 VS(uint id:SV_VertexID):SV_Position{return float4(id==2?3:-1,id==1?3:-1,0,1);}
float4 PS():SV_Target{return float4(value+source.SampleLevel(originalSampler,float2(2,2),0).r+unorderedSource[0],0,0,1);})";
    const UINT compileFlags=D3DCOMPILE_OPTIMIZATION_LEVEL3|(unbounded?D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES:0);
    ComPtr<ID3DBlob> vs,ps;Check(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"VS","vs_5_1",compileFlags,0,&vs,&error));
    Check(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"PS","ps_5_1",compileFlags,0,&ps,&error));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root.Get();pso.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pso.NumRenderTargets=1;pso.RTVFormats[0]=image.Format;pso.SampleDesc.Count=1;pso.SampleMask=UINT_MAX;
    pso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pso.RasterizerState.DepthClipEnable=TRUE;
    auto& blend=pso.BlendState.RenderTarget[0];blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;
    blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=15;
    ComPtr<ID3D12PipelineState> pipeline;Check(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&pipeline)));
    ComPtr<ID3D12CommandAllocator> allocator,next;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&next)));
    ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    Require(!bank->CaptureEye(list.Get(),0) && !bank->Bind(list.Get()) && !bank->BindSourceEye(list.Get(),0) && !bank->BindSource(list.Get(),2),"Paired/source bank modes or draw indices were confused");
    {
        ComPtr<ID3D12CommandAllocator> compute;ComPtr<ID3D12GraphicsCommandList> commands;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,IID_PPV_ARGS(&compute)));
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COMPUTE,compute.Get(),nullptr,IID_PPV_ARGS(&commands)));
        Require(!bank->BindSource(commands.Get()),"Original graphics tables bound on a compute recording");Check(commands->Close());
    }
    {
        StereoBindingBank samplerOnly,empty;const SourceBankDraw s{root,bytes,{tables.data()+1,1}},e{root,bytes,{}};
        Check(samplerOnly.CreateSourcePass(device,{&s,1}));Check(empty.CreateSourcePass(device,{&e,1}));
        Require(!samplerOnly.DescriptorCount() && samplerOnly.BindSource(list.Get()) && empty.BindSource(list.Get()),"Unused original parameters required invented table contents");
    }
    const float color[]{.25f,0,0,1};list->ClearRenderTargetView(textureRtv,color,0,nullptr);Transition(list.Get(),texture.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    list->CopyBufferRegion(unordered.Get(),0,upload.Get(),0,4);Transition(list.Get(),unordered.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->SetPipelineState(pipeline.Get());list->OMSetRenderTargets(1,&targetRtv,FALSE,nullptr);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for(UINT draw=0;draw<2;++draw){Require(bank->BindSource(list.Get(),draw),"Original parameter33 source bind failed");
        D3D12_VIEWPORT viewport{float(draw),0,1,1,0,1};D3D12_RECT scissor{LONG(draw),0,LONG(draw+1),1};list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);list->DrawInstanced(3,1,0,0);}
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total{};device->GetCopyableFootprints(&image,0,1,0,&footprint,nullptr,nullptr,&total);
    auto readback=buffer(total,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(list.Get(),target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=target.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);Check(list->Close());
    ComPtr<ID3D12Fence> gate,complete;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&complete)));
    Check(queue->Wait(gate.Get(),1));ID3D12CommandList* lists[]{list.Get()};auto submitted=cvr::gpu::PrepareCommandResources(1,lists);
    Require(!submitted.batches.empty(),"Original source bank was not kept on its recording");queue->ExecuteCommandLists(1,lists);cvr::gpu::SubmitCommandResources(queue,std::move(submitted));
    Check(list->Reset(next.Get(),nullptr));cvr::gpu::ResetCommandResources(list.Get());
    Failure(*bank,SourcePassFailureStage::Arguments,uint64_t(SourcePassFailureReason::AlreadyInitialized));
    Require(bank->BindSource(list.Get()),"Immutable source descriptors could not be kept on a new certified recording");Check(list->Close());
    Check(list->Reset(next.Get(),nullptr));cvr::gpu::ResetCommandResources(list.Get());Check(list->Close());
    bank.reset();tables={};firstTables={};secondTables={};constants.Reset();texture.Reset();unordered.Reset();cpu.Reset();samplers.Reset();sentinel.Reset();secondSentinel.Reset();
    Require(!destroyed && !secondDestroyed,"Submitted original/storage-hit descriptors lost their draw owners");Check(gate->Signal(1));Check(queue->Signal(complete.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Source fence event");Check(complete->SetEventOnCompletion(1,event));
    const auto waited=WaitForSingleObject(event,5000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"Source GPU timeout");cvr::gpu::CollectCommandResources();
    Require(destroyed==1,"Completed source bank leaked read-resource owners");
    Require(secondDestroyed==(sharedStorage?1u:0u),"Storage hit omitted or leaked the second draw's fenced owner");
    void* data{};D3D12_RANGE range{0,SIZE_T(total)};Check(readback->Map(0,&range,&data));const auto* pixels=static_cast<const float*>(data);
    const bool equal=pixels[0]==13.25f && pixels[4]==(unbounded?13.5f:13.25f);D3D12_RANGE none{};readback->Unmap(0,&none);
    Require(equal,"Original CBV/SRV/UAV/sampler descriptors changed or source buffer contents were frozen");
}
static void DirectGpuInputs(ID3D12Device* device,ID3D12CommandQueue* queue,ID3D12RootSignature* root,const PairedRootPlan& plan,
    std::span<const PairedBinding> requested){
    auto buffer=[&](UINT64 size,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=size;
        desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> value;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&value)));return value;
    };
    auto upload=buffer(512,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto constants=buffer(512,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    void* mapped{};D3D12_RANGE none{};Check(upload->Map(0,&none,&mapped));std::memset(mapped,0,512);
    const float left=6,right=7;std::memcpy(mapped,&left,4);std::memcpy(static_cast<uint8_t*>(mapped)+256,&right,4);upload->Unmap(0,nullptr);
    D3D12_RESOURCE_DESC image{};image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;image.Width=image.Height=1;
    image.DepthOrArraySize=image.MipLevels=image.SampleDesc.Count=1;image.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    image.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> texture,target;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&texture)));
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target)));
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=image.Format;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels=1;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    std::atomic<UINT> destroyed{};ComPtr<IUnknown> owner;owner.Attach(new Sentinel(destroyed));
    std::array<BankPair,2> pairs{};pairs[0].binding=requested[0];pairs[1].binding=requested[1];
    for(UINT eye=0;eye<2;++eye){auto& cbv=pairs[0].eyes[eye];cbv.source=constants;cbv.owner=owner;
        cbv.gpuCbv={constants->GetGPUVirtualAddress()+UINT64(eye)*256,256};
        auto& srv=pairs[1].eyes[eye];srv.source=texture;srv.owner=owner;srv.borrowedSrv=true;srv.implicitSrv=eye==1;srv.srv=view;
    }
    for(UINT changed=0;changed<5;++changed){auto bad=pairs;
        if(changed==0)bad[0].eyes[0].gpuCbv.BufferLocation++;
        if(changed==1)bad[0].eyes[1].gpuCbv.SizeInBytes=512;
        if(changed==2)bad[0].eyes[0].owner.Reset();
        if(changed==3)bad[1].eyes[0].owner.Reset();
        if(changed==4)bad[0].eyes[0].constants={reinterpret_cast<const uint8_t*>(&left),4};
        StereoBindingBank rejected;Require(FAILED(rejected.Create(device,root,plan,bad)) && !rejected.DescriptorCount(),"Malformed direct GPU input accepted");
    }
    auto bank=std::make_unique<StereoBindingBank>();Check(bank->Create(device,root,plan,pairs));
    Require(!bank->ConstantBytes() && !bank->SnapshotCount(),"Owned GPU inputs were copied back through CPU or resnapshotted");
    const char shader[]=R"(
cbuffer Left:register(b2,space100){float left;}; cbuffer Right:register(b3,space100){float right;};
Texture2D<float4> a:register(t0,space101); Texture2D<float4> b:register(t1,space101);
struct V{float4 p:SV_Position;float2 values:TEXCOORD0;};
V VS(uint id:SV_VertexID){V v;v.p=float4(id==2?3:-1,id==1?3:-1,0,1);v.values=float2(left,right);return v;}
float4 PS(V v):SV_Target{return float4(v.values,a.Load(int3(0,0,0)).r+b.Load(int3(0,0,0)).r,1);})";
    ComPtr<ID3DBlob> vs,ps,error;Check(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"VS","vs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&error));
    Check(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"PS","ps_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&error));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};desc.pRootSignature=root;desc.VS={vs->GetBufferPointer(),vs->GetBufferSize()};desc.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;desc.NumRenderTargets=1;desc.RTVFormats[0]=image.Format;desc.SampleDesc.Count=1;desc.SampleMask=UINT_MAX;
    desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;desc.RasterizerState.DepthClipEnable=TRUE;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask=15;ComPtr<ID3D12PipelineState> pipeline;
    Check(device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&pipeline)));
    ComPtr<ID3D12CommandAllocator> allocator,next;ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&next)));
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12DescriptorHeap> rtvs;const D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,2,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
    Check(device->CreateDescriptorHeap(&rtvDesc,IID_PPV_ARGS(&rtvs)));auto a=rtvs->GetCPUDescriptorHandleForHeapStart(),b=a;
    b.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);device->CreateRenderTargetView(texture.Get(),nullptr,a);device->CreateRenderTargetView(target.Get(),nullptr,b);
    const float color[]{.25f,0,0,1};list->ClearRenderTargetView(a,color,0,nullptr);
    Transition(list.Get(),texture.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    list->CopyBufferRegion(constants.Get(),0,upload.Get(),0,512);Transition(list.Get(),constants.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    Require(bank->CaptureEye(list.Get(),0) && bank->CaptureEye(list.Get(),1) && bank->Bind(list.Get()),"Direct GPU bank did not bind on its retained recording");
    list->SetPipelineState(pipeline.Get());list->OMSetRenderTargets(1,&b,FALSE,nullptr);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport{0,0,1,1,0,1};const D3D12_RECT scissor{0,0,1,1};list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);list->DrawInstanced(3,1,0,0);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};device->GetCopyableFootprints(&image,0,1,0,&footprint,nullptr,nullptr,&bytes);
    auto readback=buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(list.Get(),target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=target.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);Check(list->Close());
    ComPtr<ID3D12Fence> gate,done;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));
    Check(queue->Wait(gate.Get(),1));ID3D12CommandList* submittedList=list.Get();auto submission=cvr::gpu::PrepareCommandResources(1,&submittedList);
    queue->ExecuteCommandLists(1,&submittedList);cvr::gpu::SubmitCommandResources(queue,std::move(submission));
    Check(list->Reset(next.Get(),nullptr));cvr::gpu::ResetCommandResources(list.Get());Check(list->Close());bank.reset();pairs={};owner.Reset();
    Require(!destroyed,"Submitted direct GPU versions lost their lifetime owner");Check(gate->Signal(1));Check(queue->Signal(done.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Direct GPU fence event");Check(done->SetEventOnCompletion(1,event));
    const auto waited=WaitForSingleObject(event,5000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"Direct GPU read timeout");cvr::gpu::CollectCommandResources();
    Require(destroyed==1,"Direct GPU owner remained retained after completion");
    D3D12_RANGE range{0,SIZE_T(bytes)};Check(readback->Map(0,&range,&mapped));const auto* pixel=static_cast<const float*>(mapped);
    const bool equal=pixel[0]==6 && pixel[1]==7 && pixel[2]==.5f && pixel[3]==1;readback->Unmap(0,&none);
    Require(equal,"GPU-resident per-eye CBVs or explicit/default borrowed SRVs differed");
}
int main(int argc,char** argv) try {
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    const bool hardware=argc>1 && std::strcmp(argv[1],"hardware")==0;
    ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> adapter;
    if(hardware){ComPtr<IDXGIFactory6> modern;if(FAILED(factory.As(&modern)))return 77;
        for(UINT index=0;;++index){ComPtr<IDXGIAdapter1> candidate;
            const auto result=modern->EnumAdapterByGpuPreference(index,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&candidate));
            if(result==DXGI_ERROR_NOT_FOUND)break;Check(result);
            if(!candidate)continue;DXGI_ADAPTER_DESC1 description{};Check(candidate->GetDesc1(&description));
            if(!(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(D3D12CreateDevice(candidate.Get(),D3D_FEATURE_LEVEL_12_0,__uuidof(ID3D12Device),nullptr))){adapter=candidate;break;}
        }if(!adapter)return 77;
    }else Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
    D3D12_DESCRIPTOR_RANGE ranges[]{
        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,1,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,3,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,3,0,0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,1,0,0,0}};
    D3D12_ROOT_PARAMETER params[5]{};
    for(UINT i=0;i<5;++i){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,ranges+i};
        params[i].ShaderVisibility=i==0 || i==2?D3D12_SHADER_VISIBILITY_VERTEX:D3D12_SHADER_VISIBILITY_PIXEL;}
    D3D12_ROOT_SIGNATURE_DESC desc{5,params,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> signature,error;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error));
    const PairedBinding requested[]{{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_VERTEX,1,100},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,D3D12_SHADER_VISIBILITY_PIXEL,0,101}};
    PairedRootPlan plan;Check(BuildPairedRoot(signature->GetBufferPointer(),signature->GetBufferSize(),requested,plan));
    ComPtr<ID3D12RootSignature> root;Check(device->CreateRootSignature(0,plan.serialized.data(),plan.serialized.size(),IID_PPV_ARGS(&root)));
    D3D12_HEAP_PROPERTIES memory{};memory.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC image{};image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;image.Width=image.Height=1;
    image.DepthOrArraySize=image.MipLevels=image.SampleDesc.Count=1;image.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;image.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> sharedTexture,target;Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&sharedTexture)));
    image.Width=2;
    Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target)));
    ComPtr<ID3D12DescriptorHeap> cpuHeap,samplerHeap,rtvs,visibleHeap;
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&cpuHeap)));
    hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&visibleHeap)));
    hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&samplerHeap)));
    hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=2;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs)));
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=image.Format;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    device->CreateShaderResourceView(sharedTexture.Get(),&view,cpuHeap->GetCPUDescriptorHandleForHeapStart());
    D3D12_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;sampler.MaxLOD=D3D12_FLOAT32_MAX;device->CreateSampler(&sampler,samplerHeap->GetCPUDescriptorHandleForHeapStart());
    auto sharedRtv=rtvs->GetCPUDescriptorHandleForHeapStart(),targetRtv=sharedRtv;targetRtv.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    device->CreateRenderTargetView(sharedTexture.Get(),nullptr,sharedRtv);device->CreateRenderTargetView(target.Get(),nullptr,targetRtv);
    std::atomic<UINT> destroyed{};ComPtr<IUnknown> sentinel;sentinel.Attach(new Sentinel(destroyed));
    std::array<SharedBankTable,3> shared{};
    for(UINT i=0;i<2;++i){shared[i].parameter=i+2;shared[i].count=1;shared[i].cpuHeap=cpuHeap;shared[i].leases={sharedTexture,sentinel};}
    shared[2].parameter=4;shared[2].count=1;shared[2].cpuHeap=samplerHeap;
    float left=6,right=7;
    std::array<BankPair,2> pairs{};pairs[0].binding=requested[0];pairs[1].binding=requested[1];
    pairs[0].eyes[0].constants={reinterpret_cast<const uint8_t*>(&left),4};pairs[0].eyes[1].constants={reinterpret_cast<const uint8_t*>(&right),4};
    pairs[1].eyes[0].srv=pairs[1].eyes[1].srv=view;
    auto bank=std::make_unique<StereoBindingBank>();
    {auto bad=shared;bad[0].cpuHeap=visibleHeap;Require(FAILED(bank->Create(device.Get(),root.Get(),plan,pairs,bad)),"Shader-visible descriptor copy accepted");}
    {auto bad=shared;bad[2].parameter=2;Require(FAILED(bank->Create(device.Get(),root.Get(),plan,pairs,bad)),"Wrong table kind accepted");}
    {auto bad=pairs;bad[0].eyes[1].constants={};Require(FAILED(bank->Create(device.Get(),root.Get(),plan,bad,shared)),"Empty CBV accepted");}
    {auto bad=pairs;bad[1].eyes[0].srv.Shader4ComponentMapping=0;Require(FAILED(bank->Create(device.Get(),root.Get(),plan,bad,shared)),"Invalid null view accepted");}
    {auto bad=plan;bad.bindings[1].destinationOffset=1;Require(FAILED(bank->Create(device.Get(),root.Get(),bad,pairs,shared)),"Out-of-bounds pair accepted");}
    Require(!bank->DescriptorCount(),"Failed creation was not transactional");
    {
        StereoBindingBank arena;Require(FAILED(arena.CreateArena(device.Get(),root.Get(),plan,2,1024)),"SRV arena accepted");
        PairedRootPlan cbvPlan;Check(BuildPairedRoot(signature->GetBufferPointer(),signature->GetBufferSize(),{requested,1},cbvPlan));
        ComPtr<ID3D12RootSignature> cbvRoot;Check(device->CreateRootSignature(0,cbvPlan.serialized.data(),cbvPlan.serialized.size(),IID_PPV_ARGS(&cbvRoot)));
        Check(arena.CreateArena(device.Get(),cbvRoot.Get(),cbvPlan,2,1024));UINT index=UINT_MAX;
        auto bad=pairs[0];bad.eyes[1].constants={};Require(FAILED(arena.AppendConstants(cbvPlan,{&bad,1},index)),"Empty arena CBV accepted");
        auto changed=cbvPlan;++changed.bindings[0].destinationOffset;
        Require(FAILED(arena.AppendConstants(changed,{pairs.data(),1},index)),"Changed arena layout accepted");
        Require(index==UINT_MAX && !arena.DrawCount() && !arena.ConstantBytes() && !arena.DescriptorCount(),"Failed arena append changed publication");
        Check(arena.AppendConstants(cbvPlan,{pairs.data(),1},index));Require(index==0,"First arena index");
        Check(arena.AppendConstants(cbvPlan,{pairs.data(),1},index));Require(index==1 && arena.ConstantBytes()==1024 && arena.DescriptorCount()==4,"Second arena entry overwrote the first");
        Require(FAILED(arena.AppendConstants(cbvPlan,{pairs.data(),1},index)) && index==1 && arena.DrawCount()==2,"Arena capacity ignored");
        Require(arena.RestartArena() && !arena.DrawCount() && !arena.ConstantBytes(),"Unsubmitted arena reset failed");
        Check(arena.AppendConstants(cbvPlan,{pairs.data(),1},index));Require(index==0,"Restart did not reset arena offsets");
        StereoBindingBank limited;Check(limited.CreateArena(device.Get(),cbvRoot.Get(),cbvPlan,2,256));
        Require(limited.AppendConstants(cbvPlan,{pairs.data(),1},index)==E_OUTOFMEMORY && !limited.DrawCount() && !limited.ConstantBytes(),"Arena byte limit not transactional");
    }
    float alternateRight=8;auto alternate=pairs;alternate[0].eyes[1].constants={reinterpret_cast<const uint8_t*>(&alternateRight),4};
    const BankDraw draws[]{{root.Get(),&plan,pairs,shared},{root.Get(),&plan,alternate,shared}};
    Check(bank->CreatePass(device.Get(),draws));
    Require(bank->DrawCount()==2 && bank->DescriptorCount()==9 && bank->SnapshotCount()==0 && bank->ConstantBytes()==768,"Shared tables/constants were not deduplicated across materials");
    Require(FAILED(bank->Create(device.Get(),root.Get(),plan,pairs,shared)),"Frozen bank mutated");
    left=right=alternateRight=99;
    device->CreateShaderResourceView(nullptr,&view,cpuHeap->GetCPUDescriptorHandleForHeapStart());
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_BORDER;sampler.BorderColor[0]=.75f;device->CreateSampler(&sampler,samplerHeap->GetCPUDescriptorHandleForHeapStart());
    shared={};cpuHeap.Reset();samplerHeap.Reset();sentinel.Reset();
    const char shader[]=R"(
cbuffer Left : register(b2,space100){float left;}; cbuffer Right : register(b3,space100){float right;};
Texture2D<float4> commonTexture:register(t3); SamplerState commonSampler:register(s0);
Texture2D<float4> missing0:register(t0,space101); Texture2D<float4> missing1:register(t1,space101);
struct V {float4 p:SV_Position;float value:TEXCOORD;};
V VS(uint id:SV_VertexID){V v;v.p=float4(id==2?3:-1,id==1?3:-1,0,1);v.value=left+right;return v;}
float4 PS(V v):SV_Target{return float4(v.value+commonTexture.SampleLevel(commonSampler,float2(2,2),0).r+missing0.Load(int3(0,0,0)).r+missing1.Load(int3(0,0,0)).r,0,0,1);})";
    ComPtr<ID3DBlob> vs,ps;Check(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"VS","vs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&error));
    Check(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"PS","ps_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&error));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root.Get();pso.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pso.NumRenderTargets=1;pso.RTVFormats[0]=image.Format;pso.SampleDesc.Count=1;pso.SampleMask=UINT_MAX;
    pso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pso.RasterizerState.DepthClipEnable=TRUE;
    auto& b=pso.BlendState.RenderTarget[0];b.SrcBlend=b.SrcBlendAlpha=D3D12_BLEND_ONE;b.DestBlend=b.DestBlendAlpha=D3D12_BLEND_ZERO;b.BlendOp=b.BlendOpAlpha=D3D12_BLEND_OP_ADD;b.LogicOp=D3D12_LOGIC_OP_NOOP;b.RenderTargetWriteMask=15;
    ComPtr<ID3D12PipelineState> pipeline;Check(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&pipeline)));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator,next;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&next)));
    ComPtr<ID3D12GraphicsCommandList> list,other;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,next.Get(),nullptr,IID_PPV_ARGS(&other)));Check(other->Close());
    const float color[]{.25f,0,0,1};list->ClearRenderTargetView(sharedRtv,color,0,nullptr);Transition(list.Get(),sharedTexture.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Require(!bank->Bind(list.Get()),"Uncaptured bank bound");Require(bank->CaptureEye(list.Get(),0),"Eye zero capture");
    Require(!bank->Bind(list.Get()) && !bank->CaptureEye(list.Get(),0) && !bank->CaptureEye(other.Get(),1),"Partial/duplicate/other-recording capture accepted");
    Require(bank->CaptureEye(list.Get(),1) && bank->Bind(list.Get()),"Complete bank rejected");
    Require(!bank->Bind(list.Get(),2),"Unknown draw index accepted");
    sharedTexture.Reset();Require(!destroyed,"Shared lease released before recording");
    list->SetPipelineState(pipeline.Get());list->OMSetRenderTargets(1,&targetRtv,FALSE,nullptr);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for(UINT draw=0;draw<2;++draw) {
        Require(bank->Bind(list.Get(),draw),"Material binding selection failed");
        D3D12_VIEWPORT viewport{float(draw),0,1,1,0,1};D3D12_RECT scissor{LONG(draw),0,LONG(draw+1),1};
        list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);list->DrawInstanced(3,1,0,0);
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};device->GetCopyableFootprints(&image,0,1,0,&footprint,nullptr,nullptr,&bytes);
    D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    memory.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> readback;Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    Transition(list.Get(),target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=target.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
    list->CopyTextureRegion(&to,0,0,0,&from,nullptr);Check(list->Close());
    ComPtr<ID3D12Fence> gate,complete;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&complete)));
    Check(queue->Wait(gate.Get(),1));ID3D12CommandList* lists[]{list.Get()};auto submission=cvr::gpu::PrepareCommandResources(1,lists);
    Require(!submission.batches.empty(),"Bank owner not retained");queue->ExecuteCommandLists(1,lists);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(submission));
    Check(list->Reset(next.Get(),nullptr));cvr::gpu::ResetCommandResources(list.Get());
    Require(!bank->Bind(list.Get()),"Reset recording reused old bank");bank.reset();Require(!destroyed,"GPU flight lost shared lease");Check(list->Close());
    Check(gate->Signal(1));Check(queue->Signal(complete.Get(),1));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event");Check(complete->SetEventOnCompletion(1,event));auto wait=WaitForSingleObject(event,5000);CloseHandle(event);Require(wait==WAIT_OBJECT_0,"GPU timeout");
    cvr::gpu::CollectCommandResources();Require(destroyed==1,"Completed bank leaked shared lease");
    void* data{};D3D12_RANGE range{0,SIZE_T(bytes)};Check(readback->Map(0,&range,&data));const float a=static_cast<const float*>(data)[0],bValue=static_cast<const float*>(data)[4];D3D12_RANGE none{};readback->Unmap(0,&none);
    Require(a==13.25f && bValue==14.25f,"Material constants/shared texture/sampler/null descriptors changed after source overwrite");
    SourcePass(device.Get(),queue.Get());
    SourcePass(device.Get(),queue.Get(),true);
    SourcePass(device.Get(),queue.Get(),true,true);
    DirectGpuInputs(device.Get(),queue.Get(),root.Get(),plan,requested);
    ComPtr<ID3D12InfoQueue> info;Check(device.As(&info));for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T n{};info->GetMessage(i,nullptr,&n);std::vector<char> data(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());info->GetMessage(i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",m->pDescription);throw std::runtime_error("D3D12 validation error");}}
    std::puts("PASS paired and original-root banks, parameters33/63, finite and admitted unbounded extents, explicit union storage with distinct source counts, scope conflicts/cap rollback, dynamic SRV readback, source versions, Reset and per-draw GPU-fence lifetime");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
