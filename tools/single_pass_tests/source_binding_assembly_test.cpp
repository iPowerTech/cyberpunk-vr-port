#include "Render/SourceBindingAssembly.hpp"
#include "Render/CommandResources.hpp"
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <source_location>
#include <stdexcept>
#include <string>

using namespace cvr::stereo;
using Microsoft::WRL::ComPtr;
namespace assembly=cvr::stereo::source_binding_assembly;
namespace audit=cvr::stereo::descriptors;
static void Require(bool value,const char* message,const std::source_location at=std::source_location::current()){
    if(!value)throw std::runtime_error(std::string(message)+" at line "+std::to_string(at.line()));
}
static void Check(HRESULT value){if(FAILED(value))throw std::runtime_error("D3D12 HRESULT "+std::to_string(uint32_t(value)));}
class Owner final:public IUnknown {
    std::atomic<ULONG> refs_{1};std::atomic<UINT>& destroyed_;std::vector<ComPtr<IUnknown>> children_;
public:
    Owner(std::atomic<UINT>& destroyed,std::initializer_list<IUnknown*> children):destroyed_(destroyed){
        for(auto* child:children)children_.emplace_back(child);
    }
    ~Owner(){++destroyed_;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** output)override{
        if(!output)return E_POINTER;*output=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*output=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto left=--refs_;if(!left)delete this;return left;}
};
static ComPtr<IUnknown> Lease(std::atomic<UINT>& count,std::initializer_list<IUnknown*> children={}){
    ComPtr<IUnknown> owner;owner.Attach(new Owner(count,children));return owner;
}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,UINT bytes,D3D12_HEAP_TYPE type){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width=bytes;desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
        type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        nullptr,IID_PPV_ARGS(&resource)));return resource;
}
static ComPtr<ID3D12DescriptorHeap> Heap(ID3D12Device* device,UINT count){
    const D3D12_DESCRIPTOR_HEAP_DESC desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,count,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
    ComPtr<ID3D12DescriptorHeap> heap;Check(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap)));return heap;
}
struct Fixture {
    std::array<D3D12_ROOT_PARAMETER1,36> parameters{};
    std::array<D3D12_DESCRIPTOR_RANGE1,36> ranges{};
    D3D12_ROOT_SIGNATURE_DESC1 root{};
    std::vector<families::ResourceUse> shaderUses;
    source_selectors::Result selectors;
    std::vector<audit::OwnedSourceTable> finite;
    std::vector<audit::OwnedSourceSlots> sparse;
    source_cbv_snapshot::PrivateCbv privateCbv;
    source_cbv_snapshot::PreparedCopies copies;
    assembly::OriginalCbv safeOriginal;
    std::vector<assembly::FiniteTable> finiteInputs;
    std::vector<assembly::SparseTable> sparseInputs;
    std::array<assembly::PrivateCbv,1> privateInputs;
    assembly::Input input;
    ComPtr<ID3D12Resource> original,privateBuffer;
    ComPtr<ID3D12RootSignature> signature;
    ComPtr<ID3DBlob> serialized;
    ID3D12Device* device{};
    explicit Fixture(ID3D12Device* d,std::atomic<UINT>& owners):device(d){
        for(UINT n=0;n<parameters.size();++n){ranges[n]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,100+n,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
            parameters[n].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[n].DescriptorTable={1,&ranges[n]};
            parameters[n].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
        ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_CBV,4,0,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
        ranges[2]={D3D12_DESCRIPTOR_RANGE_TYPE_CBV,2,5,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};parameters[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
        ranges[26]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,39,0,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
        ranges[31]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,UINT_MAX,0,1,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
        ranges[33]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,UINT_MAX,0,3,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
        root={UINT(parameters.size()),parameters.data(),0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};desc.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;desc.Desc_1_1=root;
        ComPtr<ID3DBlob> errors;Check(D3D12SerializeVersionedRootSignature(&desc,&serialized,&errors));
        Check(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&signature)));
        input.scope={uint64_t(reinterpret_cast<uintptr_t>(signature.Get())),11,12,13,14,1};
        input.selectorScope=input.scope;
        shaderUses={{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_VERTEX,0,1,1,476},
            {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_VERTEX,0,5,1,112},
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,D3D12_SHADER_VISIBILITY_PIXEL,0,13,1,0},
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,D3D12_SHADER_VISIBILITY_PIXEL,1,0,32768,0},
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,D3D12_SHADER_VISIBILITY_PIXEL,3,0,32768,0}};
        input.family={"root","vertex","pixel"};selectors.family=input.family;selectors.selectorEvaluationComplete=true;
        for(const auto parameter:{31u,33u}){source_selectors::TableSelection selection;
            selection.key={"root",D3D12_SHADER_VISIBILITY_PIXEL,parameter,parameter==31?1u:3u,0,0,32768};
            selection.offsets=parameter==31?std::vector<uint32_t>{29614,30989}:std::vector<uint32_t>{32763};selection.count=selection.offsets.back()+1;
            for(const auto offset:selection.offsets){selection.uses.push_back({offset,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV});
                selectors.selections.push_back({UINT(selectors.selections.size()),offset,offset,offset});}
            selectors.tables.push_back(std::move(selection));}
        selectors.evaluatedSites=UINT(selectors.selections.size());input.selectors=&selectors;input.dynamicCoverageCertified=true;
        original=Buffer(device,512,D3D12_HEAP_TYPE_UPLOAD);privateBuffer=Buffer(device,512,D3D12_HEAP_TYPE_DEFAULT);
        const UINT stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_UINT;srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Buffer.NumElements=1;
        for(const auto parameter:{0u,2u,26u}){audit::OwnedSourceTable table;table.parameter=parameter;table.count=parameter==0?4:parameter==2?2:39;
            table.cpuHeap=Heap(device,table.count);table.owner=Lease(owners,{original.Get()});table.identity=parameter+1;table.version=1;
            table.sourceGpu=0x100000+UINT64(parameter)*0x10000;table.sourceHeap=parameter+100;
            const auto base=table.cpuHeap->GetCPUDescriptorHandleForHeapStart();
            for(UINT n=0;n<table.count;++n){if(parameter==26)device->CreateShaderResourceView(nullptr,&srv,{base.ptr+SIZE_T(n)*stride});
                else{const D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{original->GetGPUVirtualAddress(),parameter==0?512u:256u};
                    device->CreateConstantBufferView(&cbv,{base.ptr+SIZE_T(n)*stride});}}
            finite.push_back(std::move(table));}
        for(const auto& selected:selectors.tables){audit::OwnedSourceSlots table;table.parameter=selected.key.parameter;table.count=selected.count;
            table.cpuHeap=Heap(device,UINT(selected.offsets.size()));table.owner=Lease(owners);table.identity=table.parameter+1;table.version=1;
            table.sourceGpu=0x1000000+UINT64(table.parameter)*0x100000;table.sourceHeap=table.parameter+1000;
            const auto base=table.cpuHeap->GetCPUDescriptorHandleForHeapStart();
            for(UINT n=0;n<selected.offsets.size();++n){audit::OwnedSourceSlot slot;slot.offset=selected.offsets[n];
                slot.compactIndex=UINT(selected.offsets.size())-n-1;slot.type=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                slot.stamp.heap=table.sourceHeap;slot.stamp.gpu=table.sourceGpu+UINT64(slot.offset)*stride;
                slot.stamp.value[0]=2;slot.stamp.value[4]=srv.Format;slot.stamp.value[5]=srv.ViewDimension;
                slot.stamp.value[6]=srv.Shader4ComponentMapping;slot.stamp.value[8]=1;
                device->CreateShaderResourceView(nullptr,&srv,{base.ptr+SIZE_T(slot.compactIndex)*stride});table.slots.push_back(slot);}
            sparse.push_back(std::move(table));}
        privateCbv.parameter=0;privateCbv.offset=1;privateCbv.reflectedBytes=476;privateCbv.paddedBytes=512;privateCbv.copiedBytes=476;
        privateCbv.resource=privateBuffer;privateCbv.owner=Lease(owners,{privateBuffer.Get()});privateCbv.view={privateBuffer->GetGPUVirtualAddress(),512};
        copies.owner=Lease(owners,{original.Get()});copies.list=11;copies.generation=12;copies.occurrence=13;copies.copiedBytes=476;copies.count=1;
        safeOriginal.scope=input.scope;safeOriginal.parameter=2;safeOriginal.reflectedBytes=112;safeOriginal.view={original->GetGPUVirtualAddress(),256};
        safeOriginal.resource=original;safeOriginal.owner=Lease(owners,{original.Get()});safeOriginal.readEpochCertified=true;
        for(const auto& table:finite)finiteInputs.push_back({input.scope,&table});for(const auto& table:sparse)sparseInputs.push_back({input.scope,&table});
        privateInputs[0]={input.scope,&privateCbv,&copies,true};input.root=&root;input.shaderUses=shaderUses;
        input.finite=finiteInputs;input.sparse=sparseInputs;input.privateCbvs=privateInputs;
    }
};
static void Reject(Fixture& fixture,assembly::Failure expected){assembly::Plan plan;assembly::Failure failure;
    Require(!assembly::Build(fixture.input,plan,failure) && failure==expected && !plan.Ready() && !plan.TableCount(),"Invalid assembly receipt published a plan");}
static void Negatives(Fixture& fixture){
    const auto scope=fixture.finiteInputs[0].scope;
    fixture.finiteInputs[0].scope.eye=0;Reject(fixture,assembly::Failure::Scope);fixture.finiteInputs[0].scope=scope;
    ++fixture.sparseInputs[0].scope.occurrence;Reject(fixture,assembly::Failure::Scope);fixture.sparseInputs[0].scope=scope;
    ++fixture.privateInputs[0].scope.source;Reject(fixture,assembly::Failure::Scope);fixture.privateInputs[0].scope=scope;
    fixture.input.selectorScope.eye=0;Reject(fixture,assembly::Failure::Scope);fixture.input.selectorScope=scope;
    ++fixture.copies.generation;Reject(fixture,assembly::Failure::Scope);--fixture.copies.generation;
    fixture.input.dynamicCoverageCertified=false;Reject(fixture,assembly::Failure::DynamicCoverage);fixture.input.dynamicCoverageCertified=true;
    fixture.selectors.family.vertexSha256="other";Reject(fixture,assembly::Failure::DynamicCoverage);fixture.selectors.family=fixture.input.family;
    auto foreignTable=fixture.selectors.tables[0];foreignTable.key.rootSha256="other";
    fixture.selectors.tables.push_back(foreignTable);Reject(fixture,assembly::Failure::DynamicCoverage);fixture.selectors.tables.pop_back();
    auto& slots=fixture.sparse[0].slots;const auto compact=slots[1].compactIndex;
    slots[1].compactIndex=slots[0].compactIndex;Reject(fixture,assembly::Failure::Collision);slots[1].compactIndex=compact;
    const auto slot=slots[1];slots.pop_back();Reject(fixture,assembly::Failure::Type);slots.push_back(slot);
    const auto offset=slots[0].offset;slots[0].offset=7;Reject(fixture,assembly::Failure::Type);slots[0].offset=offset;
    slots[0].type=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;Reject(fixture,assembly::Failure::Type);slots[0].type=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ++slots[0].stamp.gpu;Reject(fixture,assembly::Failure::Type);--slots[0].stamp.gpu;
    ++fixture.finite[0].count;Reject(fixture,assembly::Failure::Extent);--fixture.finite[0].count;
    fixture.privateCbv.paddedBytes=476;Reject(fixture,assembly::Failure::CbvRange);fixture.privateCbv.paddedBytes=512;
    fixture.privateCbv.view.SizeInBytes=256;fixture.privateCbv.paddedBytes=256;Reject(fixture,assembly::Failure::CbvRange);
    fixture.privateCbv.view.SizeInBytes=512;fixture.privateCbv.paddedBytes=512;
    ++fixture.privateCbv.privateOffset;Reject(fixture,assembly::Failure::CbvRange);--fixture.privateCbv.privateOffset;
    const auto copyOwner=fixture.copies.owner;fixture.copies.owner.Reset();Reject(fixture,assembly::Failure::CbvOwner);fixture.copies.owner=copyOwner;
    auto duplicate=fixture.privateInputs[0];const std::array<assembly::PrivateCbv,2> duplicates{duplicate,duplicate};
    fixture.input.privateCbvs=duplicates;Reject(fixture,assembly::Failure::Collision);fixture.input.privateCbvs=fixture.privateInputs;
    D3D12_DESCRIPTOR_RANGE1 overlap[2]{{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,3,0,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,7,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,1}};
    fixture.parameters[0].DescriptorTable={2,overlap};Reject(fixture,assembly::Failure::Root);fixture.parameters[0].DescriptorTable={1,&fixture.ranges[0]};
    fixture.root.Flags=D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;Reject(fixture,assembly::Failure::Root);
    fixture.root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
}
static void CacheTests(ID3D12Device* device){
    std::atomic<UINT> owners{},oldCopiesDestroyed{},currentCopiesDestroyed{};
    Fixture fixture(device,owners);fixture.input.originalCbvs={&fixture.safeOriginal,1};
    fixture.copies.owner=Lease(oldCopiesDestroyed,{fixture.original.Get()});
    assembly::Plan firstPlan;assembly::Failure failure;Require(assembly::Build(fixture.input,firstPlan,failure) && firstPlan.Ready(),"Cache fixture plan was not ready");
    assembly::CpuTableCache cache,tiny;std::vector<SharedBankTable> first,repeated,changed;
    Require(FAILED(assembly::Assemble(device,firstPlan,tiny,changed,failure,firstPlan.DescriptorCount()-1)) &&
        failure==assembly::Failure::Capacity && changed.empty() && !tiny.TableCount() && !tiny.UniqueDescriptorCount() && !tiny.HitCount() && !tiny.MissCount(),
        "Fresh cache published a partial table/counter before transaction capacity failure");
    Check(assembly::Assemble(device,firstPlan,cache,first,failure));
    Require(cache.UniqueDescriptorCount()==firstPlan.DescriptorCount() && cache.TableCount()==5 && cache.MissCount()==5 && !cache.HitCount(),
        "Unique CPU table cache was accounted by logical source occurrences");
    const auto firstHeap=first[0].cpuHeap.Get();const auto highHeap=first[3].cpuHeap.Get();
    auto scope=fixture.input.scope;++scope.occurrence;++scope.source;scope.eye=0;
    fixture.input.scope=fixture.input.selectorScope=scope;fixture.safeOriginal.scope=scope;
    for(auto& qualified:fixture.finiteInputs)qualified.scope=scope;for(auto& qualified:fixture.sparseInputs)qualified.scope=scope;
    fixture.privateInputs[0].scope=scope;fixture.copies.occurrence=scope.occurrence;
    fixture.copies.owner=Lease(currentCopiesDestroyed,{fixture.original.Get()});
    assembly::Plan current;Require(assembly::Build(fixture.input,current,failure) && current.Ready() && current.Source()!=firstPlan.Source(),"Different source namespace was not separately validated");
    Check(assembly::Assemble(device,current,cache,repeated,failure));
    Require(cache.TableCount()==5 && cache.UniqueDescriptorCount()==firstPlan.DescriptorCount() && cache.HitCount()==5 && cache.MissCount()==5 &&
        repeated[0].cpuHeap.Get()==firstHeap && repeated[3].cpuHeap.Get()==highHeap,"Exact immutable tables were not shared across independently qualified sources");
    Require(std::any_of(repeated[0].leases.begin(),repeated[0].leases.end(),[&](const auto& lease){return lease.Get()==fixture.copies.owner.Get();}),
        "Cache hit borrowed the previous occurrence's copy owner");
    {
        assembly::OwnedSrvCoverageView before,after;
        Require(assembly::DescribeOwnedSrvCoverage(first[3],firstPlan.Source(),before) &&
            assembly::DescribeOwnedSrvCoverage(repeated[3],current.Source(),after) && before.owner!=after.owner &&
            before.source!=after.source && before.slots.size()==2 && after.slots.size()==2,
            "Cache hit did not issue current-occurrence SRV coverage");
        Require(after.slots[0].contributor==current.Source() && before.slots[0].contributor==firstPlan.Source(),
            "Cache storage substituted a prior source occurrence's canonical provenance");
        assembly::OwnedSrvCoverageView rejected;
        Require(!assembly::DescribeOwnedSrvCoverage(repeated[3],firstPlan.Source(),rejected),"Cache hit accepted the previous occurrence");
    }
    first.clear();firstPlan={};Require(oldCopiesDestroyed==1 && !currentCopiesDestroyed,"Cache storage retained an old source-copy receipt or dropped the current receipt");
    const auto hits=cache.HitCount(),misses=cache.MissCount();const auto unique=cache.UniqueDescriptorCount();
    Require(FAILED(assembly::Assemble(device,current,cache,repeated,failure)) && cache.HitCount()==hits && cache.MissCount()==misses &&
        repeated[0].cpuHeap.Get()==firstHeap,"Nonempty cache output was overwritten or changed counters");
    fixture.sparse[0].owner=Lease(owners);Require(assembly::Build(fixture.input,current,failure) && current.Ready(),"Changed descriptor owner plan failed");
    Require(FAILED(assembly::Assemble(device,current,cache,changed,failure,unique)) && failure==assembly::Failure::Capacity && changed.empty() &&
        cache.UniqueDescriptorCount()==unique && cache.TableCount()==5 && cache.HitCount()==hits && cache.MissCount()==misses,
        "Cache hit processing mutated counters before a later unique-owner miss exceeded capacity");
    Check(assembly::Assemble(device,current,cache,changed,failure));
    Require(changed[3].cpuHeap.Get()!=highHeap && changed[4].cpuHeap==repeated[4].cpuHeap && cache.TableCount()==6 &&
        cache.UniqueDescriptorCount()==unique+fixture.sparse[0].count,"Different strong descriptor ownership falsely hit a cached CPU heap");
    const auto changedOwnerHeap=changed[3].cpuHeap.Get();const auto previousViewHeap=changed[1].cpuHeap.Get();changed.clear();
    fixture.safeOriginal.view.BufferLocation+=256;Require(assembly::Build(fixture.input,current,failure) && current.Ready(),"Changed CBV view plan failed");
    Check(assembly::Assemble(device,current,cache,changed,failure));
    Require(changed[1].cpuHeap.Get()!=previousViewHeap && changed[3].cpuHeap.Get()==changedOwnerHeap && cache.TableCount()==7,
        "Another exact CBV address falsely hit prior descriptor storage");
    const auto previousPrivateHeap=changed[0].cpuHeap.Get();changed.clear();
    fixture.privateCbv.owner=Lease(owners,{fixture.privateBuffer.Get()});Require(assembly::Build(fixture.input,current,failure) && current.Ready(),"Changed private physical owner plan failed");
    Check(assembly::Assemble(device,current,cache,changed,failure));
    Require(changed[0].cpuHeap.Get()!=previousPrivateHeap && cache.TableCount()==8,
        "Equal private CBV GPUVA/size with another arena owner falsely hit cached storage");
    const auto previousLayoutHeap=changed[4].cpuHeap.Get();changed.clear();
    fixture.ranges[33].OffsetInDescriptorsFromTableStart=1;++fixture.sparse[1].count;++fixture.sparse[1].slots[0].offset;
    const UINT stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    fixture.sparse[1].slots[0].stamp.gpu+=stride;auto& selection=fixture.selectors.tables[1];
    selection.key.originalTableOffset=1;++selection.count;++selection.offsets[0];++selection.uses[0].offset;++fixture.selectors.selections.back().descriptorOffset;
    Require(assembly::Build(fixture.input,current,failure) && current.Ready(),"Changed original table layout plan failed");
    Check(assembly::Assemble(device,current,cache,changed,failure));
    Require(changed[4].cpuHeap.Get()!=previousLayoutHeap && changed[4].count==32765 && cache.TableCount()==9,
        "Another original offset/extent falsely hit a cached null-hole layout");
    for(const auto& table:changed)Require(!table.identity && !table.version && !table.offset,"CPU interning created bank identity or rebased original tables");
    const auto committed=cache.UniqueDescriptorCount();const auto committedHits=cache.HitCount(),committedMisses=cache.MissCount();
    current={};changed.clear();Require(FAILED(assembly::Assemble(device,current,cache,changed,failure)) && failure==assembly::Failure::NotReady &&
        cache.UniqueDescriptorCount()==committed && cache.HitCount()==committedHits && cache.MissCount()==committedMisses,"Cache reused previous readiness or changed a failed transaction");
    repeated.clear();fixture.copies={};fixture.privateCbv={};fixture.safeOriginal={};fixture.finite.clear();fixture.sparse.clear();
    fixture.original.Reset();fixture.privateBuffer.Reset();Require(currentCopiesDestroyed==1,"CPU cache retained occurrence-specific copy receipts after all current users released");
}
static assembly::Plan SparsePassPlan(Fixture& fixture,std::span<const UINT> offsets,std::atomic<UINT>& owners,UINT elements=1){
    auto& source=fixture.sparse[0];auto& selected=fixture.selectors.tables[0];
    fixture.selectors.tables.resize(1);fixture.selectors.selections.clear();selected.offsets.assign(offsets.begin(),offsets.end());
    selected.count=offsets.back()+1;selected.uses.clear();
    for(const auto offset:offsets){selected.uses.push_back({offset,1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV});
        fixture.selectors.selections.push_back({UINT(fixture.selectors.selections.size()),offset,offset,offset});}
    fixture.selectors.evaluatedSites=UINT(offsets.size());source.count=selected.count;source.slots.clear();
    source.cpuHeap=Heap(fixture.device,UINT(offsets.size()));source.owner=Lease(owners,{fixture.original.Get()});++source.identity;
    const UINT stride=fixture.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_UINT;srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Buffer.NumElements=elements;
    for(UINT n=0;n<offsets.size();++n){audit::OwnedSourceSlot slot;slot.offset=offsets[n];slot.compactIndex=UINT(offsets.size())-n-1;
        slot.type=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;slot.stamp.gpu=source.sourceGpu+UINT64(slot.offset)*stride;slot.stamp.heap=source.sourceHeap;
        slot.stamp.value[0]=2;slot.stamp.value[1]=1;slot.stamp.value[2]=101;slot.stamp.value[4]=srv.Format;slot.stamp.value[5]=srv.ViewDimension;
        slot.stamp.value[6]=srv.Shader4ComponentMapping;slot.stamp.value[8]=elements;
        fixture.device->CreateShaderResourceView(fixture.original.Get(),&srv,
            {source.cpuHeap->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(slot.compactIndex)*stride});source.slots.push_back(slot);}
    ++fixture.input.scope.source;++fixture.input.scope.occurrence;fixture.input.scope.eye^=1;fixture.input.selectorScope=fixture.input.scope;
    fixture.sparseInputs[0].scope=fixture.input.scope;
    fixture.input.shaderUses={&fixture.shaderUses[3],1};fixture.input.finite={};fixture.input.sparse={fixture.sparseInputs.data(),1};
    fixture.input.privateCbvs={};fixture.input.originalCbvs={};
    assembly::Plan plan;assembly::Failure failure;Require(assembly::Build(fixture.input,plan,failure) && plan.Ready(),"Sparse pass source was not individually certified");return plan;
}
static void CoverageTests(ID3D12Device* device){
    std::atomic<UINT> owners{};Fixture fixture(device,owners);fixture.input.originalCbvs={&fixture.safeOriginal,1};
    assembly::Plan plan;assembly::Failure failure;Require(assembly::Build(fixture.input,plan,failure) && plan.Ready(),"Coverage plan");
    std::vector<SharedBankTable> tables;Check(assembly::Assemble(device,plan,tables,failure));
    const auto& table=tables[3];const auto scope=plan.Source();const auto original=fixture.sparse[0].slots[0];
    assembly::OwnedSrvCoverageView view;
    Require(assembly::DescribeOwnedSrvCoverage(table,scope,view) && view.source==scope && view.slots.size()==2,
        "Direct sparse assembly omitted exact copied slots");
    Require(view.slots[0].offset==29614 && view.slots[1].offset==30989 && !view.slots[0].resourceCookie &&
        view.slots[0].contributor==scope && view.slots[0].originalOwner==fixture.sparse[0].owner &&
        view.slots[0].originalCpuHeap==fixture.sparse[0].cpuHeap,
        "Copied null SRV provenance or compact/original index mapping was lost");
    Require(assembly::MatchesOwnedSrvCoverage(table,scope,original.offset,original.stamp,view),"Original canonical SRV did not match its receipt");
    Require(!assembly::MatchesOwnedSrvCoverage(table,scope,7,original.stamp,view) && !view.owner && view.slots.empty(),
        "Generated null hole was certified as an original SRV");
    for(UINT field=0;field<5;++field){auto stamp=original.stamp;
        if(field==0)++stamp.gpu;else if(field==1)++stamp.heap;else if(field==2)stamp.value[2]=99;
        else if(field==3)stamp.value[4]=DXGI_FORMAT_R32_FLOAT;else stamp.value[8]=2;
        Require(!assembly::MatchesOwnedSrvCoverage(table,scope,original.offset,stamp,view) && !view.owner,"Foreign canonical descriptor accepted");
    }
    const auto reject=[&](const SharedBankTable& value,const assembly::Scope& source){
        view.owner=table.srvCoverage;
        Require(!assembly::DescribeOwnedSrvCoverage(value,source,view) && !view.owner && view.slots.empty(),"Changed table retained stale SRV coverage");
    };
    for(UINT field=0;field<9;++field){auto changed=table;
        switch(field){case 0:changed.cpuHeap=Heap(device,table.storageExtent);break;case 1:++changed.offset;break;
        case 2:--changed.count;break;case 3:++changed.parameter;break;case 4:++changed.identity;break;case 5:++changed.version;break;
        case 6:++changed.storageScope;break;case 7:--changed.storageExtent;break;case 8:changed.srvCoverage.reset();break;}
        reject(changed,scope);
    }
    for(UINT field=0;field<6;++field){auto source=scope;
        switch(field){case 0:++source.root;break;case 1:++source.list;break;case 2:++source.generation;break;
        case 3:++source.occurrence;break;case 4:++source.source;break;case 5:source.eye^=1;break;}
        reject(table,source);
    }
    for(size_t i=0;i<table.leases.size();++i){auto changed=table;changed.leases.erase(changed.leases.begin()+i);reject(changed,scope);}
    auto extended=table;extended.leases.push_back(Lease(owners));
    Require(assembly::DescribeOwnedSrvCoverage(extended,scope,view),"Additional retained owner invalidated exact table coverage");
    for(UINT index=0;index<3;++index){Require(!tables[index].srvCoverage,"Uncertified finite table got sparse coverage");reject(tables[index],scope);}
    auto transplanted=tables[4];transplanted.srvCoverage=table.srvCoverage;reject(transplanted,scope);
    // No query of the original GPU heap or mutable external source container.
    fixture.sparse[0].slots[0].stamp.value[8]=19;fixture.sparse[0].owner=Lease(owners);
    Require(assembly::MatchesOwnedSrvCoverage(table,scope,original.offset,original.stamp,view),"Receipt read later native/source metadata");
    const auto held=view.owner;tables.clear();plan={};fixture.sparse.clear();
    Require(held && view.slots[0].originalOwner && view.slots[0].originalCpuHeap && view.slots[0].stamp.value==original.stamp.value,
        "Read-only coverage view lost its immutable provenance owners");
}
static void PassTests(ID3D12Device* device){
    std::atomic<UINT> owners{},firstOwners{},secondOwners{},conflictOwners{},otherDomainOwners{},oldCopies{},currentCopies{};
    Fixture fixture(device,owners);const std::array<UINT,2> left{4,8},right{8,12};
    auto first=SparsePassPlan(fixture,left,firstOwners);const auto firstOwner=fixture.sparse[0].owner.Get();
    auto second=SparsePassPlan(fixture,right,secondOwners);
    const auto currentOwner=fixture.sparse[0].owner.Get();
    const std::array<const assembly::Plan*,2> plans{&first,&second};assembly::Failure failure;
    assembly::CpuTableCache cache,tiny;std::vector<std::vector<SharedBankTable>> output,repeated;
    Require(FAILED(assembly::AssemblePass(device,plans,tiny,output,failure,12)) && failure==assembly::Failure::Capacity && output.empty() &&
        !tiny.UniqueDescriptorCount() && !tiny.TableCount() && !tiny.HitCount() && !tiny.MissCount(),"Pass cap failure published a partial union/cache");
    assembly::Plan absent;const std::array<const assembly::Plan*,2> notReady{&first,&absent};
    Require(FAILED(assembly::AssemblePass(device,notReady,tiny,output,failure)) && failure==assembly::Failure::NotReady && output.empty() && !tiny.TableCount(),
        "Pass borrowed another source's readiness");
    Check(assembly::AssemblePass(device,plans,cache,output,failure));
    Require(output.size()==2 && output[0].size()==1 && output[1].size()==1 && output[0][0].cpuHeap==output[1][0].cpuHeap &&
        output[0][0].count==9 && output[1][0].count==13 && output[0][0].cpuHeap->GetDesc().NumDescriptors==13 &&
        first.DescriptorCount()==9 && second.DescriptorCount()==13 && first.Source()!=second.Source(),"Union changed original extents or borrowed a source namespace");
    Require(cache.UniqueDescriptorCount()==13 && cache.TableCount()==1 && cache.HitCount()==1 && cache.MissCount()==1,
        "Union accounted repeated original extents instead of one shared heap");
    Require(output[0][0].storageScope==reinterpret_cast<uintptr_t>(output[0][0].cpuHeap.Get()) &&
        output[1][0].storageScope==output[0][0].storageScope && output[0][0].storageExtent==13 && output[1][0].storageExtent==13,
        "Union storage qualification lost its real held heap or maximum extent");
    {
        assembly::OwnedSrvCoverageView leftCoverage,rightCoverage,rejected;
        Require(assembly::DescribeOwnedSrvCoverage(output[0][0],first.Source(),leftCoverage) &&
            assembly::DescribeOwnedSrvCoverage(output[1][0],second.Source(),rightCoverage) &&
            leftCoverage.slots.size()==4 && rightCoverage.slots.size()==4 && leftCoverage.owner!=rightCoverage.owner,
            "Union failed to retain occurrence-specific receipts with all contributors");
        const auto count=[&](const auto& view,UINT offset,const assembly::Scope& scope){return std::count_if(view.slots.begin(),view.slots.end(),
            [&](const auto& slot){return slot.offset==offset && slot.contributor==scope && slot.resourceCookie==101;});};
        Require(count(leftCoverage,4,first.Source())==1 && count(leftCoverage,8,first.Source())==1 &&
            count(leftCoverage,8,second.Source())==1 && count(leftCoverage,12,second.Source())==1,
            "Union coverage invented holes or discarded a canonical contributor");
        const auto original=fixture.sparse[0].slots.back();
        Require(assembly::MatchesOwnedSrvCoverage(output[0][0],first.Source(),12,original.stamp,rejected),
            "Union physical storage omitted a certified contributor beyond this source's admitted count");
        Require(!assembly::DescribeOwnedSrvCoverage(output[0][0],second.Source(),rejected),"Union heap sharing substituted another source occurrence");
        auto stale=output[0][0];stale.srvCoverage=output[1][0].srvCoverage;
        Require(!assembly::DescribeOwnedSrvCoverage(stale,first.Source(),rejected),"Union receipt was transplanted between source counts/scopes");
        stale=output[0][0];--stale.storageExtent;
        Require(!assembly::DescribeOwnedSrvCoverage(stale,first.Source(),rejected),"Truncated union reused full storage receipt");
    }
    {StereoBindingBank bank;std::array<SourceBankDraw,2> draws;
        for(UINT n=0;n<2;++n)draws[n]={fixture.signature,
            {static_cast<const uint8_t*>(fixture.serialized->GetBufferPointer()),fixture.serialized->GetBufferSize()},output[n]};
        Check(bank.CreateSourcePass(device,draws));Require(bank.DescriptorCount()==13 && bank.DrawCount()==2,
            "Visible bank duplicated the qualified union or changed source count");}
    Require(std::any_of(output[1][0].leases.begin(),output[1][0].leases.end(),[&](const auto& lease){return lease.Get()==currentOwner;}),
        "Union omitted the current source's capture owner");
    for(const auto& source:output)Require(std::any_of(source[0].leases.begin(),source[0].leases.end(),[&](const auto& lease){return lease.Get()==firstOwner;}) &&
        std::any_of(source[0].leases.begin(),source[0].leases.end(),[&](const auto& lease){return lease.Get()==currentOwner;}),
        "Union omitted a contributing physical owner from a returned source");
    // Original offsets 4/8/12 stay fixed; all other slots use the same NullSlot
    // construction as ordinary assembly. CPU descriptor contents have no read API.
    for(const auto& source:output)Require(!source[0].offset && !source[0].identity && !source[0].version,"Union rebased indices or granted bank identity");
    Check(assembly::AssemblePass(device,plans,cache,repeated,failure));
    Require(repeated[0][0].cpuHeap==output[0][0].cpuHeap && cache.HitCount()==3 && cache.MissCount()==1,"Exact pass union did not hit shared immutable storage");
    const auto hits=cache.HitCount(),misses=cache.MissCount();
    Require(FAILED(assembly::AssemblePass(device,plans,cache,repeated,failure)) && cache.HitCount()==hits && cache.MissCount()==misses,
        "Pass overwrote existing output or changed counters on failure");
    auto conflict=SparsePassPlan(fixture,right,conflictOwners,2);const std::array<const assembly::Plan*,3> conflicting{&first,&second,&conflict};
    std::vector<std::vector<SharedBankTable>> changed;
    Require(FAILED(assembly::AssemblePass(device,conflicting,cache,changed,failure,13)) && failure==assembly::Failure::Capacity && changed.empty() &&
        cache.UniqueDescriptorCount()==13 && cache.TableCount()==1 && cache.HitCount()==hits && cache.MissCount()==misses,
        "Conflicting canonical view merged or mutated cache before pass rollback");
    assembly::CpuTableCache partitioned;Check(assembly::AssemblePass(device,conflicting,partitioned,changed,failure));
    Require(partitioned.TableCount()==2 && partitioned.UniqueDescriptorCount()==26 && changed[0][0].cpuHeap==changed[1][0].cpuHeap &&
        changed[2][0].cpuHeap!=changed[0][0].cpuHeap,"Same cookie/stamp with a different full canonical view was merged");
    {
        assembly::OwnedSrvCoverageView leftCoverage,conflictCoverage;
        Require(assembly::DescribeOwnedSrvCoverage(changed[0][0],first.Source(),leftCoverage) &&
            assembly::DescribeOwnedSrvCoverage(changed[2][0],conflict.Source(),conflictCoverage),"Partitioned coverage missing");
        const auto conflictSlot=fixture.sparse[0].slots[0];
        Require(!assembly::MatchesOwnedSrvCoverage(changed[0][0],first.Source(),conflictSlot.offset,conflictSlot.stamp,conflictCoverage),
            "Conflicting view crossed union coverage partitions");
    }
    ++fixture.sparse[0].sourceHeap;auto otherDomain=SparsePassPlan(fixture,right,otherDomainOwners);
    const std::array<const assembly::Plan*,2> separate{&first,&otherDomain};assembly::CpuTableCache separated;changed.clear();
    Check(assembly::AssemblePass(device,separate,separated,changed,failure));
    Require(separated.TableCount()==2 && separated.UniqueDescriptorCount()==22 && changed[0][0].cpuHeap!=changed[1][0].cpuHeap,
        "Union crossed the original native heap domain");
    first={};second={};output.clear();repeated.clear();Require(!firstOwners && !secondOwners,"Shared union storage dropped contributing physical owners");
    Fixture finite(device,owners);finite.input.shaderUses={&finite.shaderUses[2],1};finite.input.finite={&finite.finiteInputs[2],1};
    finite.input.sparse={};finite.input.privateCbvs={};finite.input.originalCbvs={};finite.input.selectors=nullptr;
    assembly::Plan finitePlan;Require(assembly::Build(finite.input,finitePlan,failure) && finitePlan.Ready(),"No-union finite fixture failed");
    const std::array<const assembly::Plan*,2> finitePlans{&finitePlan,&finitePlan};assembly::CpuTableCache finiteCache;changed.clear();
    Check(assembly::AssemblePass(device,finitePlans,finiteCache,changed,failure));
    Require(finiteCache.UniqueDescriptorCount()==39 && finiteCache.TableCount()==1 && finiteCache.HitCount()==1 && changed[0][0].cpuHeap==changed[1][0].cpuHeap,
        "Pass without eligible sparse tables failed to use the existing exact cache");
    Require(changed[0][0].storageScope==reinterpret_cast<uintptr_t>(changed[0][0].cpuHeap.Get()) && changed[0][0].storageExtent==39 &&
        changed[1][0].storageScope==changed[0][0].storageScope && changed[1][0].storageExtent==39,
        "Exact finite storage was copied once per source instead of shared");
    const std::span<const uint8_t> finiteRoot{static_cast<const uint8_t*>(finite.serialized->GetBufferPointer()),finite.serialized->GetBufferSize()};
    const SourceBankDraw finiteDraws[]{{finite.signature,finiteRoot,changed[0]},{finite.signature,finiteRoot,changed[1]}};
    StereoBindingBank finiteBank;Check(finiteBank.CreateSourcePass(device,finiteDraws));
    Require(finiteBank.DrawCount()==2 && finiteBank.DescriptorCount()==39,"Exact finite sharing did not reach the GPU bank");
    auto otherRoot=finite.root;otherRoot.Flags=D3D12_ROOT_SIGNATURE_FLAGS(otherRoot.Flags|D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC otherDesc{};otherDesc.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;otherDesc.Desc_1_1=otherRoot;
    ComPtr<ID3DBlob> otherBlob,otherError;Check(D3D12SerializeVersionedRootSignature(&otherDesc,&otherBlob,&otherError));
    ComPtr<ID3D12RootSignature> otherSignature;Check(device->CreateRootSignature(0,otherBlob->GetBufferPointer(),otherBlob->GetBufferSize(),IID_PPV_ARGS(&otherSignature)));
    Require(otherSignature!=finite.signature,"Finite root-domain fixture reused an interned identical root");
    auto otherScope=finite.input.scope;otherScope.root=reinterpret_cast<uintptr_t>(otherSignature.Get());
    finite.input.scope=otherScope;finite.finiteInputs[2].scope=otherScope;finite.input.root=&otherRoot;
    assembly::Plan otherFinite;Require(assembly::Build(finite.input,otherFinite,failure) && otherFinite.Ready(),"Other finite root-domain plan failed");
    std::vector<SharedBankTable> otherTables;Check(assembly::Assemble(device,otherFinite,finiteCache,otherTables,failure));
    Require(otherTables[0].cpuHeap!=changed[0][0].cpuHeap && otherTables[0].storageScope!=changed[0][0].storageScope,
        "Another root borrowed the old bank storage scope");
    Fixture mixed(device,owners);mixed.input.originalCbvs={&mixed.safeOriginal,1};
    mixed.copies.owner=Lease(oldCopies,{mixed.original.Get()});assembly::Plan oldPlan,currentPlan;
    Require(assembly::Build(mixed.input,oldPlan,failure) && oldPlan.Ready(),"Mixed pass original plan failed");
    auto scope=mixed.input.scope;++scope.source;++scope.occurrence;scope.eye=0;mixed.input.scope=mixed.input.selectorScope=scope;
    for(auto& q:mixed.finiteInputs)q.scope=scope;for(auto& q:mixed.sparseInputs)q.scope=scope;mixed.privateInputs[0].scope=scope;mixed.safeOriginal.scope=scope;
    mixed.copies.occurrence=scope.occurrence;mixed.copies.owner=Lease(currentCopies,{mixed.original.Get()});
    Require(assembly::Build(mixed.input,currentPlan,failure) && currentPlan.Ready(),"Mixed pass current plan failed");
    const std::array<const assembly::Plan*,2> mixedPlans{&oldPlan,&currentPlan};assembly::CpuTableCache mixedCache;changed.clear();
    Check(assembly::AssemblePass(device,mixedPlans,mixedCache,changed,failure));
    Require(changed[0][0].cpuHeap==changed[1][0].cpuHeap &&
        std::any_of(changed[1][0].leases.begin(),changed[1][0].leases.end(),[&](const auto& lease){return lease==mixed.copies.owner;}),
        "Pass union replaced current private-CBV copy receipt");
    oldPlan={};changed[0].clear();Require(oldCopies==1 && !currentCopies,"Pass cache/current source retained a foreign copy receipt");
}
static ComPtr<ID3D12Resource> GpuBuffer(ID3D12Device* device,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width=bytes;desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&resource)));return resource;
}
static void Execute(ID3D12CommandQueue* queue,ID3D12GraphicsCommandList* list,ID3D12Fence* fence,UINT64 value){
    Check(list->Close());ID3D12CommandList* raw=list;auto flight=cvr::gpu::PrepareCommandResources(1,&raw);
    queue->ExecuteCommandLists(1,&raw);cvr::gpu::SubmitCommandResources(queue,std::move(flight));Check(queue->Signal(fence,value));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"GPU fence event unavailable");
    const auto result=fence->SetEventOnCompletion(value,event);if(FAILED(result)){CloseHandle(event);Check(result);}
    const auto waited=WaitForSingleObject(event,5000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"Private SRV fixture timed out");
}
static void PrivateSrvGpu(ID3D12Device* device){
    namespace snapshot=structured_snapshot_view;namespace multilayer=multilayer_selectors;
    // Standalone fixture owns every input and submission. These contracts are
    // not evidence for a native original shader, epoch, scheduling or consumer.
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> sourceAllocator,allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&sourceAllocator)));
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> source,list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,sourceAllocator.Get(),nullptr,IID_PPV_ARGS(&source)));
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12Fence> sourceFence,fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&sourceFence)));
    Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    std::array<std::array<uint8_t,228>,2> b4{};std::array<uint8_t,6656> bytes{};
    for(UINT version=0;version<2;++version){const float base=3412;const uint32_t mask=65535;std::memcpy(b4[version].data()+128,&base,4);std::memcpy(b4[version].data()+224,&mask,4);
        // Distinct actual B4 payloads with equal index inputs; the shader ignores
        // this padding, so output changes below are attributable only to t5.
        b4[version][0]=uint8_t(version);
        for(UINT row=0;row<16;++row){const uint32_t value=2001+version*1000+row;std::memcpy(bytes.data()+version*2048+row*128,&value,4);}
        std::memcpy(bytes.data()+6144+version*256,b4[version].data(),228);}
    for(UINT row=0;row<16;++row){const uint32_t value=1001+row;std::memcpy(bytes.data()+4096+row*128,&value,4);}
    auto upload=GpuBuffer(device,bytes.size(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void* mapped{};
    Check(upload->Map(0,nullptr,&mapped));std::memcpy(mapped,bytes.data(),bytes.size());upload->Unmap(0,nullptr);
    auto original=GpuBuffer(device,snapshot::DomainBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto dense=GpuBuffer(device,4096,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto b4Gpu=GpuBuffer(device,512,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto b4Readback=GpuBuffer(device,512,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    source->CopyBufferRegion(original.Get(),436736,upload.Get(),4096,2048);source->CopyBufferRegion(dense.Get(),0,upload.Get(),0,4096);
    source->CopyBufferRegion(b4Gpu.Get(),0,upload.Get(),6144,512);D3D12_RESOURCE_BARRIER barriers[3]{};
    for(UINT n=0;n<3;++n){barriers[n].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barriers[n].Transition={n==0?original.Get():n==1?dense.Get():b4Gpu.Get(),
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,n==0?D3D12_RESOURCE_STATES(D3D12_RESOURCE_STATE_COPY_SOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE):
        n==2?D3D12_RESOURCE_STATES(D3D12_RESOURCE_STATE_COPY_SOURCE|D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER):D3D12_RESOURCE_STATE_COPY_SOURCE};}
    source->ResourceBarrier(3,barriers);source->CopyBufferRegion(b4Readback.Get(),0,b4Gpu.Get(),0,512);
    Require(cvr::gpu::KeepCommandResources(source.Get(),{upload.Get(),original.Get(),dense.Get(),b4Gpu.Get(),b4Readback.Get(),sourceAllocator.Get()}),"Source owners missing");
    Execute(queue.Get(),source.Get(),sourceFence.Get(),1);Check(b4Readback->Map(0,nullptr,&mapped));
    for(UINT n=0;n<2;++n)Require(!std::memcmp(static_cast<const uint8_t*>(mapped)+n*256,b4[n].data(),228),"Actual GPU b4 bytes differ");b4Readback->Unmap(0,nullptr);
    std::array<D3D12_ROOT_PARAMETER1,24> parameters{};std::array<D3D12_DESCRIPTOR_RANGE1,24> ranges{};
    for(UINT n=0;n<24;++n){ranges[n]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,100+n,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
        parameters[n].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[n].DescriptorTable={1,&ranges[n]};parameters[n].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
    ranges[23]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,8,0,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
    ranges[19]={D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,4,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
    const D3D12_ROOT_SIGNATURE_DESC1 root{24,parameters.data(),0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc{};rootDesc.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;rootDesc.Desc_1_1=root;
    ComPtr<ID3DBlob> serialized,error;Check(D3D12SerializeVersionedRootSignature(&rootDesc,&serialized,&error));ComPtr<ID3D12RootSignature> signature;
    Check(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&signature)));
    const source_selectors::FamilyKey family{std::string(multilayer::RootSha256),std::string(multilayer::VertexSha256),std::string(multilayer::PixelSha256)};
    const std::array<families::ResourceUse,2> uses{{{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,D3D12_SHADER_VISIBILITY_PIXEL,0,5,1,0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_PIXEL,0,4,1,228}}};
    std::array<snapshot::Input,2> inputs;
    for(UINT n=0;n<2;++n){auto& input=inputs[n];input.scope={reinterpret_cast<uintptr_t>(signature.Get()),reinterpret_cast<uintptr_t>(source.Get()),7,n+1,n};
        input.privateB4Scope=input.scope;input.originalView.key={family.rootSha256,5,5,0,23,5};input.originalView.resourceWidth=snapshot::DomainBytes;
        input.originalView.numElements=8192;input.originalView.structureByteStride=128;input.originalView.dimension=1;input.originalView.componentMapping=5768;input.originalView.explicitDescription=true;
        input.privateB4Bytes=b4[n];input.privateB4GpuMatchVerified=input.allRecordPathsBounded=true;std::string message;
        Require(multilayer::PlanRead(family,{{family.rootSha256,5,4,0,19,0},b4[n]},input.originalView,input.readPlan,message),"Fixture record paths not bounded");
        input.dense={dense,dense,436736,n*2048,2048};input.copy={dense,input.scope.list,7,n+1,436736,n*2048,2048};input.completion={input.scope,queue,sourceFence,1,true,true};}
    snapshot::Plan overlayPlan;snapshot::Failure snapshotFailure;Require(snapshot::BuildPlan(inputs,overlayPlan,snapshotFailure),"Private overlay plan failed");
    snapshot::Overlay overlay;Check(overlay.Create(device,overlayPlan));const snapshot::DestinationRecording recording{reinterpret_cast<uintptr_t>(list.Get()),11,allocator,queue,true};
    Require(overlay.Record(list.Get(),recording,snapshotFailure),"Completed private overlay failed to record");std::array<snapshot::PrivateView,2> privateViews;
    for(UINT n=0;n<2;++n)Require(overlay.GetView(n,privateViews[n]) && snapshot::ValidatePrivateViewForRecording(privateViews[n],list.Get(),recording),"Private recording proof failed");
    audit::OwnedSourceTable finite;finite.parameter=23;finite.count=8;finite.cpuHeap=Heap(device,8);finite.owner=original;
    finite.identity=91;finite.version=12;finite.sourceGpu=0x100000;finite.sourceHeap=1313;
    const UINT stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format=DXGI_FORMAT_UNKNOWN;srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;srv.Shader4ComponentMapping=5768;srv.Buffer.NumElements=8192;srv.Buffer.StructureByteStride=128;
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};nullSrv.Format=DXGI_FORMAT_R32_UINT;nullSrv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;nullSrv.Shader4ComponentMapping=5768;nullSrv.Buffer.NumElements=1;
    for(UINT n=0;n<8;++n)device->CreateShaderResourceView(n==5?original.Get():nullptr,n==5?&srv:&nullSrv,{finite.cpuHeap->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(n)*stride});
    audit::SrvBinding binding;binding.resource=original;binding.view=srv;binding.stamp.gpu=finite.sourceGpu+5*stride;binding.stamp.heap=finite.sourceHeap;
    binding.description[0]=2;binding.description[1]=1;binding.description[2]=777;binding.description[5]=1;binding.description[6]=5768;binding.description[8]=8192;binding.description[9]=128;
    binding.stamp.value=binding.description;const auto savedBinding=binding;
    audit::OwnedSourceTable b4Finite;b4Finite.parameter=19;b4Finite.count=1;b4Finite.cpuHeap=Heap(device,1);b4Finite.owner=b4Gpu;
    b4Finite.identity=92;b4Finite.version=12;b4Finite.sourceGpu=0x200000;b4Finite.sourceHeap=1323;
    std::array<source_cbv_snapshot::PrivateCbv,2> b4Views;std::array<source_cbv_snapshot::PreparedCopies,2> b4Copies;
    for(UINT n=0;n<2;++n){auto& value=b4Views[n];value.parameter=19;value.reflectedBytes=228;value.copiedBytes=228;value.paddedBytes=256;
        value.privateOffset=n*256;value.resource=b4Gpu;value.owner=b4Gpu;value.view={b4Gpu->GetGPUVirtualAddress()+n*256,256};
        auto& receipt=b4Copies[n];receipt.owner=b4Gpu;receipt.list=inputs[n].scope.list;receipt.generation=7;receipt.occurrence=n+1;receipt.copiedBytes=228;receipt.count=1;}
    device->CreateConstantBufferView(&b4Views[0].view,b4Finite.cpuHeap->GetCPUDescriptorHandleForHeapStart());
    assembly::Failure failure;assembly::CpuTableCache cache;std::array<std::vector<SharedBankTable>,4> tables;
    for(UINT n=0;n<4;++n){const UINT version=n==2?1:0;const auto& scoped=inputs[version].scope;
        const assembly::Scope scope{scoped.root,scoped.list,scoped.generation,scoped.occurrence,100+version,scoped.eye};
        const std::array<assembly::FiniteTable,2> qualified{{{scope,&finite},{scope,&b4Finite}}};
        const assembly::PrivateCbv qualifiedB4{scope,&b4Views[version],&b4Copies[version],true};
        assembly::Input input;input.scope=scope;input.root=&root;input.shaderUses=uses;input.finite=qualified;input.family=family;input.privateCbvs={&qualifiedB4,1};
        assembly::PrivateSrv replacement;replacement.scope=scope;replacement.parameter=23;replacement.offset=5;replacement.value=&privateViews[version];
        replacement.original=binding;replacement.originalOwner=finite.owner;replacement.originalIdentity=finite.identity;replacement.originalVersion=finite.version;
        replacement.originalGpu=finite.sourceGpu;replacement.originalHeap=finite.sourceHeap;replacement.originalCount=finite.count;replacement.resourceCookie=777;replacement.originalSlotCertified=true;
        replacement.privateB4=&b4Views[version];replacement.privateB4Copies=&b4Copies[version];replacement.privateB4Bytes=b4[version];replacement.privateB4BytesCertified=true;
        if(n==1 || n==2)input.privateSrvs={&replacement,1};assembly::Plan plan;
        Require(assembly::Build(input,plan,failure) && plan.Ready() && plan.DescriptorCount()==9,"Scoped t5 assembly rejected");
        // Fresh, uncached final control reads the ORIGINAL finite CPU heap after
        // both replacements; a cached pre-replacement control could hide a write.
        Check(n==3?assembly::Assemble(device,plan,tables[n],failure):assembly::Assemble(device,plan,cache,tables[n],failure));
        assembly::OwnedSrvCoverageView noCoverage;
        Require(!tables[n][1].srvCoverage && !assembly::DescribeOwnedSrvCoverage(tables[n][1],scope,noCoverage),
            "Finite or private-overridden T5 inherited unrelated sparse SRV coverage");
        Require(tables[n].size()==2 && tables[n][1].parameter==23 && tables[n][1].count==8 && !tables[n][1].offset &&
            tables[n][1].storageScope==reinterpret_cast<uintptr_t>(tables[n][1].cpuHeap.Get()) && tables[n][1].storageExtent==8,
            "Finite private t5 override rebased indices or granted union storage");
        if(n==1){const auto valid=replacement;for(UINT which=0;which<15;++which){replacement=valid;snapshot::PrivateView torn=privateViews[version];
                switch(which){case 0:++replacement.scope.source;break;case 1:replacement.originalSlotCertified=false;break;case 2:++replacement.resourceCookie;break;
                    case 3:++replacement.original.stamp.gpu;break;case 4:++replacement.original.description[9];break;case 5:++replacement.originalIdentity;break;
                    case 6:replacement.offset=4;break;case 7:replacement.originalOwner.Reset();break;case 8:++torn.scope.occurrence;replacement.value=&torn;break;
                    case 9:++torn.srv.Buffer.FirstElement;replacement.value=&torn;break;case 10:replacement.privateB4Bytes=b4[1-version];break;
                    case 11:replacement.privateB4=&b4Views[1-version];break;case 12:replacement.privateB4Copies=&b4Copies[1-version];break;
                    case 13:replacement.privateB4BytesCertified=false;break;default:torn.recordCount=33;replacement.value=&torn;break;}
                assembly::Plan rejected;Require(!assembly::Build(input,rejected,failure) && !rejected.Ready() && !rejected.TableCount(),"Invalid private t5 receipt acquired a bank plan");}
            replacement=valid;const auto validFamily=input.family;input.family.pixelSha256="foreign";assembly::Plan rejected;
            Require(!assembly::Build(input,rejected,failure),"Foreign shader family acquired private t5 access");input.family=validFamily;
            Require(std::any_of(tables[n][1].leases.begin(),tables[n][1].leases.end(),[&](const auto& lease){return lease==privateViews[version].owner;}) &&
                std::any_of(tables[n][1].leases.begin(),tables[n][1].leases.end(),[&](const auto& lease){return lease==finite.owner;}) &&
                std::any_of(tables[n][1].leases.begin(),tables[n][1].leases.end(),[&](const auto& lease){return lease==b4Copies[version].owner;}),"Original/private/B4 copy owners omitted");}}
    Require(tables[0][1].cpuHeap!=tables[1][1].cpuHeap && tables[1][1].cpuHeap!=tables[2][1].cpuHeap && cache.TableCount()==5,
        "Different private SRV versions falsely shared immutable descriptor storage");
    Require(binding.description==savedBinding.description && binding.stamp.value==savedBinding.stamp.value && binding.stamp.gpu==savedBinding.stamp.gpu &&
        binding.stamp.heap==savedBinding.stamp.heap && finite.count==8,"Private override modified original canonical inputs");
    StereoBindingBank bank;std::array<SourceBankDraw,4> draws;
    for(UINT n=0;n<4;++n)draws[n]={signature,{static_cast<const uint8_t*>(serialized->GetBufferPointer()),serialized->GetBufferSize()},tables[n]};Check(bank.CreateSourcePass(device,draws));
    constexpr char shader[]=R"(
struct Rec { uint4 v[8]; }; StructuredBuffer<Rec> records : register(t5);
cbuffer Material : register(b4) { float4 ignored[8]; float baseRecord; float3 ignoredBase; float4 ignoredTail[5]; uint mask; };
float4 VS(uint id:SV_VertexID):SV_Position { float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1); }
uint PS(float4 p:SV_Position):SV_Target { return records[(uint)baseRecord+(uint)p.x].v[0].x; }
)";
    ComPtr<ID3DBlob> vs,ps;Check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&error));
    Check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&error));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};psoDesc.pRootSignature=signature.Get();psoDesc.VS={vs->GetBufferPointer(),vs->GetBufferSize()};psoDesc.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    psoDesc.SampleMask=UINT_MAX;psoDesc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;psoDesc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;psoDesc.RasterizerState.DepthClipEnable=TRUE;
    for(auto& blend:psoDesc.BlendState.RenderTarget){blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;
        blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;}
    psoDesc.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;psoDesc.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    psoDesc.DepthStencilState.StencilReadMask=psoDesc.DepthStencilState.StencilWriteMask=D3D12_DEFAULT_STENCIL_READ_MASK;
    psoDesc.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};psoDesc.DepthStencilState.BackFace=psoDesc.DepthStencilState.FrontFace;
    psoDesc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;psoDesc.NumRenderTargets=1;psoDesc.RTVFormats[0]=DXGI_FORMAT_R32_UINT;psoDesc.SampleDesc.Count=1;
    ComPtr<ID3D12PipelineState> pso;Check(device->CreateGraphicsPipelineState(&psoDesc,IID_PPV_ARGS(&pso)));
    D3D12_RESOURCE_DESC targetDesc{};targetDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;targetDesc.Width=16;targetDesc.Height=4;
    targetDesc.DepthOrArraySize=targetDesc.MipLevels=targetDesc.SampleDesc.Count=1;targetDesc.Format=DXGI_FORMAT_R32_UINT;targetDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> target;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&targetDesc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target)));
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};ComPtr<ID3D12DescriptorHeap> rtv;Check(device->CreateDescriptorHeap(&rtvDesc,IID_PPV_ARGS(&rtv)));
    const auto handle=rtv->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(target.Get(),nullptr,handle);const float zero[4]{};
    list->ClearRenderTargetView(handle,zero,0,nullptr);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);list->SetPipelineState(pso.Get());list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for(UINT n=0;n<4;++n){D3D12_VIEWPORT viewport{0,float(n),16,1,0,1};D3D12_RECT scissor{0,LONG(n),16,LONG(n+1)};
        list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);Require(bank.BindSource(list.Get(),n),"Private/original bank bind failed");list->DrawInstanced(3,1,0,0);}
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};UINT rows{};UINT64 rowBytes{},total{};device->GetCopyableFootprints(&targetDesc,0,1,0,&layout,&rows,&rowBytes,&total);
    auto readback=GpuBuffer(device,total,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={target.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=target.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=layout;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    Require(cvr::gpu::KeepCommandResources(list.Get(),{pso.Get(),rtv.Get(),target.Get(),readback.Get()}),"Consumer owners missing");
    Require(overlay.ValidateDestinationSubmission(recording,queue.Get()),"Private copy submission contract rejected");Execute(queue.Get(),list.Get(),fence.Get(),1);Check(readback->Map(0,nullptr,&mapped));
    const UINT values[]{1001,2001,3001,1001};for(UINT n=0;n<4;++n){const auto* output=reinterpret_cast<const UINT*>(static_cast<const uint8_t*>(mapped)+layout.Offset+n*layout.Footprint.RowPitch);
        for(UINT x=0;x<16;++x)Require(output[x]==values[n]+x,"Private-only t5 override did not change output or modified original slot");}readback->Unmap(0,nullptr);cvr::gpu::CollectCommandResources();
}
static void GuardedTests(ID3D12Device* device){
    namespace ml=multilayer_selectors;
    // These owned CPU-descriptor fixtures deliberately do not read GPU payloads.
    // Native recording/submission certification remains the caller's contract.
    std::array<D3D12_ROOT_PARAMETER1,36> parameters{};std::array<D3D12_DESCRIPTOR_RANGE1,36> ranges{};
    for(UINT p=0;p<36;++p){ranges[p]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,100+p,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
        parameters[p].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[p].DescriptorTable={1,&ranges[p]};
        parameters[p].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
    ranges[19]={D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,4,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
    ranges[23]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,8,0,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
    ranges[31]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,UINT_MAX,0,1,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
    D3D12_ROOT_SIGNATURE_DESC1 root{36,parameters.data(),0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC version{};version.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;version.Desc_1_1=root;
    ComPtr<ID3DBlob> serialized,error;Check(D3D12SerializeVersionedRootSignature(&version,&serialized,&error));ComPtr<ID3D12RootSignature> signature;
    Check(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&signature)));
    const assembly::Scope scope{reinterpret_cast<uintptr_t>(signature.Get()),11,12,13,14,0};
    std::vector<families::ResourceUse> uses{{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,5,0,4,1,228},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,1,0,32768,0}};
    const source_selectors::FamilyKey regular{std::string(64,'a'),std::string(64,'b'),std::string(64,'c')};
    std::array<uint8_t,228> expected{};const UINT indices[]{101,102};std::memcpy(expected.data(),indices,sizeof(indices));
    auto b4=GpuBuffer(device,256,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_GENERIC_READ);
    source_cbv_snapshot::PrivateCbv cbv;cbv.parameter=19;cbv.reflectedBytes=cbv.copiedBytes=228;cbv.paddedBytes=256;
    cbv.resource=b4;cbv.owner=b4;cbv.view={b4->GetGPUVirtualAddress(),256};
    source_cbv_snapshot::PreparedCopies copies{b4,scope.list,scope.generation,scope.occurrence,1,228};
    // Fill named fields too, so this fixture does not depend on aggregate order.
    copies.count=1;copies.copiedBytes=228;
    const UINT stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    audit::OwnedSourceTable finite;finite.parameter=19;finite.count=1;finite.cpuHeap=Heap(device,1);finite.owner=b4;
    finite.identity=1;finite.version=1;finite.sourceGpu=0x100000;finite.sourceHeap=100;
    device->CreateConstantBufferView(&cbv.view,finite.cpuHeap->GetCPUDescriptorHandleForHeapStart());
    audit::OwnedSourceSlots sparse;sparse.parameter=31;sparse.count=103;sparse.cpuHeap=Heap(device,2);sparse.owner=b4;
    sparse.identity=2;sparse.version=1;sparse.sourceGpu=0x200000;sparse.sourceHeap=200;
    D3D12_SHADER_RESOURCE_VIEW_DESC nullView{};nullView.Format=DXGI_FORMAT_R32_UINT;nullView.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
    nullView.Shader4ComponentMapping=5768;nullView.Buffer.NumElements=1;
    for(UINT i=0;i<2;++i){audit::OwnedSourceSlot slot;slot.offset=indices[i];slot.compactIndex=i;slot.type=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        slot.stamp.gpu=sparse.sourceGpu+UINT64(slot.offset)*stride;slot.stamp.heap=sparse.sourceHeap;
        slot.stamp.value[0]=2;slot.stamp.value[4]=DXGI_FORMAT_R32_UINT;slot.stamp.value[5]=1;slot.stamp.value[6]=5768;slot.stamp.value[8]=1;
        device->CreateShaderResourceView(nullptr,&nullView,{sparse.cpuHeap->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(i)*stride});sparse.slots.push_back(slot);}
    std::vector<assembly::FiniteTable> finiteInputs{{scope,&finite}};
    const assembly::SparseTable sparseInput{scope,&sparse};assembly::PrivateCbv privateInput{scope,&cbv,&copies,true};
    source_selectors::CbvKey key{regular.rootSha256,5,4,0,19,0};source_selectors::FamilyPlan recipe;
    recipe.key=regular;recipe.number=1;recipe.supported=true;recipe.cbvs.push_back({key,228,8,{0,4}});
    const source_selectors::TableKey table{regular.rootSha256,5,31,1,0,0,32768};
    for(UINT i=0;i<2;++i)recipe.sites.push_back({regular.pixelSha256,"%"+std::to_string(10+i),"%"+std::to_string(20+i),i+1,i*4,key,table});
    assembly::PredictedCbv prediction{key,expected};assembly::GuardedInput input;input.recipe=&recipe;input.cbvs={&prediction,1};
    auto& src=input.source;src.scope=src.selectorScope=scope;src.root=&root;src.shaderUses=uses;src.family=regular;
    src.finite=finiteInputs;src.sparse={&sparseInput,1};src.privateCbvs={&privateInput,1};
    assembly::GuardedPlan plan;assembly::Failure failure;
    Require(assembly::BuildGuarded(input,plan,failure) && plan.Prepared() && plan.CheckCount()==1,"Predicted CBV did not prepare guarded tables");
    assembly::Plan forbidden;Require(!assembly::Build(src,forbidden,failure),"Unverified predictions became an ordinary ready plan");
    for(UINT mutation=0;mutation<6;++mutation){auto bad=input;auto altered=prediction;auto alteredBytes=expected;auto privateBad=privateInput;
        if(mutation==0)bad.source.dynamicCoverageCertified=true;
        if(mutation==1)bad.cbvs={};
        if(mutation==2){altered.expected={expected.data(),227};bad.cbvs={&altered,1};}
        if(mutation==3){privateBad.finalized=false;bad.source.privateCbvs={&privateBad,1};}
        if(mutation==4){altered.key.shaderRegister=5;bad.cbvs={&altered,1};}
        if(mutation==5){alteredBytes[0]=103;altered.expected=alteredBytes;bad.cbvs={&altered,1};}
        assembly::GuardedPlan rejected;Require(!assembly::BuildGuarded(bad,rejected,failure) && !rejected.Prepared(),"Torn prediction acquired guarded tables");
    }
    assembly::CpuTableCache cache;std::vector<assembly::GuardedTables> tables;const assembly::GuardedPlan* pointer=&plan;
    Check(assembly::AssembleGuardedPass(device,{&pointer,1},cache,tables,failure));
    Require(tables.size()==1 && tables[0].source==scope && tables[0].checks.size()==1 && tables[0].checks[0].resource==b4 &&
        tables[0].checks[0].expected==std::vector<uint8_t>(expected.begin(),expected.end()),"Guarded tables lost their required GPU comparison");

    const source_selectors::FamilyKey multilayer{std::string(ml::RootSha256),std::string(ml::VertexSha256),std::string(ml::PixelSha256)};
    recipe={};recipe.key=multilayer;recipe.number=1;recipe.unsupportedReason="t5";recipe.unresolvedInputs={"t5"};
    prediction.key.rootSha256=multilayer.rootSha256;src.family=multilayer;expected={};const float first=3412;const UINT mask=1;
    std::memcpy(expected.data()+128,&first,4);std::memcpy(expected.data()+224,&mask,4);
    std::array<uint8_t,128> records{};const UINT raw=101,packed=101|(102u<<16);
    std::memcpy(records.data()+100,&raw,4);std::memcpy(records.data()+104,&packed,4);std::memcpy(records.data()+108,&packed,4);
    auto original=GpuBuffer(device,1048576,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto captured=GpuBuffer(device,1048704,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_GENERIC_READ);
    audit::OwnedSourceTable t5Table;t5Table.parameter=23;t5Table.count=8;t5Table.cpuHeap=Heap(device,8);t5Table.owner=original;
    t5Table.identity=3;t5Table.version=1;t5Table.sourceGpu=0x300000;t5Table.sourceHeap=300;
    auto view=nullView;view.Format=DXGI_FORMAT_UNKNOWN;view.Buffer.NumElements=8192;view.Buffer.StructureByteStride=128;
    for(UINT i=0;i<8;++i)device->CreateShaderResourceView(i==5?original.Get():nullptr,i==5?&view:&nullView,
        {t5Table.cpuHeap->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(i)*stride});
    finiteInputs.push_back({scope,&t5Table});src.finite=finiteInputs;
    uses.push_back({D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,0,5,1,0});src.shaderUses=uses;
    assembly::PredictedT5 t5;t5.original.scope=scope;t5.original.parameter=23;t5.original.offset=5;t5.original.original.resource=original;
    t5.original.original.view=view;t5.original.original.description[0]=2;t5.original.original.description[1]=1;t5.original.original.description[2]=777;
    t5.original.original.description[5]=1;t5.original.original.description[6]=5768;t5.original.original.description[8]=8192;t5.original.original.description[9]=128;
    t5.original.original.stamp.value=t5.original.original.description;t5.original.original.stamp.heap=t5Table.sourceHeap;
    t5.original.original.stamp.gpu=t5Table.sourceGpu+5*stride;t5.original.originalOwner=t5Table.owner;
    t5.original.originalIdentity=3;t5.original.originalVersion=1;t5.original.originalCount=8;t5.original.originalGpu=t5Table.sourceGpu;
    t5.original.originalHeap=t5Table.sourceHeap;t5.original.resourceCookie=777;t5.original.originalSlotCertified=true;
    t5.copy={captured,captured,436736,436864,128};t5.receipt={captured,11,12,13,436736,436864,128};
    t5.view=view;t5.view.Buffer.FirstElement=1;t5.expected=records;t5.recordedViewCertified=true;input.t5=&t5;
    Require(assembly::BuildGuarded(input,plan,failure) && plan.Prepared() && plan.CheckCount()==2,"Pending native-position T5 view was not qualified");
    for(UINT mutation=0;mutation<7;++mutation){auto bad=t5;
        if(mutation==0)bad.recordedViewCertified=false;if(mutation==1)bad.view.Buffer.FirstElement++;
        if(mutation==2)bad.receipt.generation++;if(mutation==3)bad.original.originalSlotCertified=false;
        if(mutation==4)bad.original.privateB4BytesCertified=true;if(mutation==5)bad.copy.resource=original;
        if(mutation==6)bad.expected={records.data(),127};auto changed=input;changed.t5=&bad;assembly::GuardedPlan rejected;
        Require(!assembly::BuildGuarded(changed,rejected,failure) && !rejected.Prepared(),"Torn pending T5 view acquired guarded tables");}
    tables.clear();Check(assembly::AssembleGuardedPass(device,{&pointer,1},cache,tables,failure));
    Require(tables[0].checks.size()==2 && tables[0].checks[1].resource==captured && tables[0].checks[1].offset==436864 &&
        tables[0].checks[1].expected==std::vector<uint8_t>(records.begin(),records.end()),"Pending T5 GPU check was lost during assembly");
    Require(tables[0].t5 && tables[0].t5->resource==captured && tables[0].t5->srv.Buffer.FirstElement==1 &&
        tables[0].t5->offset==436864 && tables[0].t5->bytes==128,"Paired T5 binding depends on a later completed overlay");
}
int main(int argc,char** argv)try{
    Require(argc==1 || (argc==2 && !std::strcmp(argv[1],"--private-srv-gpu")),"Expected optional --private-srv-gpu");
    // Default mode constructs CPU descriptors only. Optional GPU mode is an
    // isolated owned fixture, never a native scheduling/suppression certificate.
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> adapter;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
    CacheTests(device.Get());CoverageTests(device.Get());
    PassTests(device.Get());
    GuardedTests(device.Get());
    std::atomic<UINT> destroyed{};Fixture fixture(device.Get(),destroyed);Negatives(fixture);
    assembly::Plan plan;assembly::Failure failure;std::vector<SharedBankTable> tables;
    Require(assembly::Build(fixture.input,plan,failure) && !plan.Ready() && plan.MissingCbvs().size()==1 &&
        plan.MissingCbvs()[0].parameter==2 && plan.MissingCbvs()[0].reflectedBytes==112,"Unavailable b5 UPLOAD CBV was declared ready from finite table ownership");
    Require(FAILED(assembly::Assemble(device.Get(),plan,tables,failure)) && failure==assembly::Failure::NotReady && tables.empty(),"Nonready CBV plan became bank input");
    fixture.input.originalCbvs={&fixture.safeOriginal,1};fixture.safeOriginal.readEpochCertified=false;Reject(fixture,assembly::Failure::CbvOwner);
    fixture.safeOriginal.readEpochCertified=true;fixture.privateInputs[0].finalized=false;
    Require(assembly::Build(fixture.input,plan,failure) && !plan.Ready() && plan.MissingCbvs().empty(),"Unfinalized private copies were ready");
    fixture.privateInputs[0].finalized=true;Require(assembly::Build(fixture.input,plan,failure) && plan.Ready(),"Complete qualified ownership was rejected");
    Require(plan.TableCount()==5 && plan.DescriptorCount()==4+2+39+30990+32764 && plan.Source()==fixture.input.scope,"Original table extents or occurrence were compacted");
    Check(assembly::Assemble(device.Get(),plan,tables,failure));Require(tables.size()==5,"Original table assembly omitted a used table");
    const std::array<UINT,5> parameters{0,2,26,31,33};
    for(UINT n=0;n<tables.size();++n)Require(tables[n].parameter==parameters[n] && !tables[n].offset && !tables[n].identity && !tables[n].version &&
        tables[n].cpuHeap->GetDesc().NumDescriptors==tables[n].count && !tables[n].leases.empty(),"Bank table was rebased, compacted or deduplicated across scopes");
    Require(std::any_of(tables[0].leases.begin(),tables[0].leases.end(),[&](const auto& lease){return lease.Get()==fixture.copies.owner.Get();}),
        "Private arena owner replaced the source allocation/copy receipt");
    const auto before=tables[0].cpuHeap.Get();Require(FAILED(assembly::Assemble(device.Get(),plan,tables,failure)) && tables[0].cpuHeap.Get()==before,
        "Assembly overwrote existing owned output");
    {
        StereoBindingBank bank;const SourceBankDraw draw{fixture.signature,
            {static_cast<const uint8_t*>(fixture.serialized->GetBufferPointer()),fixture.serialized->GetBufferSize()},tables};
        Check(bank.CreateSourcePass(device.Get(),{&draw,1}));Require(bank.DrawCount()==1 && bank.DescriptorCount()==plan.DescriptorCount(),"Original root bank rejected the assembled descriptor domains");
        plan={};tables.clear();fixture.finite.clear();fixture.sparse.clear();fixture.privateCbv={};fixture.copies={};fixture.safeOriginal={};
        fixture.original.Reset();fixture.privateBuffer.Reset();Require(!destroyed,"Bank omitted finite/sparse/private/original-safe or copy owners");
    }
    Require(destroyed==8,"Assembly/bank owner retention leaked or omitted a receipt");
    if(argc==2)PrivateSrvGpu(device.Get());
    ComPtr<ID3D12InfoQueue> info;Check(device.As(&info));
    for(UINT64 n=0;n<info->GetNumStoredMessages();++n){SIZE_T bytes{};Check(info->GetMessage(n,nullptr,&bytes));std::vector<char> storage(bytes);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());Check(info->GetMessage(n,message,&bytes));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("D3D12 assembly validation error");}}
    std::puts(argc==2?"PASS CPU assembly and standalone private-only t5 output changes with original slot unchanged":"PASS original-offset finite/sparse CPU assembly, qualified storage and complete owner lifetime; no GPU execution");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
