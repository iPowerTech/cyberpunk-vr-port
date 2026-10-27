#pragma once
#include "Render/PairedPassFrontier.hpp"
#include <span>

namespace cvr::stereo::context_order {
using graph_snapshot::Field;
using Read=graph_snapshot::Read;
enum class Failure:uint32_t {None,Source,Classification,Edges,Ranks,CpuCycle,UnpublishedPrefix,Allocation};
struct Classification {
    uint32_t handle{},kind{},methodRva{};
    bool operator==(const Classification&)const=default;
};
struct Witness {uint32_t producer{},consumer{},producerSlot{},consumerSlot{},kind{};};
struct Result {
    Failure failure=Failure::Source;
    uint64_t fingerprint{};
    uint32_t publishers{},synchronizers{},requiredPairs{},provedPairs{};
    std::vector<Classification> classes;
    std::vector<Witness> missing;
    // Only CPU readiness for native prefix consumption; not resource/alias safety.
    bool PrefixReady()const{return failure==Failure::None;}
};

inline bool Classify(const graph_snapshot::Node& node,uint64_t base,Read read,Classification& out){
    out={node.handle&0xDFFFFFFFu,0,0};
    if(!node.work && !node.vtable)return
        !Field<uint64_t>(node.bytes,16) &&
        std::all_of(node.definition.begin(),node.definition.end(),[](uint8_t byte){return !byte;});
    if(!base || !read || node.vtable<base || node.vtable-base>UINT32_MAX ||
        node.vtable>UINT64_MAX-0x30 || Field<uint64_t>(node.definition,0)!=node.vtable)return false;
    uint64_t method{};
    if(!read(node.vtable+0x28,&method,8) || method<base || method-base>UINT32_MAX)return false;
    struct Profile {uint32_t vtable,work;};
    constexpr Profile zero[]{
        {0x2B39DF0,0xC26DB8},{0x2B39F10,0xB2BEFC},{0x312D070,0xB4C3CC},
        {0x312D280,0x21CDC8},{0x312D310,0xB79578},{0x312D340,0x14A700},
        {0x312D3A0,0x611830},{0x312D3D0,0xBF35A4},{0x312D430,0x374AD8},
        {0x312D880,0x378E68},{0x312DCD0,0xCBB23C},{0x312DD90,0x79B03C},
        {0x312E180,0x76ECC4}};
    const auto vtable=node.vtable-base;
    out.methodRva=uint32_t(method-base);
    std::array<uint8_t,6> bytes{};
    if(vtable==0x2B3A008 && node.work==base+0x1EC1D0 && out.methodRva==0x18E4120){
        out.kind=2;
        return read(method,bytes.data(),6) && bytes==std::array<uint8_t,6>{0xB8,2,0,0,0,0xC3};
    }
    if(vtable==0x2B3A038 && node.work==base+0x58DA9C && out.methodRva==0x18E46C0){
        out.kind=3;
        return Field<uint32_t>(node.definition,24)<=2 && read(method,bytes.data(),6) &&
            bytes==std::array<uint8_t,6>{0xB8,3,0,0,0,0xC3};
    }
    const bool known=std::any_of(std::begin(zero),std::end(zero),[&](const auto& profile){
        return vtable==profile.vtable && node.work==base+profile.work;});
    return known && out.methodRva==0x18D9C80 && read(method,bytes.data(),3) &&
        bytes[0]==0x33 && bytes[1]==0xC0 && bytes[2]==0xC3;
}

// 1EC404 publishes type-2 contexts only in execution mode. 58DC98 consumes and
// clears EVERY slot through its phase-1 rank, even an as-yet unpublished slot.
// A phase-1 DAG alone cannot prove these producers finished on the CPU.
inline Result Analyze(const graph_snapshot::Snapshot& graph,uint64_t base,Read read)try{
    Result result;
    if(graph.failure || graph.nodes.empty() || graph.nodes.size()>220 || graph.edges.size()>65536 ||
        !base || base>UINT64_MAX-0x312E1B0 || !read)return result;
    std::map<uint32_t,const graph_snapshot::Node*> nodes;
    std::set<uint32_t> ids;
    std::array<std::set<uint32_t>,2> ranks;
    for(size_t i=0;i<graph.nodes.size();++i){const auto& node=graph.nodes[i];const auto id=node.handle&0xDFFFFFFFu;
        if(id>=65536 || node.order!=i || Field<uint32_t>(node.bytes,0)!=i ||
            !ids.insert(id).second || Field<uint32_t>(node.bytes,24)>1)return result;
        for(size_t phase=0;phase<2;++phase)if(node.bytes[28+phase]>=graph.nodes.size() ||
            !ranks[phase].insert(node.bytes[28+phase]).second){result.failure=Failure::Ranks;return result;}
        Classification classification;
        if(!Classify(node,base,read,classification)){result.failure=Failure::Classification;return result;}
        result.publishers+=classification.kind==2;result.synchronizers+=classification.kind==3;
        result.classes.push_back(classification);nodes.emplace(id,&node);
    }
    std::array<paired_frontier::Edges,2> edges;
    std::set<uint32_t> edgeIds;
    result.failure=Failure::Edges;
    for(size_t i=0;i<graph.edges.size();++i){const auto& row=graph.edges[i];
        const auto phase=Field<uint32_t>(row.bytes,4),from=Field<uint32_t>(row.bytes,8)&0xDFFFFFFFu,
            to=Field<uint32_t>(row.bytes,12)&0xDFFFFFFFu;
        if(row.order!=i || Field<uint32_t>(row.bytes,0)!=i || phase>1 ||
            !edgeIds.insert(row.handle&0xBFFFFFFFu).second || !nodes.contains(from) || !nodes.contains(to) ||
            from==to || !edges[phase].emplace(from,to).second)return result;
        if(nodes.at(from)->bytes[28+phase]>=nodes.at(to)->bytes[28+phase]){result.failure=Failure::Ranks;return result;}
    }
    if(!paired_frontier::detail::Acyclic(ids,edges[0])){result.failure=Failure::CpuCycle;return result;}
    result.fingerprint=paired_frontier::detail::Fingerprint(graph,base);
    for(const auto& producer:result.classes)if(producer.kind==2 || producer.kind==3){
        const auto slot=nodes.at(producer.handle)->bytes[29];
        std::set<uint32_t> reachable{producer.handle};
        std::vector<uint32_t> pending{producer.handle};
        for(size_t i=0;i<pending.size();++i)for(auto edge=edges[0].lower_bound({pending[i],0});
            edge!=edges[0].end() && edge->first==pending[i];++edge)
            if(reachable.insert(edge->second).second)pending.push_back(edge->second);
        for(const auto& consumer:result.classes)if(consumer.kind==3){
            const auto end=nodes.at(consumer.handle)->bytes[29];if(slot>=end)continue;
            ++result.requiredPairs;
            if(reachable.contains(consumer.handle))++result.provedPairs;
            else result.missing.push_back({producer.handle,consumer.handle,slot,end,producer.kind});
        }
    }
    result.failure=result.missing.empty()?Failure::None:Failure::UnpublishedPrefix;
    return result;
}catch(...){Result result;result.failure=Failure::Allocation;return result;}
}
