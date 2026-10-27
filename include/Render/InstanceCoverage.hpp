#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace cvr::stereo {
struct InstanceSource {
    std::span<const uint8_t> bytes;std::vector<bool>* covered{};
    // Optional actual rendered-view mask. Bit 0 is the peer layer.
    std::span<const uint8_t> viewMasks;
};
struct InstanceMatch {uint32_t source{},instance{},peer{};};
struct InstanceRun {uint32_t first{},count{};};
struct InstanceCoveragePlan {std::vector<InstanceMatch> matches;std::vector<InstanceRun> runs;uint32_t unmatched{};};
// The caller groups sources by a fully identical geometry/material contract and
// certifies instance-ordinal independence. No coverage changes until Commit.
inline bool PlanInstanceCoverage(std::span<const InstanceSource> sources,std::span<const uint8_t> peer,
    uint32_t stride,InstanceCoveragePlan& output){
    if((stride!=48 && stride!=64) || peer.empty() || peer.size()%stride || peer.size()>16384 || sources.size()>128)return false;
    std::vector<std::vector<bool>> used;used.reserve(sources.size());
    // A source may concatenate one native MAIN batch and its owned peer-only additions.
    for(const auto& source:sources){if(!source.covered || source.bytes.empty() || source.bytes.size()%stride || source.bytes.size()>32768 ||
        source.covered->size()!=source.bytes.size()/stride || (!source.viewMasks.empty() && source.viewMasks.size()!=source.covered->size()))return false;
        used.push_back(*source.covered);
        for(size_t i=0;i<source.viewMasks.size();++i){const auto mask=source.viewMasks[i];if(!mask || mask>3)return false;
            if(!(mask&1))used.back()[i]=true;}
    }
    InstanceCoveragePlan plan;
    for(uint32_t p=0;p<peer.size()/stride;++p){bool found=false;
        for(uint32_t s=0;s<sources.size() && !found;++s)for(uint32_t i=0;i<used[s].size();++i){
            if(used[s][i] || !std::equal(peer.begin()+size_t(p)*stride,peer.begin()+size_t(p+1)*stride,sources[s].bytes.begin()+size_t(i)*stride))continue;
            used[s][i]=true;plan.matches.push_back({s,i,p});found=true;
            if(!plan.runs.empty() && plan.runs.back().first+plan.runs.back().count==p)++plan.runs.back().count;
            else plan.runs.push_back({p,1});break;
        }
        if(!found)++plan.unmatched;
    }
    output=std::move(plan);return true;
}
inline bool CommitInstanceCoverage(std::span<const InstanceSource> sources,const InstanceCoveragePlan& plan){
    std::vector<std::vector<bool>> used;for(const auto& source:sources){
        if(!source.covered || (!source.viewMasks.empty() && (source.viewMasks.size()!=source.covered->size() ||
            std::any_of(source.viewMasks.begin(),source.viewMasks.end(),[](auto mask){return !mask || mask>3;}))))return false;
        used.push_back(*source.covered);
    }
    for(const auto& m:plan.matches){if(m.source>=used.size() || m.instance>=used[m.source].size() || used[m.source][m.instance])return false;
        const auto masks=sources[m.source].viewMasks;
        if(!masks.empty() && (masks.size()!=used[m.source].size() || !(masks[m.instance]&1) || masks[m.instance]>3))return false;
        used[m.source][m.instance]=true;}
    for(size_t i=0;i<sources.size();++i)*sources[i].covered=std::move(used[i]);return true;
}
}
