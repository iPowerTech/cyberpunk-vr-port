#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace cvr::stereo {
struct NativeCullCandidate {
    uint64_t id{};
    std::array<float,6> bounds{};
    uint16_t includeMask{},excludeMask{};
    uint32_t reserved{};
};
static_assert(sizeof(NativeCullCandidate)==40);
struct NativeVisibleIds {
    uint64_t* data{};
    uint32_t capacity{},count{};
};
static_assert(sizeof(NativeVisibleIds)==16);
using NativeFineCull=char(*)(void*,int64_t*,char,void*);
inline constexpr size_t MaxFineCandidates=65536;

inline bool FineCandidateCount(uintptr_t begin,uintptr_t end,size_t& count) {
    count=0;
    if(!begin || end<begin || (end-begin)%sizeof(NativeCullCandidate))return false;
    count=(end-begin)/sizeof(NativeCullCandidate);
    return count<=MaxFineCandidates;
}

// The native fine test appends at most one ID per candidate. Preallocate the
// whole output so the native container never grows or owns this allocation.
inline bool RunNativeFine(NativeFineCull fine,void* query,
    std::span<const NativeCullCandidate> candidates,char partial,std::vector<uint64_t>& ids) {
    ids.clear();
    if(!fine || !query || candidates.size()>MaxFineCandidates)return false;
    if(candidates.empty())return true;
    ids.resize(candidates.size());
    NativeVisibleIds output{ids.data(),static_cast<uint32_t>(ids.size()),0};
    int64_t range[]{static_cast<int64_t>(reinterpret_cast<uintptr_t>(candidates.data())),
        static_cast<int64_t>(reinterpret_cast<uintptr_t>(candidates.data()+candidates.size()))};
    fine(query,range,partial,&output);
    if(output.data!=ids.data() || output.capacity!=ids.size() || output.count>output.capacity){ids.clear();return false;}
    ids.resize(output.count);return true;
}
}
