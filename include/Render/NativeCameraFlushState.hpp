#pragma once
#include "Render/NativeCameraBindingState.hpp"
#include <algorithm>
#include <vector>

namespace cvr::stereo::camera_upload::flush_state {
struct Table {
    uint32_t group{},rootParameter{},start{},count{},first{};
    int32_t cached{};
    std::vector<uint64_t> descriptors;
    bool operator==(const Table&)const=default;
};
struct Snapshot {
    Context context;
    uint64_t heap{},layout{},cpuTable{},dirty{},rebind{},groups{};
    uint64_t resourceMask{},samplerMask{},graphicsMask{};
    std::array<uint64_t,4> visibility{};
    std::array<uint32_t,3> programs{};
    uint64_t cachedCpu{},cachedGpu{};
    uint8_t cpuValid{},gpuValid{};
    uint32_t base{},capacity{},stride{},cursor{},count{};
    std::vector<Table> tables;
    bool operator==(const Snapshot&)const=default;
};
inline bool ReadOnce(ReadState read,Context context,uint32_t stages,Snapshot& out) {
    out={};
    const auto copy=[&](uint64_t p,void* dst,size_t n){return read && p && p<=UINT64_MAX-n && read(p,dst,n);};
    if(!context || !stages || stages>3 || context.state>UINT64_MAX-0x181 || context.context>UINT64_MAX-0x3c8)return false;
    Snapshot s;s.context=context;uint64_t list{},state{};uint8_t fullRebind{};
    if(!copy(context.context+0x30,&list,8) || !copy(context.context+0x60,&state,8) || list!=context.list || state!=context.state ||
       !copy(state,&s.heap,8) || !copy(state+8,&s.base,4) || !copy(state+0xc,&s.capacity,4) ||
       !copy(state+0x10,&s.stride,4) || !copy(state+0x14,&s.cursor,4) || !copy(state+0x28,&s.cpuTable,8) ||
       !copy(state+0x68,&s.layout,8) || !copy(state+0x70,&s.dirty,8) || !copy(state+0x78,&s.rebind,8) ||
       !copy(state+0x50,&s.cachedCpu,8) || !copy(state+0x58,&s.cachedGpu,8) ||
       !copy(state+0x60,&s.cpuValid,1) || !copy(state+0x61,&s.gpuValid,1) || s.cpuValid>1 || s.gpuValid>1 ||
       !copy(state+0x180,&fullRebind,1) || fullRebind || !s.heap || !s.cpuTable || !s.layout ||
       s.layout>UINT64_MAX-0x473 || !s.stride || s.stride>256 || !s.capacity || s.cursor>s.capacity)return false;
    if(!copy(s.layout,&s.samplerMask,8) || !copy(s.layout+8,&s.resourceMask,8) ||
       !copy(s.layout+0x18,s.visibility.data(),sizeof(s.visibility)) ||
       !copy(context.context+0x3c0,s.programs.data(),8) || !copy(context.context+0x10c,&s.programs[2],4))return false;
    for(uint32_t stage=0;stage<3;++stage)if(s.programs[stage] && s.programs[stage]<=0x7fffffff)s.graphicsMask|=s.visibility[stage];
    s.graphicsMask&=s.resourceMask|s.samplerMask;
    for(uint32_t stage=0;stage<2;++stage)if(stages&(1u<<stage)){
        uint8_t group{};
        if(!s.programs[stage] || s.programs[stage]>0x7fffffff)return false;
        if(!copy(s.layout+0x455+stage*0x1c,&group,1) || group>=64)return false;
        const uint64_t bit=uint64_t(1)<<group;
        if(!(s.resourceMask&bit) || (s.samplerMask&bit) || !(s.visibility[stage]&bit))return false;
        if(s.groups&bit)continue;
        if(s.layout>UINT64_MAX-0x38-uint64_t(group)*16-16)return false;
        const auto entry=s.layout+0x38+uint64_t(group)*16;
        uint16_t first{},count{};uint8_t kind{},parameter{};Table table;table.group=group;
        if(!copy(entry+4,&table.start,4) || !copy(entry+8,&first,2) || !copy(entry+10,&count,2) ||
           !copy(entry+13,&kind,1) || kind==2 || !copy(entry+14,&parameter,1) || parameter>=64 ||
           !count || first>1 || uint32_t(1-first)>=count || table.start>8192 || count>8192-table.start ||
           !copy(state+0x80+uint64_t(group)*4,&table.cached,4))return false;
        table.rootParameter=parameter;table.first=first;table.count=count;table.descriptors.resize(count);
        for(const auto& previous:s.tables)
            if(previous.rootParameter==table.rootParameter ||
               (previous.start<table.start+table.count && table.start<previous.start+previous.count))return false;
        const uint64_t offset=uint64_t(table.start)*8;
        if(s.cpuTable>UINT64_MAX-offset || !copy(s.cpuTable+offset,table.descriptors.data(),size_t(count)*8) ||
           std::any_of(table.descriptors.begin(),table.descriptors.end(),[](auto p){return !p;}))return false;
        s.count+=count;s.groups|=bit;s.tables.push_back(std::move(table));
    }
    if(!s.count || uint64_t(s.base)+s.capacity>UINT32_MAX ||
       (uint64_t(s.base)+s.cursor+uint64_t(s.count)*2)*s.stride>UINT32_MAX)return false;
    out=std::move(s);return true;
}
inline bool Read(ReadState read,Context context,uint32_t stages,Snapshot& out) {
    Snapshot a,b;
    if(!ReadOnce(read,context,stages,a) || !ReadOnce(read,context,stages,b) || a!=b)return false;
    out=std::move(a);return true;
}
inline bool Room(const Snapshot& s) {
    return !(s.dirty&s.graphicsMask) && !s.rebind && s.count && s.cursor<=s.capacity && uint64_t(s.count)*2<=s.capacity-s.cursor;
}
inline bool SameAllocation(const Snapshot& a,const Snapshot& b) {
    if(a.context!=b.context || a.heap!=b.heap || a.layout!=b.layout || a.cpuTable!=b.cpuTable ||
       a.base!=b.base || a.capacity!=b.capacity || a.stride!=b.stride || a.groups!=b.groups || a.count!=b.count ||
       a.tables.size()!=b.tables.size() || a.resourceMask!=b.resourceMask || a.samplerMask!=b.samplerMask ||
       a.graphicsMask!=b.graphicsMask || a.visibility!=b.visibility || a.programs!=b.programs)return false;
    if((a.cpuValid && (!b.cpuValid || a.cachedCpu!=b.cachedCpu)) ||
       (a.gpuValid && (!b.gpuValid || a.cachedGpu!=b.cachedGpu)))return false;
    for(size_t i=0;i<a.tables.size();++i){const auto& x=a.tables[i];const auto& y=b.tables[i];
        if(x.group!=y.group || x.rootParameter!=y.rootParameter || x.start!=y.start || x.count!=y.count || x.first!=y.first)return false;}
    return true;
}
inline bool Pending(const Snapshot& original,const Snapshot& next,uint64_t replacement) {
    if(!replacement || !SameAllocation(original,next) || next.cursor!=original.cursor ||
       next.dirty!=(original.dirty|original.groups) || next.rebind)return false;
    for(size_t t=0;t<original.tables.size();++t){const auto& a=original.tables[t];const auto& b=next.tables[t];
        if(b.cached!=-1 || a.descriptors.size()!=b.descriptors.size())return false;
        for(size_t i=0;i<a.descriptors.size();++i)
            if(b.descriptors[i]!=(i==1-a.first?replacement:a.descriptors[i]))return false;}
    return true;
}
inline bool Flushed(const Snapshot& pending,const Snapshot& next) {
    if(!SameAllocation(pending,next) || next.cursor!=pending.cursor+pending.count ||
       next.dirty!=(pending.dirty&~pending.groups) || next.rebind)return false;
    for(size_t i=0;i<pending.tables.size();++i)
        if(pending.tables[i].descriptors!=next.tables[i].descriptors || next.tables[i].cached!=-1)return false;
    return true;
}
}
