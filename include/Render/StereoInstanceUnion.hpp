#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cvr::stereo {
struct PackedInstances {
    // A record concatenates every per-instance stream used by this batch.
    // This includes skinning/previous-pose addresses, not only the transform.
    std::span<const uint8_t> bytes;
    uint32_t stride{};
};
struct StereoInstanceGroups {
    // Masks 1, 2 and 3 select the first eye, second eye and both eyes.
    std::array<std::vector<uint8_t>,3> records;
    uint32_t stride{};
    size_t Count(unsigned mask) const {return mask>=1 && mask<=3 && stride?records[mask-1].size()/stride:0;}
};
struct InstanceMergeEligibility {
    bool identicalGeometryAndMaterials=false;
    bool opaqueOrderIndependent=false;
    bool usesInstanceId=true;
};
inline bool MergeStereoInstances(PackedInstances first,PackedInstances second,
    const InstanceMergeEligibility& eligibility,StereoInstanceGroups& output) {
    if(!eligibility.identicalGeometryAndMaterials || !eligibility.opaqueOrderIndependent || eligibility.usesInstanceId ||
       !first.stride || first.stride!=second.stride || first.stride>4096 ||
       first.bytes.size()%first.stride || second.bytes.size()%first.stride ||
       first.bytes.size()>16*1024*1024 || second.bytes.size()>16*1024*1024)return false;
    const auto firstCount=first.bytes.size()/first.stride,secondCount=second.bytes.size()/second.stride;
    StereoInstanceGroups result;result.stride=first.stride;
    // string_view equality compares all bytes after hashing, so a hash collision
    // cannot merge different instances. Count duplicates rather than deduplicating
    // them: two identical draw instances in one eye must remain two instances.
    struct Occurrences {std::vector<size_t> indices;size_t used{};};
    std::unordered_map<std::string_view,Occurrences> available;available.reserve(secondCount);
    for(size_t i=0;i<secondCount;++i) {
        const auto* record=second.bytes.data()+i*second.stride;
        available[{reinterpret_cast<const char*>(record),second.stride}].indices.push_back(i);
    }
    std::vector<bool> paired(secondCount);
    auto append=[&](unsigned mask,const uint8_t* data) {
        auto& group=result.records[mask-1];group.insert(group.end(),data,data+result.stride);
    };
    for(size_t i=0;i<firstCount;++i) {
        const auto* record=first.bytes.data()+i*first.stride;
        const auto found=available.find({reinterpret_cast<const char*>(record),first.stride});
        if(found!=available.end() && found->second.used<found->second.indices.size()) {
            auto& occurrences=found->second;paired[occurrences.indices[occurrences.used++]]=true;append(3,record);
        }else append(1,record);
    }
    for(size_t i=0;i<secondCount;++i)if(!paired[i])append(2,second.bytes.data()+i*second.stride);
    output=std::move(result);return true;
}
}
