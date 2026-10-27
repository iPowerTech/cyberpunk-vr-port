#pragma once
#include "Render/NativeStereoFamilies.hpp"
#include "Render/StereoDescriptorAudit.hpp"
#include <algorithm>
#include <array>
#include <span>

namespace cvr::stereo::source_tables {
struct Table {
    UINT parameter{}, count{};
    std::vector<descriptors::SourceSlotUse> uses;
};
struct DeferredUse {
    UINT parameter{}, offset{};
    families::ResourceUse use;
};
struct Plan {
    std::vector<Table> finite;
    std::vector<DeferredUse> deferred;
    bool Complete()const noexcept{return deferred.empty();}
};
// Covers declared finite descriptor domains, not resource contents or ordering.
// Unbounded-root domains need selectors from the same owned source occurrence.
inline bool Build(const D3D12_ROOT_SIGNATURE_DESC1& root,
    std::span<const families::ResourceUse> uses,Plan& output){
    output={};
    if(!root.NumParameters || root.NumParameters>64 || !root.pParameters || uses.empty() ||
        root.Flags&(D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE|
            D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|
            D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED))return false;
    struct Range {UINT parameter{},offset{},extent{},visibility{};D3D12_DESCRIPTOR_RANGE1 desc;bool tail{};};
    std::vector<Range> ranges;
    std::array<UINT,64> extents{};
    std::array<bool,64> tails{};
    for(UINT p=0;p<root.NumParameters;++p){const auto& parameter=root.pParameters[p];
        if(parameter.ParameterType!=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)continue;
        const auto& table=parameter.DescriptorTable;
        if(!table.NumDescriptorRanges || !table.pDescriptorRanges)return false;
        uint64_t cursor{};bool sampler=table.pDescriptorRanges[0].RangeType==D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        for(UINT n=0;n<table.NumDescriptorRanges;++n){const auto& range=table.pDescriptorRanges[n];
            if(tails[p] || !range.NumDescriptors || range.RangeType>D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER ||
                (range.RangeType==D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)!=sampler)return false;
            if(range.OffsetInDescriptorsFromTableStart!=D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)cursor=range.OffsetInDescriptorsFromTableStart;
            if(cursor>UINT_MAX)return false;
            const bool tail=range.NumDescriptors==UINT_MAX;
            if(tail){if(n+1!=table.NumDescriptorRanges)return false;tails[p]=true;}
            else if(cursor+range.NumDescriptors>1000000 || uint64_t(range.BaseShaderRegister)+range.NumDescriptors>uint64_t(UINT_MAX)+1)return false;
            ranges.push_back({p,UINT(cursor),range.NumDescriptors,UINT(parameter.ShaderVisibility),range,tail});
            if(!tail){cursor+=range.NumDescriptors;extents[p]=std::max(extents[p],UINT(cursor));}
        }
    }
    Plan plan;
    for(const auto& use:uses){
        if(use.type>D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER || !use.count)return false;
        const Range* match{};
        for(const auto& range:ranges){
            if(UINT(range.desc.RangeType)!=use.type || range.desc.RegisterSpace!=use.space ||
                (range.visibility!=D3D12_SHADER_VISIBILITY_ALL && range.visibility!=use.visibility) ||
                use.shaderRegister<range.desc.BaseShaderRegister)continue;
            const uint64_t relative=uint64_t(use.shaderRegister)-range.desc.BaseShaderRegister;
            if(!range.tail && (relative>=range.extent || use.count>range.extent-relative))continue;
            if(relative+range.offset>UINT_MAX || uint64_t(use.shaderRegister)+use.count>uint64_t(UINT_MAX)+1 || match)return false;
            match=&range;
        }
        if(!match)return false;
        const auto offset=match->offset+(use.shaderRegister-match->desc.BaseShaderRegister);
        if(tails[match->parameter]){plan.deferred.push_back({match->parameter,offset,use});continue;}
        auto table=std::find_if(plan.finite.begin(),plan.finite.end(),[&](const auto& value){return value.parameter==match->parameter;});
        if(table==plan.finite.end()){plan.finite.push_back({match->parameter,extents[match->parameter],{}});table=std::prev(plan.finite.end());}
        table->uses.push_back({offset,use.count,D3D12_DESCRIPTOR_RANGE_TYPE(use.type)});
    }
    std::sort(plan.finite.begin(),plan.finite.end(),[](const auto& a,const auto& b){return a.parameter<b.parameter;});
    output=std::move(plan);return true;
}
}
