#pragma once
#include <cstdint>
#include <map>
#include <vector>

namespace cvr::stereo::constants {
// A bounded graph of witnessed queue operations. Fences are usable only while
// one GPU queue signals monotonically above the initial completed value.
class GpuQueueOrder {
public:
    struct Reach {bool before{},pending{};};
    static constexpr uint32_t Capacity=32768;
    uint32_t Submit(uint64_t queue){return Append(queue,0,0,true);}
    void Fence(uint64_t fence,uint64_t completed) {
        if(!fence || fences_.contains(fence))return;
        if(fences_.size()>=Capacity){failed_=true;return;}
        fences_.emplace(fence,FenceState{completed,completed,0,completed});cache_.clear();
    }
    void Signal(uint64_t queue,uint64_t fence,uint64_t value) {
        const auto point=Append(queue,0,0);auto found=fences_.find(fence);
        if(found==fences_.end()){failed_=true;return;}
        auto& state=found->second;
        if(!point || value<=state.highest || (state.owner && state.owner!=queue)){state.invalid=true;return;}
        state.owner=queue;state.highest=value;state.signals.emplace(value,point);
    }
    void Wait(uint64_t queue,uint64_t fence,uint64_t value){Append(queue,fence,value);}
    void Invalidate(uint64_t fence){const auto found=fences_.find(fence);if(found!=fences_.end())found->second.invalid=true;cache_.clear();}
    void Progress(uint64_t fence,uint64_t completed) {
        const auto found=fences_.find(fence);if(found==fences_.end())return;
        if(found->second.completed!=completed){found->second.completed=completed;cache_.clear();}
    }
    Reach Reaches(uint32_t from,uint32_t target) const {
        if(failed_ || !from || !target || from>=nodes_.size() || target>=nodes_.size())return {};
        const uint64_t key=(uint64_t(from)<<32)|target;
        if(const auto found=cache_.find(key);found!=cache_.end())return found->second;
        std::vector<uint32_t> stack{from};std::vector<bool> visited(nodes_.size());Reach result;
        while(!stack.empty()) {
            const auto id=stack.back();stack.pop_back();if(!id || visited[id])continue;visited[id]=true;
            if(id==target){result.before=true;result.pending=false;break;}
            const auto& node=nodes_[id];if(node.previous)stack.push_back(node.previous);
            stack.insert(stack.end(),node.completed.begin(),node.completed.end());
            if(!node.fence)continue;
            const auto found=fences_.find(node.fence);
            if(found==fences_.end()){result.pending=true;continue;}
            const auto& fence=found->second;
            if(fence.invalid || node.value<=fence.baseline)continue;
            const auto signal=fence.signals.find(node.value);
            // A higher observed signal is not proof: a lower pre-observation
            // signal may already have satisfied this wait. Completion of such
            // an unknown producer ends Pending, without inventing an edge.
            if(signal==fence.signals.end())result.pending|=fence.completed<node.value;
            else stack.push_back(signal->second);
        }
        if(cache_.size()<65536)cache_.emplace(key,result);
        return result;
    }
    bool Failed() const{return failed_;}
    void Clear(){nodes_.assign(1,{});last_.clear();fences_.clear();cache_.clear();failed_=false;}
private:
    struct Node {uint32_t previous{};uint64_t fence{},value{};std::vector<uint32_t> completed;};
    struct FenceState {uint64_t baseline{},highest{},owner{},completed{};bool invalid{};std::map<uint64_t,uint32_t> signals;};
    std::vector<Node> nodes_{1};
    std::map<uint64_t,uint32_t> last_;
    std::map<uint64_t,FenceState> fences_;
    mutable std::map<uint64_t,Reach> cache_;
    bool failed_{};
    uint32_t Append(uint64_t queue,uint64_t fence,uint64_t value,bool importCompleted=false) {
        cache_.clear();
        if(failed_ || !queue || nodes_.size()>=Capacity){failed_=true;return 0;}
        Node node{last_[queue],fence,value};
        if(importCompleted)for(const auto& [_,observed]:fences_) {
            if(observed.invalid || observed.completed<=observed.baseline)continue;
            auto signal=observed.signals.upper_bound(observed.completed);
            if(signal!=observed.signals.begin()){--signal;node.completed.push_back(signal->second);}
        }
        const auto id=uint32_t(nodes_.size());nodes_.push_back(std::move(node));last_[queue]=id;return id;
    }
};
}
