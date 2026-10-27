#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace cvr::stereo::scene_access {
inline constexpr uint32_t MaxEventsPerSection=8192,MaxResources=8192;
inline constexpr uint64_t MaxComparisons=1048576;
inline constexpr uint32_t NoEvent=UINT32_MAX;

struct Recording {
    uint64_t list{},nativeGeneration{},token{},context{};
    uint32_t view{},node=UINT32_MAX;
    uint64_t consumer{},epoch{},pair{};
    bool operator==(const Recording&)const=default;
};
// Numeric kinds match native allocation observations; no native owner or GPU
// access permission is implied by this value-only description.
enum class AllocationKind:uint32_t {Unknown,Committed,Placed,Reserved};
struct Allocation {
    uint64_t heap{},offset{},bytes{};
    AllocationKind kind{};
    bool operator==(const Allocation&)const=default;
};
enum class Dimension:uint32_t {Unknown,Buffer,Texture};
struct Resource {
    uint64_t pointer{},cookie{};
    Allocation allocation;
    Dimension dimension{};
    uint64_t bytes{};
    uint32_t subresources{};
    bool operator==(const Resource&)const=default;
};
enum class RangeKind:uint32_t {Unknown,BufferBytes,TextureSubresources,WholeTexture};
struct Range {
    RangeKind kind{};
    uint64_t offset{},bytes{};
    uint32_t firstSubresource{},subresources{};
    bool operator==(const Range&)const=default;
};
enum class Kind:uint32_t {Read,Write,ReadWrite,Transition,Alias,Unknown};
enum class Usage:uint32_t {
    Other,Color,Depth,ShaderRead,ShaderUav,Counter,Constant,Vertex,Index,
    Indirect,Copy,Clear,CpuUpload,Query,StreamOutput,Predication,Vrs,Count
};
struct Event {
    Recording recording;
    uint64_t ordinal{};
    Kind kind=Kind::Unknown;
    Usage usage{};
    Resource resource;
    Range range;
};
enum CoverageBit:uint32_t {
    GpuCommands=1u<<0,ShaderOperands=1u<<1,CpuUploadWrites=1u<<2,
    DescriptorMutations=1u<<3,IndirectEffects=1u<<4,QueryEffects=1u<<5,
    StreamOutputEffects=1u<<6,ChildBoundaries=1u<<7,NativeRecordings=1u<<8,
    PhysicalAllocations=1u<<9
};
inline constexpr uint32_t RequiredCoverage=(1u<<10)-1;
struct Coverage {
    uint32_t mask{},eventCount{};
    uint64_t firstOrdinal{};
    bool closed{},whole{};
};
struct Section {
    Recording recording;
    std::span<const Event> events;
    Coverage coverage;
};
enum class Failure:uint32_t {
    None,Recording,Capacity,EventIdentity,EventOrder,ResourceIdentity,
    Allocation,Range,Usage,Transition,Alias,Unknown,Coverage,Conflict,Exception
};
enum Hazard:uint32_t {
    MainWritesPeerReads=1u<<0,MainWritesPeerWrites=1u<<1,PeerWritesMainReads=1u<<2
};
struct Analysis {
    Failure failure{};
    uint32_t peerEvent=NoEvent,mainEvent=NoEvent,hazards{};
    uint64_t comparisons{},overlaps{},conflicts{},resources{};
    std::array<uint64_t,2> reads{},writes{};
    std::array<std::array<uint64_t,std::size_t(Usage::Count)>,2> usages{};
    bool completeEvidence{},noObservedConflicts{};
    bool Ready()const{return failure==Failure::None && completeEvidence && noObservedConflicts;}
    bool ExecutionPermitted()const{return false;}
};
namespace detail {
inline bool ValidRecording(const Recording& value,uint32_t view){
    const auto node=value.node&0xDFFFFFFFu;
    return value.list && value.nativeGeneration && value.token && value.context && value.view==view && node<65536 &&
        (value.node==node || value.node==(node|0x20000000u)) && value.consumer && value.epoch && value.pair;
}
inline bool Reads(Kind kind){return kind==Kind::Read || kind==Kind::ReadWrite;}
inline bool Writes(Kind kind){return kind==Kind::Write || kind==Kind::ReadWrite;}
inline bool KnownAllocation(const Allocation& value){
    if(value.kind==AllocationKind::Committed)return !value.heap && !value.offset && value.bytes!=UINT64_MAX;
    return value.kind==AllocationKind::Placed && value.heap && value.bytes && value.bytes!=UINT64_MAX && value.offset<=UINT64_MAX-value.bytes;
}
inline bool ValidResource(const Resource& value){
    if(!value.pointer || !value.cookie)return false;
    if(value.dimension==Dimension::Buffer)return value.bytes && value.bytes!=UINT64_MAX && value.subresources==1;
    return value.dimension==Dimension::Texture && !value.bytes && value.subresources && value.subresources<=65536;
}
inline bool ValidRange(const Resource& resource,const Range& range){
    if(resource.dimension==Dimension::Buffer)
        return range.kind==RangeKind::BufferBytes && range.bytes && range.offset<=resource.bytes &&
            range.bytes<=resource.bytes-range.offset && !range.firstSubresource && !range.subresources;
    if(range.kind==RangeKind::WholeTexture)
        return !range.offset && !range.bytes && !range.firstSubresource && !range.subresources;
    return range.kind==RangeKind::TextureSubresources && !range.offset && !range.bytes && range.subresources &&
        range.firstSubresource<resource.subresources && range.subresources<=resource.subresources-range.firstSubresource;
}
inline bool Intersects(uint64_t a,uint64_t aBytes,uint64_t b,uint64_t bBytes){
    return a<b+bBytes && b<a+aBytes;
}
inline bool Overlap(const Event& peer,const Event& main){
    if(peer.resource.pointer==main.resource.pointer){
        if(peer.resource.dimension==Dimension::Buffer)
            return Intersects(peer.range.offset,peer.range.bytes,main.range.offset,main.range.bytes);
        if(peer.range.kind==RangeKind::WholeTexture || main.range.kind==RangeKind::WholeTexture)return true;
        return Intersects(peer.range.firstSubresource,peer.range.subresources,main.range.firstSubresource,main.range.subresources);
    }
    const auto& a=peer.resource.allocation;const auto& b=main.resource.allocation;
    if(a.kind==AllocationKind::Committed || b.kind==AllocationKind::Committed || a.heap!=b.heap)return false;
    // Different placed resources may interpret the same storage differently.
    // Do not project their logical buffer/subresource ranges into physical bytes.
    return Intersects(a.offset,a.bytes,b.offset,b.bytes);
}
inline bool Complete(const Section& section){
    return section.coverage.mask==RequiredCoverage && section.coverage.closed && section.coverage.whole &&
        section.coverage.eventCount==section.events.size();
}
}

// The caller must authenticate every identity, coverage bit and range against
// the same retained native recordings. A coverage bit means fully observed OR
// proved absent, including indirect, query and stream-output effects. This pure
// analysis neither authenticates those receipts nor grants execution permission.
inline Analysis Analyze(const Section& peerTail,const Section& mainPrefix)try{
    Analysis output;
    const auto fail=[&](Failure why,uint32_t peer=NoEvent,uint32_t main=NoEvent){
        output.failure=why;output.peerEvent=peer;output.mainEvent=main;
        output.completeEvidence=false;output.noObservedConflicts=false;return output;
    };
    const auto& a=peerTail.recording;const auto& b=mainPrefix.recording;
    if(!detail::ValidRecording(a,1) || !detail::ValidRecording(b,0) || a.consumer!=b.consumer || a.epoch!=b.epoch || a.pair!=b.pair ||
        a.list==b.list || a.token==b.token || a.context==b.context || (a.node&0xDFFFFFFFu)==(b.node&0xDFFFFFFFu))
        return fail(Failure::Recording);
    if(peerTail.events.size()>MaxEventsPerSection || mainPrefix.events.size()>MaxEventsPerSection)return fail(Failure::Capacity);
    const std::array<const Section*,2> sections{&peerTail,&mainPrefix};
    std::map<uint64_t,Resource> pointers;
    std::map<uint64_t,uint64_t> cookies;
    std::vector<uint32_t> mainWrites;
    for(uint32_t side=0;side<sections.size();++side){const auto& section=*sections[side];
        if(section.coverage.firstOrdinal>UINT64_MAX-section.events.size())return fail(Failure::EventOrder);
        for(uint32_t i=0;i<section.events.size();++i){const auto& event=section.events[i];
            const auto reject=[&](Failure why){return fail(why,side?NoEvent:i,side?i:NoEvent);};
            if(event.recording!=section.recording)return reject(Failure::EventIdentity);
            if(event.ordinal!=section.coverage.firstOrdinal+i)return reject(Failure::EventOrder);
            if(event.kind==Kind::Transition)return reject(Failure::Transition);
            if(event.kind==Kind::Alias)return reject(Failure::Alias);
            if(event.kind!=Kind::Read && event.kind!=Kind::Write && event.kind!=Kind::ReadWrite)return reject(Failure::Unknown);
            if(uint32_t(event.usage)>=uint32_t(Usage::Count))return reject(Failure::Usage);
            if(!detail::ValidResource(event.resource))return reject(Failure::ResourceIdentity);
            if(!detail::KnownAllocation(event.resource.allocation) ||
                (event.resource.dimension==Dimension::Buffer && event.resource.allocation.bytes &&
                    event.resource.bytes>event.resource.allocation.bytes))return reject(Failure::Allocation);
            if(!detail::ValidRange(event.resource,event.range))return reject(Failure::Range);
            const auto [pointer,added]=pointers.emplace(event.resource.pointer,event.resource);
            if(!added && pointer->second!=event.resource)return reject(Failure::ResourceIdentity);
            const auto [cookie,newCookie]=cookies.emplace(event.resource.cookie,event.resource.pointer);
            if(!newCookie && cookie->second!=event.resource.pointer)return reject(Failure::ResourceIdentity);
            if(pointers.size()>MaxResources)return reject(Failure::Capacity);
            output.reads[side]+=detail::Reads(event.kind);output.writes[side]+=detail::Writes(event.kind);
            ++output.usages[side][std::size_t(event.usage)];
            if(side && detail::Writes(event.kind))mainWrites.push_back(i);
        }
    }
    output.resources=pointers.size();
    const auto compare=[&](uint32_t p,uint32_t m){
        const auto& peer=peerTail.events[p];const auto& main=mainPrefix.events[m];
        if(output.comparisons==MaxComparisons)return false;
        ++output.comparisons;
        if(!detail::Overlap(peer,main))return true;
        ++output.overlaps;
        uint32_t hazards{};
        if(detail::Writes(main.kind) && detail::Reads(peer.kind))hazards|=MainWritesPeerReads;
        if(detail::Writes(main.kind) && detail::Writes(peer.kind))hazards|=MainWritesPeerWrites;
        if(detail::Writes(peer.kind) && detail::Reads(main.kind))hazards|=PeerWritesMainReads;
        if(hazards){
            if(!output.conflicts){output.peerEvent=p;output.mainEvent=m;}
            output.hazards|=hazards;++output.conflicts;
        }
        return true;
    };
    for(uint32_t p=0;p<peerTail.events.size();++p){
        if(detail::Writes(peerTail.events[p].kind)){
            for(uint32_t m=0;m<mainPrefix.events.size();++m)if(!compare(p,m))return fail(Failure::Capacity,p,m);
        }else for(const auto m:mainWrites)if(!compare(p,m))return fail(Failure::Capacity,p,m);
    }
    output.completeEvidence=detail::Complete(peerTail) && detail::Complete(mainPrefix);
    output.noObservedConflicts=!output.conflicts;
    output.failure=output.conflicts?Failure::Conflict:output.completeEvidence?Failure::None:Failure::Coverage;
    return output;
}catch(...){Analysis output;output.failure=Failure::Exception;return output;}
}
