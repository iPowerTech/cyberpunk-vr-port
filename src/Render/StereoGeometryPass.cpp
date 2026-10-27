#include "Render/StereoGeometryPass.hpp"
#include "Render/CommandResources.hpp"
#include "Render/RenderProbeScope.hpp"
#include <cmath>
#include <algorithm>
#include <map>
#include <set>

namespace cvr::stereo {
using Microsoft::WRL::ComPtr;
struct StereoGeometryPass::Storage final: IUnknown {
    struct Admission {
        bool needed{};std::shared_ptr<const families::Family> family;
        GeometrySharedReadContract contract;
        std::vector<SharedBankTable> shared;std::vector<GeometrySharedResourceRead> resources;
        std::vector<BankPair> pairs;std::vector<std::vector<uint8_t>> constants;
        std::array<source_selectors::Result,2> selectors;std::array<bool,2> hasSelectors{};
    };
    struct Draw {ComPtr<ID3D12PipelineState> pipeline;scene_state::DrawState state;D3D12_VIEWPORT viewport{};D3D12_RECT scissor{};UINT mask{};Admission admission;};
    std::atomic<ULONG> refs{1};ComPtr<ID3D12Device> device;
    StereoBindingBank bank;StereoGeometryCommands geometry;std::vector<Draw> draws;
    GeometryPassTargets targets;ComPtr<ID3D12DescriptorHeap> rtvs,dsv;UINT rtvStride{};bool recorded{};
    bool sourceOnly{};UINT sourceNext{};ID3D12GraphicsCommandList* sourceRecording{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
};
StereoGeometryPass::StereoGeometryPass()=default;
StereoGeometryPass::~StereoGeometryPass()=default;
namespace {
bool SameDevice(ID3D12Device* device,ID3D12DeviceChild* child) {
    if(!child)return false;ComPtr<ID3D12Device> actual;return SUCCEEDED(child->GetDevice(IID_PPV_ARGS(&actual))) && actual.Get()==device;
}
UINT StateDifferences(const scene_state::DrawState& a,const scene_state::DrawState& b) {
    return (!a.original?1u:0u) | (a.original!=b.original?2u:0u) | (!a.topology || a.topology!=b.topology?4u:0u) |
        (a.stencil!=b.stencil?8u:0u) | (a.blendFactor!=b.blendFactor?16u:0u) | (!a.rate.known || !b.rate.known?32u:0u) |
        (a.rate.image || b.rate.image || a.rateImage || b.rateImage?64u:0u) | (a.rate.value!=b.rate.value || a.rate.combiners!=b.rate.combiners?128u:0u);
}
bool Target(ID3D12Device* device,ID3D12Resource* resource,DXGI_FORMAT format,bool depth,UINT64& width,UINT& height) {
    if(!SameDevice(device,resource))return false;const auto d=resource->GetDesc();
    const bool compatible=d.Format==format || (depth && ((d.Format==DXGI_FORMAT_R32G8X24_TYPELESS && format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT) ||
        (d.Format==DXGI_FORMAT_R24G8_TYPELESS && format==DXGI_FORMAT_D24_UNORM_S8_UINT) ||
        (d.Format==DXGI_FORMAT_R32_TYPELESS && format==DXGI_FORMAT_D32_FLOAT) || (d.Format==DXGI_FORMAT_R16_TYPELESS && format==DXGI_FORMAT_D16_UNORM)));
    if(!compatible || d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.DepthOrArraySize!=2 || d.MipLevels!=1 ||
        d.SampleDesc.Count!=1 || d.SampleDesc.Quality || !(d.Flags&(depth?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL:D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)))return false;
    if(width && (width!=d.Width || height!=d.Height))return false;width=d.Width;height=d.Height;return width && height;
}
bool View(const native_probe::DrawRecord& draw,UINT64 width,UINT height) {
    const auto& v=draw.viewport;const auto& s=draw.scissor;
    return std::isfinite(v.TopLeftX) && std::isfinite(v.TopLeftY) && std::isfinite(v.Width) && std::isfinite(v.Height) &&
        v.TopLeftX>=0 && v.TopLeftY>=0 && v.Width>0 && v.Height>0 && double(v.TopLeftX)+v.Width<=width && double(v.TopLeftY)+v.Height<=height &&
        v.MinDepth>=0 && v.MaxDepth<=1 && v.MinDepth<=v.MaxDepth && s.left>=0 && s.top>=0 && s.right>s.left && s.bottom>s.top &&
        UINT64(s.right)<=width && UINT(s.bottom)<=height;
}
struct Location {UINT parameter{},offset{};const D3D12_DESCRIPTOR_RANGE1* range{};};
bool Visibility(D3D12_SHADER_VISIBILITY root,UINT use){return root==D3D12_SHADER_VISIBILITY_ALL || UINT(root)==use;}
bool Locate(const D3D12_ROOT_SIGNATURE_DESC1& root,UINT original,const families::ResourceUse& use,Location& output){
    bool found{};
    for(UINT p=0;p<original;++p){const auto& parameter=root.pParameters[p];
        if(parameter.ParameterType!=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE || !Visibility(parameter.ShaderVisibility,use.visibility))continue;
        uint64_t next{};
        for(UINT n=0;n<parameter.DescriptorTable.NumDescriptorRanges;++n){const auto& range=parameter.DescriptorTable.pDescriptorRanges[n];
            const uint64_t offset=range.OffsetInDescriptorsFromTableStart==UINT_MAX?next:range.OffsetInDescriptorsFromTableStart;
            next=range.NumDescriptors==UINT_MAX?uint64_t(UINT_MAX)+1:offset+range.NumDescriptors;
            if(UINT(range.RangeType)!=use.type || range.RegisterSpace!=use.space || use.shaderRegister<range.BaseShaderRegister)continue;
            const uint64_t relative=uint64_t(use.shaderRegister)-range.BaseShaderRegister;
            if(relative>=range.NumDescriptors || use.count>uint64_t(range.NumDescriptors)-relative)continue;
            if(found || offset+relative>UINT_MAX)return false;found=true;output={p,UINT(offset+relative),&range};
        }
    }return found;
}
bool StaticSampler(const D3D12_ROOT_SIGNATURE_DESC1& root,const families::ResourceUse& use){
    if(use.type!=D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER || !use.count || use.count>2048 || uint64_t(use.shaderRegister)+use.count>uint64_t(UINT_MAX)+1)return false;
    for(UINT n=0;n<use.count;++n){UINT matches{};for(UINT s=0;s<root.NumStaticSamplers;++s){const auto& sampler=root.pStaticSamplers[s];
            matches+=sampler.RegisterSpace==use.space && sampler.ShaderRegister==use.shaderRegister+n && Visibility(sampler.ShaderVisibility,use.visibility);}
        if(matches!=1)return false;
    }return true;
}
bool Scope(const source_binding_assembly::Scope& scope,const families::Family& family,const geometry::DrawResources& geometry){
    return scope.root==reinterpret_cast<uintptr_t>(family.sourceRoot.Get()) && scope.root==geometry.draw.root &&
        scope.list && scope.generation && scope.occurrence && scope.source && scope.eye==geometry.draw.side && scope.eye<2;
}
bool Selections(const source_selectors::Result& result,const source_selectors::FamilyKey& family,
    const families::ResourceUse& use,const Location& location,UINT extent,std::set<const source_selectors::TableSelection*>& consumed){
    if(!result.selectorEvaluationComplete || result.family!=family || result.tables.size()>64 || result.selections.size()>source_selectors::MaxTotalSites ||
        result.evaluatedSites!=result.selections.size() || result.nativeExecutionPermission || result.nativeExecutionPermitted || result.nativeCommandsSuppressed)return false;
    bool matched{};
    for(const auto& table:result.tables){const auto& key=table.key;
        if(key.parameter!=location.parameter || key.visibility!=use.visibility || key.space!=use.space || key.shaderRegisterBase!=use.shaderRegister ||
            key.originalTableOffset!=location.offset || key.declaredExtent!=use.count || key.rootSha256!=family.rootSha256)continue;
        if(matched || table.offsets.empty() || table.offsets.size()>source_selectors::MaxTotalSites || !std::is_sorted(table.offsets.begin(),table.offsets.end()) ||
            std::adjacent_find(table.offsets.begin(),table.offsets.end())!=table.offsets.end() || table.count!=uint64_t(table.offsets.back())+1 || table.count>extent)return false;
        std::vector<UINT> intervals;
        for(const auto& interval:table.uses){if(interval.type!=use.type || !interval.count || interval.offset<location.offset ||
                uint64_t(interval.offset)+interval.count>uint64_t(location.offset)+use.count || interval.count>source_selectors::MaxTotalSites-intervals.size())return false;
            for(UINT n=0;n<interval.count;++n)intervals.push_back(interval.offset+n);}
        if(intervals!=table.offsets)return false;
        for(const auto offset:table.offsets)if(offset<location.offset || uint64_t(offset)-location.offset>=use.count || offset>=extent)return false;
        consumed.insert(&table);matched=true;
    }return matched;
}
bool Coverage(const GeometryPassDraw& input,bool& needsContract){
    const auto& family=*input.family;const auto& plan=family.pairedRoot->plan;needsContract=!input.shared.empty();
    if(!family.sourceRoot || input.geometry.eyes[0].draw.root!=reinterpret_cast<uintptr_t>(family.sourceRoot.Get()) ||
        input.geometry.eyes[1].draw.root!=reinterpret_cast<uintptr_t>(family.sourceRoot.Get()) || family.uses.empty() || family.uses.size()>4096 ||
        input.shared.size()>64 || input.constants.size()!=plan.bindings.size())return false;
    ComPtr<ID3D12VersionedRootSignatureDeserializer> reader;const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* decoded{};
    if(plan.serialized.empty() || FAILED(D3D12CreateVersionedRootSignatureDeserializer(plan.serialized.data(),plan.serialized.size(),IID_PPV_ARGS(&reader))) ||
        FAILED(reader->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1,&decoded)))return false;
    const auto& root=decoded->Desc_1_1;
    if(root.NumParameters>64 || plan.originalParameters>root.NumParameters || root.Flags&(D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE|
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED))return false;
    std::map<UINT,const SharedBankTable*> shared;
    for(const auto& table:input.shared){if(table.parameter>=plan.originalParameters || !table.count || !table.cpuHeap || table.leases.empty() ||
            !shared.emplace(table.parameter,&table).second)return false;
        const auto heap=table.cpuHeap->GetDesc();if(heap.Flags!=D3D12_DESCRIPTOR_HEAP_FLAG_NONE || table.offset>heap.NumDescriptors || table.count>heap.NumDescriptors-table.offset)return false;
        if(root.pParameters[table.parameter].ParameterType!=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)return false;
    }
    std::set<const PairedBindingLocation*> usedPairs;std::set<UINT> usedShared;
    std::array<std::set<const source_selectors::TableSelection*>,2> selections;
    const source_selectors::FamilyKey key{family.rootSha256,family.vertexSha256,family.pixelSha256};
    for(const auto& use:family.uses){if(!use.count || (use.visibility!=1 && use.visibility!=5) || use.type==D3D12_DESCRIPTOR_RANGE_TYPE_UAV || use.type>3)return false;
        const PairedBindingLocation* paired{};
        if(!use.space && use.count==1)for(const auto& binding:plan.bindings)if(UINT(binding.binding.type)==use.type &&
            UINT(binding.binding.visibility)==use.visibility && binding.binding.sourceRegister==use.shaderRegister){if(paired)return false;paired=&binding;}
        if(paired){if(!usedPairs.insert(paired).second || paired->sourceParameter>=plan.originalParameters)return false;
            const auto& source=root.pParameters[paired->sourceParameter];
            if(!Visibility(source.ShaderVisibility,use.visibility) || source.ParameterType!=paired->sourceType)return false;
            if(source.ParameterType==D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE){Location location;if(!Locate(root,plan.originalParameters,use,location) ||
                    location.parameter!=paired->sourceParameter || location.offset!=paired->sourceOffset)return false;
            }else if(source.ParameterType!=(use.type==D3D12_DESCRIPTOR_RANGE_TYPE_CBV?D3D12_ROOT_PARAMETER_TYPE_CBV:D3D12_ROOT_PARAMETER_TYPE_SRV) ||
                source.Descriptor.RegisterSpace || source.Descriptor.ShaderRegister!=use.shaderRegister)return false;
            const BankPair* value{};for(const auto& pair:input.constants)if(pair.binding==paired->binding){if(value)return false;value=&pair;}
            if(!value)return false;
            if(use.type==D3D12_DESCRIPTOR_RANGE_TYPE_CBV){if(!use.bytes || use.bytes>65536)return false;
                for(const auto& eye:value->eyes)if(eye.source || eye.constants.size()<use.bytes)return false;
            }else if(use.type==D3D12_DESCRIPTOR_RANGE_TYPE_SRV){needsContract=true;for(const auto& eye:value->eyes)if(!eye.constants.empty())return false;
            }else return false;
            continue;
        }
        if(use.type==D3D12_DESCRIPTOR_RANGE_TYPE_CBV)return false;
        Location location;const bool located=Locate(root,plan.originalParameters,use,location);const bool fixed=StaticSampler(root,use);
        if(fixed){if(located)return false;continue;}
        if(!located)return false;const auto at=shared.find(location.parameter);if(at==shared.end())return false;
        const auto& table=*at->second;const auto heap=table.cpuHeap->GetDesc();
        if(heap.Type!=(use.type==D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER?D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV))return false;
        usedShared.insert(location.parameter);needsContract=true;
        if(location.range->NumDescriptors==UINT_MAX && use.count>1){
            if(use.type!=D3D12_DESCRIPTOR_RANGE_TYPE_SRV || key.rootSha256.empty() || key.vertexSha256.empty())return false;
            for(UINT eye=0;eye<2;++eye){const auto& proof=input.sharedRead.eyes[eye];
                if(!Scope(proof.scope,family,input.geometry.eyes[eye]) || !proof.gpuInputOwner || !proof.selectors ||
                    !Selections(*proof.selectors,key,use,location,table.count,selections[eye]))return false;}
        }else if(uint64_t(location.offset)+use.count>table.count)return false;
    }
    if(usedPairs.size()!=plan.bindings.size() || usedShared.size()!=shared.size())return false;
    for(UINT eye=0;eye<2;++eye)if(const auto* result=input.sharedRead.eyes[eye].selectors){
        if(result->family!=key || !result->selectorEvaluationComplete || result->tables.size()!=selections[eye].size() ||
            result->evaluatedSites!=result->selections.size() || result->selections.size()>source_selectors::MaxTotalSites)return false;
        for(const auto& table:result->tables)if(!selections[eye].contains(&table))return false;
        std::set<UINT> sites;
        for(const auto& selection:result->selections)if(selection.siteIndex>=result->evaluatedSites || !sites.insert(selection.siteIndex).second || !std::any_of(result->tables.begin(),result->tables.end(),[&](const auto& table){
            return selection.shaderIndex<table.key.declaredExtent && uint64_t(table.key.originalTableOffset)+selection.shaderIndex==selection.descriptorOffset &&
                std::binary_search(table.offsets.begin(),table.offsets.end(),selection.descriptorOffset);}))return false;
        for(const auto& table:result->tables)for(const auto offset:table.offsets)if(std::none_of(result->selections.begin(),result->selections.end(),
            [&](const auto& selection){return selection.descriptorOffset==offset && uint64_t(table.key.originalTableOffset)+selection.shaderIndex==offset;}))return false;
    }
    return true;
}
bool ReadContract(ID3D12Device* device,const GeometrySharedReadContract& contract,const families::Family& family,
    std::span<const BankPair> pairs,std::span<const SharedBankTable> tables,ID3D12GraphicsCommandList* actual){
    constexpr UINT reads=D3D12_RESOURCE_STATE_GENERIC_READ,shader=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if(!contract.list || !contract.generation || !contract.epochOwner || !contract.allocatorOwner || !contract.queueOwner || !contract.validate || contract.resources.size()>8192 ||
        family.rootSha256.empty() || family.vertexSha256.empty() || (family.pass.pixelShader && family.pixelSha256.empty()))return false;
    if(actual && (reinterpret_cast<uintptr_t>(actual)!=contract.list || actual->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT || !SameDevice(device,actual)))return false;
    std::set<ID3D12Resource*> resources;
    for(const auto& read:contract.resources)if(!read.resource || !SameDevice(device,read.resource.Get()) || !read.state || !(UINT(*read.state)&shader) ||
        (UINT(*read.state)&~reads) || !read.allocationOwner || !read.producerPosition || !resources.insert(read.resource.Get()).second)return false;
    for(const auto& pair:pairs)if(pair.binding.type==D3D12_DESCRIPTOR_RANGE_TYPE_SRV)for(const auto& value:pair.eyes)if(value.source){
        const auto read=std::find_if(contract.resources.begin(),contract.resources.end(),[&](const auto& entry){return entry.resource==value.source;});
        if(read==contract.resources.end() || !read->state || *read->state!=value.state ||
            !(UINT(value.state)&shader) || (UINT(value.state)&~reads))return false;
    }
    for(UINT eye=0;eye<2;++eye){const auto& scope=contract.eyes[eye].scope;
        if(!scope.root || scope.root!=reinterpret_cast<uintptr_t>(family.sourceRoot.Get()) || !scope.list || !scope.generation || !scope.occurrence || !scope.source ||
            scope.eye!=(eye?0u:1u) || !contract.eyes[eye].gpuInputOwner)return false;}
    return contract.validate(contract,family,pairs,tables,actual);
}
}
HRESULT StereoGeometryPass::Create(ID3D12Device* device,std::span<const GeometryPassDraw> inputs,const GeometryPassTargets& targets) try {
    validationStage_=1;validationDraw_=validationBits_=0;
    if(storage_ || !device || inputs.empty() || inputs.size()>2048 || targets.colors.size()>8 || (!targets.depth && targets.colors.empty()))return E_INVALIDARG;
    ComPtr<Storage> pending;pending.Attach(new Storage);pending->device=device;pending->targets=targets;
    std::vector<BankDraw> banks;std::vector<GeometryCommandPair> geometry;banks.reserve(inputs.size());geometry.reserve(inputs.size());
    families::PassContract first{};UINT64 width{};UINT height{};
    for(const auto& input:inputs) {
        validationDraw_=UINT(banks.size());validationStage_=2;
        if(!input.family || !input.family->pairedRoot || !SameDevice(device,input.family->pipeline.Get()) ||
            !SameDevice(device,input.family->sourceRoot.Get()) || !SameDevice(device,input.family->pairedRoot->root.Get()) || !input.viewMask || (input.viewMask&~3u))return E_INVALIDARG;
        const auto& family=*input.family;const auto& contract=family.pass;const auto& a=input.geometry.eyes[0];const auto& b=input.geometry.eyes[1];
        validationStage_=3;
        validationBits_=(!family.instanceRebaseSafe?1u:0u) | (!contract.fixedRasterState?2u:0u) | (contract.samples.Count!=1 || contract.samples.Quality?4u:0u) |
            (contract.topology!=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE?8u:0u) | (contract.targets.NumRenderTargets!=targets.colors.size()?16u:0u) |
            (bool(targets.depth)!=(contract.depth!=DXGI_FORMAT_UNKNOWN)?32u:0u) |
            (reinterpret_cast<uintptr_t>(a.state.original.Get())!=a.draw.pipeline || !SameDevice(device,a.state.original.Get())?64u:0u) |
            (a.state.topology!=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST && a.state.topology!=D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP?128u:0u) |
            ((StateDifferences(a.state,b.state)&(contract.pixelShader?UINT_MAX:~224u))<<8);
        if(validationBits_)return E_INVALIDARG;
        validationStage_=4;
        bool needsContract{};if(!Coverage(input,needsContract))return E_INVALIDARG;
        if(needsContract && !ReadContract(device,input.sharedRead,family,input.constants,input.shared,nullptr))return E_INVALIDARG;
        validationStage_=5;
        if(banks.empty()) {
            first=contract;
            for(UINT i=0;i<targets.colors.size();++i)if(!Target(device,targets.colors[i].Get(),contract.targets.RTFormats[i],false,width,height))return E_INVALIDARG;
            if(targets.depth && !Target(device,targets.depth.Get(),contract.depth,true,width,height))return E_INVALIDARG;
        }else if(contract.depth!=first.depth || std::memcmp(contract.targets.RTFormats,first.targets.RTFormats,targets.colors.size()*sizeof(DXGI_FORMAT)))return E_INVALIDARG;
        validationStage_=6;
        if(!View(a.draw,width,height) || a.draw.rtCount!=targets.colors.size() || b.draw.rtCount!=targets.colors.size())return E_INVALIDARG;
        validationStage_=7;
        for(const auto factor:a.state.blendFactor)if(!std::isfinite(factor))return E_INVALIDARG;
        banks.push_back({family.pairedRoot->root.Get(),&family.pairedRoot->plan,input.constants,input.shared});
        geometry.push_back(input.geometry);geometry.back().instanceRebaseSafe=family.instanceRebaseSafe;
        pending->draws.push_back({family.pipeline,a.state,a.draw.viewport,a.draw.scissor,input.viewMask});
        if(needsContract){auto& admission=pending->draws.back().admission;admission.needed=true;admission.family=input.family;
            admission.contract=input.sharedRead;admission.resources.assign(input.sharedRead.resources.begin(),input.sharedRead.resources.end());
            admission.shared.assign(input.shared.begin(),input.shared.end());admission.pairs.assign(input.constants.begin(),input.constants.end());
            admission.constants.resize(admission.pairs.size()*2);
            for(size_t pair=0;pair<admission.pairs.size();++pair)for(UINT eye=0;eye<2;++eye)if(admission.pairs[pair].binding.type==D3D12_DESCRIPTOR_RANGE_TYPE_CBV){
                auto& bytes=admission.constants[pair*2+eye];const auto source=admission.pairs[pair].eyes[eye].constants;
                bytes.assign(source.begin(),source.end());admission.pairs[pair].eyes[eye].constants=bytes;}
            // Rebind views from owned storage at Record; inline Result addresses
            // can change when the draw vector moves during preparation.
            admission.contract.resources={};
            for(UINT eye=0;eye<2;++eye){if(const auto* selectors=input.sharedRead.eyes[eye].selectors){
                    admission.selectors[eye]=*selectors;admission.hasSelectors[eye]=true;}
                admission.contract.eyes[eye].selectors=nullptr;}
        }
        // VRS changes pixel-shader invocation rate, not depth/stencil coverage.
        // A PS-less pipeline has no reader for an inherited shading-rate image.
        if(!contract.pixelShader){auto& state=pending->draws.back().state;state.rate={};state.rate.known=true;state.rateImage.Reset();}
    }
    validationStage_=8;auto hr=pending->bank.CreatePass(device,banks);if(FAILED(hr))return hr;
    validationStage_=9;
    hr=pending->geometry.Create(device,geometry);if(FAILED(hr))return hr;
    validationStage_=10;probe::Scope internal;
    if(!targets.colors.empty()) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,UINT(targets.colors.size()),D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
        hr=device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&pending->rtvs));if(FAILED(hr))return hr;
        pending->rtvStride=device->GetDescriptorHandleIncrementSize(hd.Type);auto handle=pending->rtvs->GetCPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<targets.colors.size();++i){D3D12_RENDER_TARGET_VIEW_DESC view{};view.Format=first.targets.RTFormats[i];view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2DARRAY;view.Texture2DArray.ArraySize=2;
            device->CreateRenderTargetView(targets.colors[i].Get(),&view,handle);handle.ptr+=pending->rtvStride;}
    }
    if(targets.depth) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
        hr=device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&pending->dsv));if(FAILED(hr))return hr;
        D3D12_DEPTH_STENCIL_VIEW_DESC view{};view.Format=first.depth;view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY;view.Texture2DArray.ArraySize=2;
        device->CreateDepthStencilView(targets.depth.Get(),&view,pending->dsv->GetCPUDescriptorHandleForHeapStart());
    }
    storage_=std::move(pending);validationStage_=validationDraw_=validationBits_=0;return S_OK;
}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
bool StereoGeometryPass::Record(ID3D12GraphicsCommandList* list) {
    if(!storage_ || storage_->sourceOnly || storage_->recorded || !list || list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT || !SameDevice(storage_->device.Get(),list))return false;
    ComPtr<ID3D12GraphicsCommandList1> views;if(FAILED(list->QueryInterface(IID_PPV_ARGS(&views))))return false;
    ComPtr<ID3D12GraphicsCommandList5> rates;
    if(FAILED(list->QueryInterface(IID_PPV_ARGS(&rates))))return false;
    for(const auto& draw:storage_->draws)if(draw.admission.needed){const auto& admission=draw.admission;
        auto contract=admission.contract;contract.resources=admission.resources;
        for(UINT eye=0;eye<2;++eye)contract.eyes[eye].selectors=admission.hasSelectors[eye]?&admission.selectors[eye]:nullptr;
        if(!ReadContract(storage_->device.Get(),contract,*admission.family,admission.pairs,admission.shared,list))return false;
    }
    if(!gpu::KeepCommandResources(list,{storage_.Get()}))return false;
    storage_->recorded=true;probe::Scope internal;
    if(!storage_->bank.CaptureEye(list,0) || !storage_->bank.CaptureEye(list,1))return false;
    const auto rtv=storage_->rtvs?storage_->rtvs->GetCPUDescriptorHandleForHeapStart():D3D12_CPU_DESCRIPTOR_HANDLE{};
    const auto dsv=storage_->dsv?storage_->dsv->GetCPUDescriptorHandleForHeapStart():D3D12_CPU_DESCRIPTOR_HANDLE{};
    list->SetPredication(nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
    list->OMSetRenderTargets(UINT(storage_->targets.colors.size()),storage_->rtvs?&rtv:nullptr,TRUE,storage_->dsv?&dsv:nullptr);
    rates->RSSetShadingRateImage(nullptr);
    for(UINT i=0;i<storage_->draws.size();++i) {
        const auto& draw=storage_->draws[i];list->SetPipelineState(draw.pipeline.Get());
        if(!storage_->bank.Bind(list,i))return false;
        list->IASetPrimitiveTopology(draw.state.topology);list->OMSetStencilRef(draw.state.stencil);list->OMSetBlendFactor(draw.state.blendFactor.data());
        list->RSSetViewports(1,&draw.viewport);list->RSSetScissorRects(1,&draw.scissor);
        rates->RSSetShadingRate(draw.state.rate.value,draw.state.rate.combiners.data());views->SetViewInstanceMask(draw.mask);
        if(!storage_->geometry.RecordGeometry(list,i))return false;
    }
    return true;
}
UINT StereoGeometryPass::DrawCount()const{return storage_?UINT(storage_->draws.size()):0;}
UINT64 StereoGeometryPass::InstanceBytes()const{return storage_?storage_->geometry.InstanceBytes():0;}
HRESULT StereoGeometryPass::CreateSources(ID3D12Device* device,std::span<const GeometrySourcePassDraw> inputs)try{
    validationStage_=11;validationDraw_=validationBits_=0;
    if(storage_ || !device || inputs.empty() || inputs.size()>StereoGeometryCommands::MaxSourceCommands)return E_INVALIDARG;
    ComPtr<Storage> pending;pending.Attach(new Storage);pending->device=device;pending->sourceOnly=true;
    std::vector<SourceBankDraw> banks;std::vector<GeometrySourceCommand> geometry;
    banks.reserve(inputs.size());geometry.reserve(inputs.size());pending->draws.reserve(inputs.size());
    for(const auto& input:inputs){
        validationDraw_=UINT(banks.size());validationStage_=12;
        const auto& source=input.geometry.geometry;const auto& draw=source.draw;const auto& state=source.state;const auto& contract=input.contract;
        if(!SameDevice(device,state.original.Get()) || draw.pipeline!=reinterpret_cast<uintptr_t>(state.original.Get()) ||
            draw.root!=reinterpret_cast<uintptr_t>(input.binding.root.Get()) || !SameDevice(device,input.binding.root.Get()) ||
            (draw.flags&48)!=48 || (draw.flags&64) || !state.rate.known ||
            !contract.fixedRasterState || contract.samples.Count!=1 || contract.samples.Quality ||
            contract.topology!=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE || contract.targets.NumRenderTargets>8 ||
            contract.targets.NumRenderTargets!=draw.rtCount ||
            (state.topology!=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST && state.topology!=D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP) || state.stencil>255 ||
            state.rate.image!=state.rateImage.Get() || (state.rateImage && !SameDevice(device,state.rateImage.Get())))return E_INVALIDARG;
        const auto& v=draw.viewport;const auto& s=draw.scissor;
        if(!std::isfinite(v.TopLeftX) || !std::isfinite(v.TopLeftY) || !std::isfinite(v.Width) || !std::isfinite(v.Height) ||
            !std::isfinite(v.MinDepth) || !std::isfinite(v.MaxDepth) || v.Width<=0 || v.Height<=0 ||
            v.MinDepth<0 || v.MaxDepth>1 || v.MinDepth>v.MaxDepth || s.right<s.left || s.bottom<s.top)return E_INVALIDARG;
        for(const auto value:state.blendFactor)if(!std::isfinite(value))return E_INVALIDARG;
        const auto rate=state.rate.value;
        if(rate!=D3D12_SHADING_RATE_1X1 && rate!=D3D12_SHADING_RATE_1X2 && rate!=D3D12_SHADING_RATE_2X1 &&
            rate!=D3D12_SHADING_RATE_2X2 && rate!=D3D12_SHADING_RATE_2X4 && rate!=D3D12_SHADING_RATE_4X2 && rate!=D3D12_SHADING_RATE_4X4)return E_INVALIDARG;
        for(const auto combiner:state.rate.combiners)if(combiner<D3D12_SHADING_RATE_COMBINER_PASSTHROUGH || combiner>D3D12_SHADING_RATE_COMBINER_SUM)return E_INVALIDARG;
        banks.push_back(input.binding);geometry.push_back(input.geometry);
        pending->draws.push_back({state.original,state,draw.viewport,draw.scissor,0});
    }
    validationStage_=13;auto hr=pending->bank.CreateSourcePass(device,banks);if(FAILED(hr))return hr;
    validationStage_=14;hr=pending->geometry.CreateMixedSources(device,geometry);if(FAILED(hr))return hr;
    storage_=std::move(pending);validationStage_=validationDraw_=validationBits_=0;return S_OK;
}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
bool StereoGeometryPass::RecordSource(ID3D12GraphicsCommandList* list,UINT index,SourceTargetBind target,void* user){
    if(!storage_ || !storage_->sourceOnly || !list || !target || index!=storage_->sourceNext || index>=storage_->draws.size() ||
        list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT || !SameDevice(storage_->device.Get(),list) ||
        (storage_->sourceRecording && (storage_->sourceRecording!=list || !gpu::HasCommandResource(list,storage_.Get()))))return false;
    ComPtr<ID3D12GraphicsCommandList5> rates;if(FAILED(list->QueryInterface(IID_PPV_ARGS(&rates))))return false;
    if(!gpu::KeepCommandResources(list,{storage_.Get()}))return false;
    storage_->sourceRecording=list;
    probe::Scope internal;const auto& draw=storage_->draws[index];
    if(!target(user,list,index))return false;
    list->SetPipelineState(draw.pipeline.Get());
    if(!storage_->bank.BindSource(list,index))return false;
    list->SetPredication(nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
    list->IASetPrimitiveTopology(draw.state.topology);list->OMSetStencilRef(draw.state.stencil);list->OMSetBlendFactor(draw.state.blendFactor.data());
    list->RSSetViewports(1,&draw.viewport);list->RSSetScissorRects(1,&draw.scissor);
    rates->RSSetShadingRateImage(draw.state.rateImage.Get());rates->RSSetShadingRate(draw.state.rate.value,draw.state.rate.combiners.data());
    if(!storage_->geometry.RecordSourceGeometry(list,index))return false;
    ++storage_->sourceNext;return true;
}
UINT StereoGeometryPass::SourceCount()const{return storage_ && storage_->sourceOnly?UINT(storage_->draws.size()):0;}
UINT StereoGeometryPass::SourceRecordedCount()const{return storage_ && storage_->sourceOnly?storage_->sourceNext:0;}
}
