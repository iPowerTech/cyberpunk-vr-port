#pragma once
#include "Render/PairedCpuFrontier.hpp"
#include "Render/NativeContextOrder.hpp"

namespace cvr::stereo::cpu_overlap {
enum class Failure:uint32_t {None,Source,Classification,SharedOwner,Cycle,Invariant,Allocation};
// Test-only CPU topology candidate. The original GPU order/resource schedule stays
// intact; native job/definition ownership still has to be certified by a caller.
class Plan {
public:
    bool Build(const paired_frontier::Plan& source,graph_snapshot::Read read)try{
        *this=Plan{};
        if(source.SelectedBoundary()!=paired_frontier::Boundary::Solid || !source.Source() ||
            source.State()!=paired_frontier::Result::CandidateNeedsCpuAndResourceCompile ||
            !source.Matches(*source.Source(),source.ImageBase()))return false;
        const auto classification=context_order::Analyze(*source.Source(),source.ImageBase(),read);
        if(!classification.PrefixReady()){failure_=Failure::Classification;return false;}
        paired_cpu_frontier::Plan candidate;
        if(candidate.BuildCandidate(source)!=paired_cpu_frontier::Result::CandidateNeedsOwnershipAndResourceCompile)return false;
        original_=source.Original();proposed_=candidate.Proposed();proposed_[1]=original_[1];
        source_=source.Source();base_=source.ImageBase();fingerprint_=source.SourceFingerprint();
        std::map<uint32_t,const graph_snapshot::Node*> nodes;std::set<uint32_t> ids;
        for(const auto& node:source_->nodes){const auto id=node.handle&0xDFFFFFFFu;nodes[id]=&node;ids.insert(id);}
        // Preserve all originally ordered accesses to an identical native object,
        // including its child jobs. No intentional reversal is accepted here.
        for(auto a=nodes.begin();a!=nodes.end();++a){const auto object=graph_snapshot::Field<uint64_t>(a->second->bytes,16);if(!object)continue;
            for(auto b=std::next(a);b!=nodes.end();++b){if(graph_snapshot::Field<uint64_t>(b->second->bytes,16)!=object)continue;
                if(a->second->vtable!=b->second->vtable || a->second->work!=b->second->work){failure_=Failure::SharedOwner;witness_={a->first,b->first};return false;}
                for(const auto edge:{paired_frontier::Link{a->first,b->first},paired_frontier::Link{b->first,a->first}}){
                    if(paired_frontier::detail::Path(original_[0],edge.first,edge.second).empty())continue;
                    shared_.insert(edge);
                    if(paired_frontier::detail::Path(proposed_[0],edge.first,edge.second).empty()){proposed_[0].insert(edge);restored_.insert(edge);}
                }
            }
        }
        // The native synchronizer consumes every context before its original GPU
        // rank, regardless of whether that publisher has finished on the CPU.
        for(const auto& producer:classification.classes)if(producer.kind==2 || producer.kind==3)
            for(const auto& consumer:classification.classes)if(consumer.kind==3 && nodes.at(producer.handle)->bytes[29]<nodes.at(consumer.handle)->bytes[29]){
                const paired_frontier::Link edge{producer.handle,consumer.handle};prefix_.insert(edge);
                if(paired_frontier::detail::Path(proposed_[0],edge.first,edge.second).empty())proposed_[0].insert(edge);
            }
        witness_=paired_cpu_frontier::detail::Cycle(ids,proposed_[0]);
        if(!witness_.empty()){failure_=Failure::Cycle;return false;}
        const auto& main=source.EyeRoles()[0];const auto& peer=source.EyeRoles()[1];
        ready_={main.solidReady,peer.solidReady,main.discard};
        if(proposed_[1]!=original_[1] ||
            paired_frontier::detail::Path(proposed_[0],ready_[0],ready_[2]).empty() ||
            paired_frontier::detail::Path(proposed_[0],ready_[1],ready_[2]).empty() ||
            std::any_of(original_[0].begin(),original_[0].end(),[&](const auto& edge){return !candidate.Early().contains(edge.first) &&
                !candidate.Early().contains(edge.second) && !proposed_[0].contains(edge);})){failure_=Failure::Invariant;return false;}
        for(uint32_t phase=0;phase<2;++phase){
            std::set_difference(original_[phase].begin(),original_[phase].end(),proposed_[phase].begin(),proposed_[phase].end(),std::inserter(removed_[phase],removed_[phase].end()));
            std::set_difference(proposed_[phase].begin(),proposed_[phase].end(),original_[phase].begin(),original_[phase].end(),std::inserter(added_[phase],added_[phase].end()));
        }
        failure_=Failure::None;return true;
    }catch(...){failure_=Failure::Allocation;return false;}
    bool Ready()const{return source_ && failure_==Failure::None;}
    bool Matches(const graph_snapshot::Snapshot& value,uint64_t base)const{return Ready() && base==base_ && !value.failure &&
        value.nodes==source_->nodes && value.edges==source_->edges && fingerprint_==paired_frontier::detail::Fingerprint(value,base);}
    Failure Rejection()const{return failure_;}
    uint64_t Fingerprint()const{return fingerprint_;}
    const auto& Proposed()const{return proposed_;}
    const auto& Original()const{return original_;}
    const auto& Added()const{return added_;}
    const auto& Removed()const{return removed_;}
    const auto& SharedOrders()const{return shared_;}
    const auto& RestoredOrders()const{return restored_;}
    const auto& PrefixOrders()const{return prefix_;}
    const auto& Witness()const{return witness_;}
    const auto& ReadyNodes()const{return ready_;}
private:
    Failure failure_=Failure::Source;uint64_t base_{},fingerprint_{};
    std::shared_ptr<const graph_snapshot::Snapshot> source_;
    std::array<paired_frontier::Edges,2> original_,proposed_,added_,removed_;
    paired_frontier::Edges shared_,restored_,prefix_;std::vector<uint32_t> witness_;std::array<uint32_t,3> ready_{};
};
}
