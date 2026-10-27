#pragma once
#include "Render/PairedPassFrontier.hpp"
#include <functional>
#include <iterator>

namespace cvr::stereo::paired_cpu_frontier {
using paired_frontier::Edges;
using paired_frontier::Link;
using graph_snapshot::Field;
enum class Result:uint32_t {Rejected,CandidateNeedsOwnershipAndResourceCompile};
enum class Failure:uint32_t {
    None,Source,SyntheticOwnership,UnprovedPrerequisite,Propagation,CpuCycle,OrderCycle,LiftedCycle,Invariant,Allocation,SharedOwner
};
struct Invariants {
    bool originalCpuAcyclic{},originalOrderAcyclic{},originalLiftedAcyclic{};
    bool candidateCpuAcyclic{},candidateOrderAcyclic{},candidateLiftedAcyclic{};
    bool originalRawUnionAcyclic{},candidateRawUnionAcyclic{};
    bool boundedSyntheticAncestors{},unmovedCpuEdgesPreserved{},barriersPropagated{};
    bool producersBeforeMainDiscard{},noMainPostDiscardToPeerReady{};
    bool sharedOwnerOrderPreserved{};
    bool nativeOwnershipProved{},resourceOrderCompiled{},nativeWaitReceiptsProved{},executionPermitted{};
};
namespace detail {
inline Edges Union(const std::array<Edges,2>& edges){auto result=edges[0];result.insert(edges[1].begin(),edges[1].end());return result;}
// Event 2*id is CPU-ready; 2*id+1 is ordered emission. Native reverse waits are NOT inferred.
inline Edges Lifted(const std::set<uint32_t>& nodes,const std::array<Edges,2>& edges){
    Edges result;for(const auto id:nodes)result.emplace(2*id,2*id+1);
    for(uint32_t phase=0;phase<2;++phase)for(const auto& [from,to]:edges[phase])result.emplace(2*from+phase,2*to+phase);
    return result;
}
inline std::vector<uint32_t> Cycle(const std::set<uint32_t>& nodes,const Edges& edges){
    std::map<uint32_t,uint8_t> state;std::vector<uint32_t> stack,result;
    const std::function<bool(uint32_t)> visit=[&](uint32_t node){
        state[node]=1;stack.push_back(node);
        for(auto edge=edges.lower_bound({node,0});edge!=edges.end() && edge->first==node;++edge){
            if(state[edge->second]==1){const auto begin=std::find(stack.begin(),stack.end(),edge->second);
                result.assign(begin,stack.end());result.push_back(edge->second);return true;}
            if(!state[edge->second] && visit(edge->second))return true;
        }
        stack.pop_back();state[node]=2;return false;
    };
    for(const auto node:nodes)if(!state[node] && visit(node))break;
    return result;
}
inline bool EmptyJoin(const graph_snapshot::Node& node){
    return !node.work && !node.vtable && Field<uint32_t>(node.bytes,8)==4 &&
        !Field<uint32_t>(node.bytes,12) && !Field<uint64_t>(node.bytes,16) &&
        std::all_of(node.definition.begin(),node.definition.end(),[](auto byte){return byte==0;});
}
}

// Source-bound topology only. No native jobs, resource ownership or GPU permission is produced.
class Plan {
public:
    Result BuildCandidate(const paired_frontier::Plan& sourcePlan)try{
        *this=Plan{};failure_=Failure::Source;
        const auto source=sourcePlan.Source();
        if(sourcePlan.State()!=paired_frontier::Result::CandidateNeedsCpuAndResourceCompile || !source ||
            !sourcePlan.Matches(*source,sourcePlan.ImageBase()))return result_;
        source_=std::make_shared<const graph_snapshot::Snapshot>(*source);base_=sourcePlan.ImageBase();
        boundary_=sourcePlan.SelectedBoundary();earlyTail_=sourcePlan.EarlyTail();
        fingerprint_=sourcePlan.SourceFingerprint();original_=sourcePlan.Original();proposed_=sourcePlan.Proposed();
        std::map<uint32_t,const graph_snapshot::Node*> nodes;std::set<uint32_t> ids;
        for(const auto& node:source_->nodes){const auto id=node.handle&0xDFFFFFFFu;ids.insert(id);nodes[id]=&node;}
        originalRawCycle_=detail::Cycle(ids,detail::Union(original_));
        invariants_.originalRawUnionAcyclic=originalRawCycle_.empty();
        invariants_.originalCpuAcyclic=paired_frontier::detail::Acyclic(ids,original_[0]);
        invariants_.originalOrderAcyclic=paired_frontier::detail::Acyclic(ids,original_[1]);
        std::set<uint32_t> events;for(const auto id:ids){events.insert(2*id);events.insert(2*id+1);}
        invariants_.originalLiftedAcyclic=paired_frontier::detail::Acyclic(events,detail::Lifted(ids,original_));
        if(!invariants_.originalCpuAcyclic || !invariants_.originalOrderAcyclic || !invariants_.originalLiftedAcyclic)return result_;
        const auto& main=sourcePlan.EyeRoles()[0];const auto& peer=sourcePlan.EyeRoles()[1];
        early_.insert(peer.prefix.begin(),peer.prefix.end());
        if(boundary_==paired_frontier::Boundary::Discard)early_.insert(peer.discard);
        failure_=Failure::SyntheticOwnership;
        bool changed=true;
        while(changed){changed=false;
            for(const auto& [from,to]:original_[0]){
                if(!early_.contains(to) || early_.contains(from))continue;
                const auto& node=*nodes.at(from);
                if(Field<uint32_t>(node.bytes,24)!=1 || node.work)continue;
                if(!detail::EmptyJoin(node)){witness_={from,to};return result_;}
                changed|=early_.insert(from).second;synthetic_.insert(from);
            }
        }
        invariants_.boundedSyntheticAncestors=true;
        for(const auto& edge:original_[0]){
            if(early_.contains(edge.first) && !early_.contains(edge.second))frontier_.insert(edge.second);
            if(early_.contains(edge.first) || !early_.contains(edge.second))continue;
            incoming_.insert(edge);
            if(edge.first!=main.solidReady &&
                (!paired_frontier::detail::Path(original_[1],main.solidReady,edge.first).empty() ||
                 !paired_frontier::detail::Path(original_[0],main.discard,edge.first).empty()))barriers_.insert(edge.first);
        }
        // Other real prerequisites must already precede MAIN's ready point. They are not moved.
        failure_=Failure::UnprovedPrerequisite;
        for(const auto& [from,to]:incoming_)if(!barriers_.contains(from) && from!=main.solidReady && nodes.at(from)->work &&
            paired_frontier::detail::Path(original_[0],from,main.solidReady).empty() &&
            paired_frontier::detail::Path(original_[1],from,main.solidReady).empty()){
            witness_={from,to};return result_;
        }
        for(const auto& edge:incoming_)if(barriers_.contains(edge.first))proposed_[0].erase(edge);
        failure_=Failure::Propagation;
        for(const auto barrier:barriers_)for(const auto target:frontier_){
            if(boundary_==paired_frontier::Boundary::Discard && target==main.discard)continue;
            if(barrier==target){witness_={barrier,target};return result_;}
            proposed_[0].emplace(barrier,target);
        }
        proposed_[0].emplace(main.solidReady,peer.collector);
        // Source CPU dependencies may let discard run before the solid ready job.
        if(boundary_==paired_frontier::Boundary::Discard)proposed_[0].emplace(peer.solidReady,peer.discard);
        proposed_[0].emplace(earlyTail_,main.discard);
        if(boundary_==paired_frontier::Boundary::Discard){
            failure_=Failure::SharedOwner;
            for(auto a=nodes.begin();a!=nodes.end();++a){
                const auto object=Field<uint64_t>(a->second->bytes,16);if(!object)continue;
                for(auto b=std::next(a);b!=nodes.end();++b){
                    if(Field<uint64_t>(b->second->bytes,16)!=object)continue;
                    if(a->second->vtable!=b->second->vtable || a->second->work!=b->second->work){witness_={a->first,b->first};return result_;}
                    for(const auto edge:{Link{a->first,b->first},Link{b->first,a->first}}){
                        if(paired_frontier::detail::Path(original_[0],edge.first,edge.second).empty())continue;
                        sharedOwnerOrders_.insert(edge);
                        if(edge==Link{main.discard,peer.discard}){
                            if(paired_frontier::detail::Path(proposed_[0],edge.second,edge.first).empty()){witness_={edge.first,edge.second};return result_;}
                            reversedOwnerOrders_.insert(edge);continue;
                        }
                        if(paired_frontier::detail::Path(proposed_[0],edge.first,edge.second).empty()){
                            proposed_[0].insert(edge);restoredOwnerOrders_.insert(edge);
                        }
                    }
                }
            }
            // Node completion includes its native children; pointer equality alone
            // is not a certificate that those children are read-only or disjoint.
            invariants_.sharedOwnerOrderPreserved=std::all_of(sharedOwnerOrders_.begin(),sharedOwnerOrders_.end(),[&](const auto& edge){
                return reversedOwnerOrders_.contains(edge) || !paired_frontier::detail::Path(proposed_[0],edge.first,edge.second).empty();});
            if(!invariants_.sharedOwnerOrderPreserved)return result_;
        }
        for(size_t phase=0;phase<2;++phase){
            std::set_difference(original_[phase].begin(),original_[phase].end(),proposed_[phase].begin(),proposed_[phase].end(),
                std::inserter(removed_[phase],removed_[phase].end()));
            std::set_difference(proposed_[phase].begin(),proposed_[phase].end(),original_[phase].begin(),original_[phase].end(),
                std::inserter(added_[phase],added_[phase].end()));
        }
        failure_=Failure::CpuCycle;witness_=detail::Cycle(ids,proposed_[0]);
        invariants_.candidateCpuAcyclic=witness_.empty();if(!witness_.empty())return result_;
        failure_=Failure::OrderCycle;witness_=detail::Cycle(ids,proposed_[1]);
        invariants_.candidateOrderAcyclic=witness_.empty();if(!witness_.empty())return result_;
        candidateRawCycle_=detail::Cycle(ids,detail::Union(proposed_));
        invariants_.candidateRawUnionAcyclic=candidateRawCycle_.empty();
        failure_=Failure::LiftedCycle;witness_=detail::Cycle(events,detail::Lifted(ids,proposed_));
        invariants_.candidateLiftedAcyclic=witness_.empty();if(!witness_.empty())return result_;
        invariants_.unmovedCpuEdgesPreserved=std::all_of(original_[0].begin(),original_[0].end(),[&](const auto& edge){
            return early_.contains(edge.first) || early_.contains(edge.second) || proposed_[0].contains(edge);});
        invariants_.barriersPropagated=true;
        for(const auto barrier:barriers_)for(const auto target:frontier_)
            if(boundary_!=paired_frontier::Boundary::Discard || target!=main.discard)
                invariants_.barriersPropagated&=proposed_[0].contains({barrier,target});
        invariants_.producersBeforeMainDiscard=!paired_frontier::detail::Path(proposed_[0],main.solidReady,main.discard).empty() &&
            !paired_frontier::detail::Path(proposed_[0],peer.solidReady,earlyTail_).empty() &&
            !paired_frontier::detail::Path(proposed_[0],earlyTail_,main.discard).empty();
        invariants_.noMainPostDiscardToPeerReady=true;
        for(const auto& [id,node]:nodes)if(Field<uint32_t>(node->bytes,24)==0 &&
            (!paired_frontier::detail::Path(original_[1],main.discard,id).empty() ||
             !paired_frontier::detail::Path(original_[0],main.discard,id).empty())){
            const auto path=paired_frontier::detail::Path(proposed_[0],id,earlyTail_);
            if(!path.empty()){invariants_.noMainPostDiscardToPeerReady=false;witness_=path;break;}
        }
        failure_=Failure::Invariant;
        if(!invariants_.unmovedCpuEdgesPreserved || !invariants_.barriersPropagated || !invariants_.producersBeforeMainDiscard ||
            !invariants_.noMainPostDiscardToPeerReady)return result_;
        failure_=Failure::None;result_=Result::CandidateNeedsOwnershipAndResourceCompile;return result_;
    }catch(...){*this=Plan{};failure_=Failure::Allocation;return result_;}

    bool Matches(const graph_snapshot::Snapshot& current,uint64_t imageBase)const{
        return source_ && imageBase==base_ && !current.failure &&
            fingerprint_==paired_frontier::detail::Fingerprint(current,imageBase) && source_->nodes==current.nodes && source_->edges==current.edges;
    }
    Result State()const{return result_;}
    Failure Rejection()const{return failure_;}
    bool ExecutionReady()const{return false;}
    paired_frontier::Boundary SelectedBoundary()const{return boundary_;}
    uint32_t EarlyTail()const{return earlyTail_;}
    uint64_t SourceFingerprint()const{return fingerprint_;}
    uint64_t ImageBase()const{return base_;}
    std::shared_ptr<const graph_snapshot::Snapshot> Source()const{return source_;}
    const std::set<uint32_t>& Early()const{return early_;}
    const std::set<uint32_t>& SyntheticAncestors()const{return synthetic_;}
    const std::set<uint32_t>& LateBarriers()const{return barriers_;}
    const std::set<uint32_t>& FrontierTargets()const{return frontier_;}
    const Edges& Incoming()const{return incoming_;}
    const Edges& SharedOwnerOrders()const{return sharedOwnerOrders_;}
    const Edges& RestoredOwnerOrders()const{return restoredOwnerOrders_;}
    const Edges& ReversedOwnerOrders()const{return reversedOwnerOrders_;}
    const std::array<Edges,2>& Original()const{return original_;}
    const std::array<Edges,2>& Proposed()const{return proposed_;}
    const std::array<Edges,2>& Removed()const{return removed_;}
    const std::array<Edges,2>& Added()const{return added_;}
    const Invariants& Proofs()const{return invariants_;}
    const std::vector<uint32_t>& WitnessPath()const{return witness_;}
    const std::vector<uint32_t>& OriginalRawCycleWitness()const{return originalRawCycle_;}
    const std::vector<uint32_t>& CandidateRawCycleWitness()const{return candidateRawCycle_;}
private:
    Result result_=Result::Rejected;Failure failure_=Failure::None;
    paired_frontier::Boundary boundary_=paired_frontier::Boundary::Solid;
    uint32_t earlyTail_{};
    uint64_t base_{},fingerprint_{};
    std::shared_ptr<const graph_snapshot::Snapshot> source_;
    std::array<Edges,2> original_,proposed_,removed_,added_;
    std::set<uint32_t> early_,synthetic_,barriers_,frontier_;
    Edges incoming_,sharedOwnerOrders_,restoredOwnerOrders_,reversedOwnerOrders_;Invariants invariants_;
    std::vector<uint32_t> witness_,originalRawCycle_,candidateRawCycle_;
};
}
