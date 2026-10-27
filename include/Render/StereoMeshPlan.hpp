#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace cvr::stereo {
// IDs come from exact equality of a caller-owned, fully prepared draw contract,
// not a hash alone. Zero is an unsupported/per-eye command and never merges.
// The caller must certify view-aware shaders, bindings, instance IDs, resource
// lifetimes and absence of cross-eye side effects before assigning a shared ID.
struct MeshPlanDraw {
    uint32_t mask{};
    std::array<uint32_t,2> source{UINT32_MAX,UINT32_MAX};
};
struct MeshPlan {
    std::vector<MeshPlanDraw> draws;
    uint32_t common{};
};
struct MeshPlanLimits {
    uint32_t drawsPerEye=32768;
    uint32_t matchingPairs=262144;
};
// A shortest common supersequence retains each eye's native order, including
// repeated identical objects and order-sensitive draws. Hunt-Szymanski LCS uses
// O((n + matches) log n), with a hard bound for highly duplicated inputs.
// Failure leaves output untouched; native multipass remains the caller's fallback.
inline bool BuildStereoMeshPlan(std::span<const uint64_t> first,std::span<const uint64_t> second,
    MeshPlan& output,MeshPlanLimits limits={}) {
    if(first.size()>limits.drawsPerEye || second.size()>limits.drawsPerEye)return false;
    struct Match {uint32_t first,second,previous;};
    std::unordered_map<uint64_t,std::vector<uint32_t>> positions;
    positions.reserve(second.size());
    for(uint32_t j=0;j<second.size();++j)if(second[j])positions[second[j]].push_back(j);
    size_t matchCount{};
    for(auto id:first)if(id) {
        const auto it=positions.find(id);if(it==positions.end())continue;
        if(it->second.size()>limits.matchingPairs-matchCount)return false;
        matchCount+=it->second.size();
    }
    std::vector<Match> matches;matches.reserve(matchCount);
    std::vector<uint32_t> tails,tailNodes;
    tails.reserve(std::min(first.size(),second.size()));tailNodes.reserve(tails.capacity());
    for(uint32_t i=0;i<first.size();++i) {
        if(!first[i])continue;
        const auto found=positions.find(first[i]);if(found==positions.end())continue;
        // Descending positions prevent a duplicate in one eye from matching
        // more than one occurrence in the other eye in the same chain.
        for(auto j=found->second.rbegin();j!=found->second.rend();++j) {
            const auto at=size_t(std::lower_bound(tails.begin(),tails.end(),*j)-tails.begin());
            if(at<tails.size() && tails[at]==*j)continue;
            const auto previous=at?tailNodes[at-1]:UINT32_MAX;
            matches.push_back({i,*j,previous});
            if(at==tails.size()){tails.push_back(*j);tailNodes.push_back(uint32_t(matches.size()-1));}
            else {tails[at]=*j;tailNodes[at]=uint32_t(matches.size()-1);}
        }
    }
    std::vector<std::array<uint32_t,2>> shared;
    for(auto at=tailNodes.empty()?UINT32_MAX:tailNodes.back();at!=UINT32_MAX;at=matches[at].previous)
        shared.push_back({matches[at].first,matches[at].second});
    std::reverse(shared.begin(),shared.end());
    MeshPlan result;result.common=uint32_t(shared.size());
    result.draws.reserve(first.size()+second.size()-shared.size());
    uint32_t i{},j{};
    auto singles=[&](uint32_t endFirst,uint32_t endSecond) {
        while(i<endFirst)result.draws.push_back({1,{i++,UINT32_MAX}});
        while(j<endSecond)result.draws.push_back({2,{UINT32_MAX,j++}});
    };
    for(const auto pair:shared) {
        singles(pair[0],pair[1]);result.draws.push_back({3,{i++,j++}});
    }
    singles(uint32_t(first.size()),uint32_t(second.size()));output=std::move(result);return true;
}
}
