#pragma once
#include "Render/NativeGeometryPackets.hpp"
#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

namespace cvr::stereo::packets {
struct PacketRange {uint32_t offset{},count{};};
inline std::array<uint8_t,40> PassIdentity(const Record& row) {
    std::array<uint8_t,40> result{};std::copy_n(row.groupBytes.begin(),result.size(),result.begin());
    // RenderElements reads +C and +14 and +20 as bytes. Adjacent padding
    // differs between worker descriptors; +18/+1C filter flags are retained.
    for(auto offset:{13u,21u,33u})std::fill_n(result.begin()+offset,3,0);
    return result;
}
// Reassemble one owned native sorted list from its worker partitions. Native
// addresses only prove contiguous slices; returned ranges index copied storage.
// Both eyes may use different partition counts (observed 1 vs 4).
inline bool AssemblePass(std::span<const Record> chunks,size_t packetCount,std::vector<PacketRange>& output) {
    if(chunks.empty() || chunks.size()>RecordCapacity)return false;
    auto word=[](const Record& row,size_t offset){uint32_t v{};std::memcpy(&v,row.groupBytes.data()+offset,4);return v;};
    const auto& first=chunks.front();const auto partitions=word(first,44);
    if(first.side>1 || first.node!=0x23A938 || chunks.size()!=(partitions?partitions:1))return false;
    std::vector<const Record*> ordered(chunks.size());
    for(const auto& row:chunks) {
        const auto part=word(row,40);
        if(part>=ordered.size() || ordered[part] || word(row,44)!=partitions ||
           row.frame!=first.frame || row.node!=first.node || row.side!=first.side || row.view!=first.view ||
           row.storage!=first.storage || row.renderer!=first.renderer || row.status>1 ||
           (row.status==uint32_t(Status::Empty))!=(row.count==0) || (row.metadataFlags&7)!=7 ||
           !row.beginQpc || row.endQpc<row.beginQpc || row.end<row.begin || (row.begin&7) ||
           row.end-row.begin!=uint64_t(row.count)*sizeof(Packet) || row.count>MaxSpanPackets ||
           row.offset>packetCount || row.count>packetCount-row.offset ||
           PassIdentity(row)!=PassIdentity(first) ||
           std::memcmp(row.argumentBytes.data()+16,first.argumentBytes.data()+16,4) ||
           std::memcmp(row.argumentBytes.data()+24,first.argumentBytes.data()+24,3))return false;
        ordered[part]=&row;
    }
    size_t total{};
    for(size_t i=0;i<ordered.size();++i) {
        if(!ordered[i] || (i && ordered[i-1]->end!=ordered[i]->begin))return false;
        total+=ordered[i]->count;
        if(total>MaxSpanPackets)return false;
        for(size_t j=0;j<i;++j)if(ordered[i]->offset<ordered[j]->offset+ordered[j]->count &&
            ordered[j]->offset<ordered[i]->offset+ordered[i]->count)return false;
    }
    // RenderElements computes slices with floor(total * part / partitions).
    // Missing, duplicated or altered worker ranges cannot form a complete pass.
    std::vector<PacketRange> result;result.reserve(ordered.size());
    for(size_t i=0;i<ordered.size();++i) {
        if(partitions && ordered[i]->count!=total*(i+1)/partitions-total*i/partitions)return false;
        result.push_back({ordered[i]->offset,ordered[i]->count});
    }
    output=std::move(result);return true;
}
}
