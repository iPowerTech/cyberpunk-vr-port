#pragma once
#include "Render/BothEyesSceneExpansion.hpp"
#include <bitset>

namespace cvr::stereo::scene_prefix_order {
enum class Failure:uint32_t {None,Source,Identity,Edges,Cycle,Ranks,Reachability,Capacity,Allocation};

// Proposed graph only. Native recording/reconciliation must run again for this
// order; unchanged shader/data dependencies require separate runtime evidence.
class Plan {
public:
    bool Build(const scene_expansion::BothEyesPlan& expansion)try{
        *this={};failure_=Failure::Source;
        if(!expansion.Ready() || expansion.ExecutionPermitted())return false;
        const auto& source=expansion.Expanded();
        failure_=Failure::Capacity;
        if(source.nodes.empty() || source.nodes.size()>MaxNodes)return false;
        failure_=Failure::Source;
        OwnedNativeGraph original;
        if(!original.Create(source,expansion.Edges()) || !original.CompileReference() || !original.SameSourceOrders() ||
            !expansion.MatchesCompiled(original.Snapshot()))return false;
        const std::array<uint32_t,4> handles{expansion.PrefixHandle(1),expansion.TailHandle(1),expansion.PrefixHandle(0),expansion.TailHandle(0)};
        std::array<uint32_t,4> ids{},ordinals{};std::set<uint32_t> unique;
        failure_=Failure::Identity;
        for(size_t i=0;i<handles.size();++i){
            ids[i]=handles[i]&0xDFFFFFFFu;if(!unique.insert(ids[i]).second)return false;
            const auto found=std::find_if(source.nodes.begin(),source.nodes.end(),[&](const auto& node){return node.handle==handles[i];});
            if(found==source.nodes.end() || graph_snapshot::Field<uint32_t>(found->bytes,24)!=(i<2?1u:0u))return false;
            ordinals[i]=found->order;
        }
        auto edges=expansion.Edges();
        failure_=Failure::Edges;
        for(auto& phase:edges){
            if(!phase.contains({ids[0],ids[1]}) || !phase.contains({ids[2],ids[3]}) || phase.erase({ids[1],ids[2]})!=1 ||
                !phase.emplace(ids[0],ids[2]).second || !phase.emplace(ids[2],ids[1]).second || !phase.emplace(ids[1],ids[3]).second)return false;
        }
        failure_=Failure::Cycle;
        OwnedNativeGraph compiled;
        if(!compiled.Create(source,edges) || !compiled.CompileReference())return false;
        auto candidate=compiled.Snapshot();
        failure_=Failure::Identity;
        if(candidate.failure || candidate.nodes.size()!=source.nodes.size())return false;
        for(size_t i=0;i<source.nodes.size();++i){const auto& a=source.nodes[i];const auto& b=candidate.nodes[i];
            if(a.handle!=b.handle || a.order!=b.order || a.vtable!=b.vtable || a.work!=b.work || a.definition!=b.definition ||
                !std::equal(a.bytes.begin(),a.bytes.begin()+28,b.bytes.begin()) || a.bytes[30]!=b.bytes[30] || a.bytes[31]!=b.bytes[31])return false;
        }
        for(uint32_t phase=0;phase<2;++phase){
            failure_=Failure::Ranks;
            const auto peerRank=source.nodes[ordinals[1]].bytes[28+phase];
            const auto mainRank=source.nodes[ordinals[2]].bytes[28+phase];
            if(uint32_t(peerRank)+1!=mainRank)return false;
            for(size_t i=0;i<source.nodes.size();++i){
                const auto expected=i==ordinals[1]?mainRank:i==ordinals[2]?peerRank:source.nodes[i].bytes[28+phase];
                if(candidate.nodes[i].bytes[28+phase]!=expected)return false;
            }
            failure_=Failure::Reachability;
            Reachability before{},after{};
            if(!Closure(source,expansion.Edges()[phase],before) || !Closure(source,edges[phase],after) ||
                !before[ordinals[1]][ordinals[2]] || before[ordinals[2]][ordinals[1]])return false;
            before[ordinals[1]].reset(ordinals[2]);before[ordinals[2]].set(ordinals[1]);
            if(before!=after)return false;
        }
        source_=source;candidate_=std::move(candidate);edges_=std::move(edges);handles_=handles;
        ready_=true;failure_=Failure::None;return true;
    }catch(...){*this={};failure_=Failure::Allocation;return false;}
    bool Ready()const{return ready_;}
    Failure Rejection()const{return failure_;}
    const graph_snapshot::Snapshot& Source()const{return source_;}
    const graph_snapshot::Snapshot& Candidate()const{return candidate_;}
    const std::array<paired_frontier::Edges,2>& Edges()const{return edges_;}
    // Original order: peer prefix, peer tail, MAIN prefix, MAIN tail.
    const std::array<uint32_t,4>& Handles()const{return handles_;}
    bool ExecutionPermitted()const{return false;}
private:
    static constexpr size_t MaxNodes=220;
    using Reachability=std::array<std::bitset<MaxNodes>,MaxNodes>;
    static bool Closure(const graph_snapshot::Snapshot& source,const paired_frontier::Edges& edges,Reachability& result){
        result={};std::map<uint32_t,uint32_t> ordinals;
        for(const auto& node:source.nodes)if(node.order>=source.nodes.size() || !ordinals.emplace(node.handle&0xDFFFFFFFu,node.order).second)return false;
        for(const auto& [from,to]:edges){const auto a=ordinals.find(from),b=ordinals.find(to);
            if(a==ordinals.end() || b==ordinals.end())return false;result[a->second].set(b->second);
        }
        for(size_t k=0;k<source.nodes.size();++k)for(size_t i=0;i<source.nodes.size();++i)
            if(result[i][k])result[i]|=result[k];
        for(size_t i=0;i<source.nodes.size();++i)if(result[i][i])return false;
        return true;
    }
    bool ready_{};Failure failure_=Failure::None;
    graph_snapshot::Snapshot source_,candidate_;
    std::array<paired_frontier::Edges,2> edges_;
    std::array<uint32_t,4> handles_{};
};
}
