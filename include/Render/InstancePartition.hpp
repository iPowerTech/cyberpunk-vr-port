#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <algorithm>

namespace cvr::stereo {
// Coverage bookkeeping, not permission to change SV_InstanceID semantics.
// A native peer batch must match one unconsumed contiguous source span exactly.
inline bool FindInstancePartition(std::span<const uint8_t> source,std::span<const uint8_t> peer,
    uint32_t stride,const std::vector<bool>& used,uint32_t& first){
    if((stride!=48 && stride!=64) || source.empty() || peer.empty() || source.size()>16384 || source.size()%stride ||
        peer.size()%stride || peer.size()>source.size() || used.size()!=source.size()/stride)return false;
    uint32_t found=UINT32_MAX;const auto count=peer.size()/stride;
    for(uint32_t i=0;i+count<=used.size();++i){
        if(std::any_of(used.begin()+i,used.begin()+i+count,[](bool v){return v;}))continue;
        if(!std::equal(peer.begin(),peer.end(),source.begin()+size_t(i)*stride))continue;
        if(found!=UINT32_MAX)return false;found=i;
    }
    if(found==UINT32_MAX)return false;first=found;return true;
}
}
