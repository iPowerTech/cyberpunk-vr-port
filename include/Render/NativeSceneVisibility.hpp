#pragma once
#include "Render/NativeFineVisibility.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

namespace cvr::stereo {
using NativeSceneRead=bool(*)(const void*,void*,size_t);
using NativeCoarseCull=char(*)(void*,const void*,uint32_t);
struct NativeScenePoolHeader {
    uintptr_t slots{};
    uint32_t size{},slotCount{};
    uintptr_t nodes{};
    uint32_t capacity{},stride{};
};
struct NativeSceneNode {
    uint32_t next{},hash{};
    uint64_t key{};
    std::array<float,6> bounds{};
    uintptr_t candidates{};
    uint32_t capacity{},count{};
};
static_assert(sizeof(NativeScenePoolHeader)==32 && sizeof(NativeSceneNode)==56);
static_assert(offsetof(NativeSceneNode,bounds)==0x10 && offsetof(NativeSceneNode,candidates)==0x28 &&
    offsetof(NativeSceneNode,capacity)==0x30 && offsetof(NativeSceneNode,count)==0x34);
struct NativeSceneBucket {
    alignas(16) std::array<uint8_t,32> bounds{};
    std::vector<NativeCullCandidate> candidates;
    uintptr_t sourceBegin{};
};
// Bounded, owned input for a complete traversal of this one native scene pool.
// Other gather callbacks and material/command preparation are separate domains.
class NativeSceneVisibility {
public:
    class TaggedBatches {
    public:
        struct Batch {
            std::array<uintptr_t,32> tags{};
            uint32_t count{};
        };
        static_assert(offsetof(Batch,count)==0x100);
        // Native 79CB6C clears Batch::count. Submit a mutable value copy per
        // call, never const_cast a stored plan. Its fifth context QWORD is
        // *(scene+0x1C38), not the address of that field.
        TaggedBatches()=default;
        TaggedBatches(const TaggedBatches&)=delete;
        TaggedBatches& operator=(const TaggedBatches&)=delete;
        TaggedBatches(TaggedBatches&& other)noexcept:
            nodes_(std::move(other.nodes_)),candidates_(std::move(other.candidates_)),batches_(std::move(other.batches_)),
            masks_(std::move(other.masks_)),candidateCount_(std::exchange(other.candidateCount_,0)),eyes_(std::exchange(other.eyes_,0)),
            visibleBuckets_(std::exchange(other.visibleBuckets_,std::array<uint32_t,2>{})){}
        TaggedBatches& operator=(TaggedBatches&& other)noexcept{
            if(this!=&other){nodes_=std::move(other.nodes_);candidates_=std::move(other.candidates_);batches_=std::move(other.batches_);
                masks_=std::move(other.masks_);candidateCount_=std::exchange(other.candidateCount_,0);eyes_=std::exchange(other.eyes_,0);
                visibleBuckets_=std::exchange(other.visibleBuckets_,std::array<uint32_t,2>{});}
            return *this;
        }
        std::span<const Batch> Batches()const{return Batches(0);}
        std::span<const Batch> Batches(uint32_t eye)const{
            return eye<eyes_?std::span<const Batch>(batches_[eye]):std::span<const Batch>{};
        }
        uint32_t BucketCount()const{return static_cast<uint32_t>(nodes_.size());}
        uint32_t CandidateCount()const{return candidateCount_;}
        uint32_t EyeCount()const{return eyes_;}
        uint32_t VisibleBuckets()const{return VisibleBuckets(0);}
        uint32_t VisibleBuckets(uint32_t eye)const{return eye<eyes_?visibleBuckets_[eye]:0;}
        // Native input bytes counted once, excluding per-eye plans/container
        // overhead. Mask bit0/bit1 describe eye0/eye1 bucket acceptance,
        // not instance or draw equivalence.
        uint64_t OwnedBytes()const{return uint64_t(nodes_.size())*sizeof(NativeSceneNode)+uint64_t(candidateCount_)*sizeof(NativeCullCandidate);}
        std::span<const uint8_t> BucketMasks()const{return masks_;}
    private:
        friend class NativeSceneVisibility;
        // Tags point inside these allocations. Moving this owner preserves
        // them; replacing it while native descendants use the tags is forbidden.
        std::vector<NativeSceneNode> nodes_;
        std::vector<std::vector<NativeCullCandidate>> candidates_;
        std::array<std::vector<Batch>,2> batches_;
        std::vector<uint8_t> masks_;
        uint32_t candidateCount_{},eyes_{};
        std::array<uint32_t,2> visibleBuckets_{};
    };
    uint32_t failure{},declared{},candidateCount{},bytesOwned{};
    std::vector<NativeSceneBucket> buckets;
    bool Capture(const void* address,NativeSceneRead read) {
        failure=declared=candidateCount=bytesOwned=0;buckets.clear();NativeScenePoolHeader pool{};
        if(!read || !read(address,&pool,sizeof(pool)))return Fail(1);
        declared=pool.size;
        if(pool.size>pool.capacity || pool.capacity>1048576 || pool.slotCount>1048576 ||
            pool.stride<sizeof(NativeSceneNode) || pool.stride>4096)return Fail(2);
        if(pool.size && (!pool.slots || !pool.nodes || !pool.slotCount))return Fail(2);
        if(!pool.size)return true;
        std::vector<uint32_t> heads(pool.slotCount);std::vector<bool> seen(pool.capacity);
        if(!read(reinterpret_cast<void*>(pool.slots),heads.data(),heads.size()*4))return Fail(3);
        for(auto index:heads)while(index!=UINT32_MAX){
            if(index>=pool.capacity || seen[index] || buckets.size()>=pool.size)return Fail(4);
            seen[index]=true;const auto offset=uint64_t(index)*pool.stride;
            if(pool.nodes>UINTPTR_MAX-offset)return Fail(4);
            NativeSceneNode node{};
            if(!read(reinterpret_cast<void*>(pool.nodes+offset),&node,sizeof(node)))return Fail(5);
            if(node.count>node.capacity || node.count>MaxFineCandidates || candidateCount+node.count>1048576 ||
                (node.count && !node.candidates))return Fail(6);
            for(size_t axis=0;axis<3;++axis)
                if(!std::isfinite(node.bounds[axis]) || !std::isfinite(node.bounds[axis+3]) || node.bounds[axis]>node.bounds[axis+3])return Fail(7);
            NativeSceneBucket bucket;
            bucket.sourceBegin=node.candidates;
            // Match the native unaligned loads at node+0x10 and node+0x1C,
            // including unused W lanes; culling consumes XYZ only.
            const auto* raw=reinterpret_cast<const uint8_t*>(&node);
            std::memcpy(bucket.bounds.data(),raw+0x10,16);std::memcpy(bucket.bounds.data()+16,raw+0x1C,16);
            bucket.candidates.resize(node.count);
            if(node.count && !read(reinterpret_cast<void*>(node.candidates),bucket.candidates.data(),node.count*40ull))return Fail(8);
            candidateCount+=node.count;bytesOwned+=32+node.count*40;buckets.push_back(std::move(bucket));index=node.next;
        }
        if(buckets.size()!=pool.size)return Fail(9);
        NativeScenePoolHeader after{};
        if(!read(address,&after,sizeof(after)) || std::memcmp(&after,&pool,sizeof(pool)))return Fail(10);
        return true;
    }
    // Own native-shaped buckets independently of this capture. This builds
    // queue inputs only; writer/proxy leases, stable query/occlusion read inputs
    // and native execution admission remain the caller's responsibility.
    bool BuildTaggedBatches(NativeCoarseCull coarse,void* query,TaggedBatches& output)const{
        return BuildTaggedBatchesImpl(coarse,{&query,1},output);
    }
    bool BuildTaggedBatches(NativeCoarseCull coarse,const std::array<void*,2>& queries,TaggedBatches& output)const{
        return BuildTaggedBatchesImpl(coarse,queries,output);
    }
private:
    bool BuildTaggedBatchesImpl(NativeCoarseCull coarse,std::span<void* const> queries,TaggedBatches& output)const try {
        if(failure || !coarse || queries.empty() || queries.size()>2 ||
            std::any_of(queries.begin(),queries.end(),[](void* query){return !query;}) ||
            buckets.size()!=declared || buckets.size()>1048576)return false;
        uint32_t count{};
        for(const auto& bucket:buckets){
            if(bucket.candidates.size()>MaxFineCandidates || bucket.candidates.size()>1048576-count)return false;
            for(size_t axis=0;axis<3;++axis){float low{},high{};
                std::memcpy(&low,bucket.bounds.data()+axis*4,4);
                std::memcpy(&high,bucket.bounds.data()+16+axis*4,4);
                if(!std::isfinite(low) || !std::isfinite(high) || low>high)return false;
            }
            count+=static_cast<uint32_t>(bucket.candidates.size());
        }
        if(count!=candidateCount)return false;
        TaggedBatches next;next.candidateCount_=count;next.eyes_=static_cast<uint32_t>(queries.size());
        next.nodes_.resize(buckets.size());next.candidates_.resize(buckets.size());next.masks_.resize(buckets.size());
        for(uint32_t eye=0;eye<next.eyes_;++eye)
            next.batches_[eye].reserve((std::min)(buckets.size(),(buckets.size()+31)/32+size_t(count)/256+1));
        for(size_t i=0;i<buckets.size();++i){
            auto& node=next.nodes_[i];const auto& bucket=buckets[i];
            next.candidates_[i]=bucket.candidates;
            node.next=UINT32_MAX;
            std::memcpy(node.bounds.data(),bucket.bounds.data(),12);
            std::memcpy(node.bounds.data()+3,bucket.bounds.data()+16,12);
            node.candidates=reinterpret_cast<uintptr_t>(next.candidates_[i].data());
            node.count=node.capacity=static_cast<uint32_t>(next.candidates_[i].size());
        }
        std::array<TaggedBatches::Batch,2> batch{};std::array<uint32_t,2> acceptedCandidates{};
        for(size_t i=0;i<next.nodes_.size();++i){const auto& node=next.nodes_[i];
            alignas(16) std::array<uint8_t,32> bounds{};
            const auto* raw=reinterpret_cast<const uint8_t*>(&node);
            std::memcpy(bounds.data(),raw+0x10,16);std::memcpy(bounds.data()+16,raw+0x1C,16);
            const auto pointer=reinterpret_cast<uintptr_t>(raw+0x10);
            if(pointer&3)return false;
            for(uint32_t eye=0;eye<next.eyes_;++eye){
                const auto result=static_cast<uint8_t>(coarse(queries[eye],bounds.data(),1));
                if(result>2)return false;
                if(!result)continue;
                next.masks_[i]|=uint8_t(1u<<eye);
                auto& current=batch[eye];current.tags[current.count++]=pointer|result;++next.visibleBuckets_[eye];
                acceptedCandidates[eye]+=node.count;
                if(current.count==32 || acceptedCandidates[eye]>=256){
                    next.batches_[eye].push_back(current);current={};acceptedCandidates[eye]=0;
                }
            }
        }
        for(uint32_t eye=0;eye<next.eyes_;++eye)if(batch[eye].count)next.batches_[eye].push_back(batch[eye]);
        output=std::move(next);return true;
    }catch(...){return false;}
public:
    bool Evaluate(NativeCoarseCull coarse,NativeFineCull fine,void* query,std::vector<uint64_t>& ids,uint32_t& visibleBuckets)const {
        ids.clear();visibleBuckets=0;
        if(failure || !coarse || !fine || !query)return false;
        std::vector<uint64_t> batch;
        for(const auto& bucket:buckets){
            const auto result=static_cast<uint8_t>(coarse(query,bucket.bounds.data(),1));
            if(result>2)return false;
            if(!result)continue;
            ++visibleBuckets;
            if(!RunNativeFine(fine,query,bucket.candidates,result==1,batch))return false;
            ids.insert(ids.end(),batch.begin(),batch.end());
        }
        return true;
    }
    // Compare precisely the bytes consumed by the hash-verified coarse/fine
    // tests. Allocations, unused AABB W lanes and candidate+36 are not inputs.
    std::array<uint32_t,10> CandidateDifferences(const NativeSceneVisibility& other)const {
        std::array<uint32_t,10> diff{};diff[0]=failure || other.failure || buckets.size()!=other.buckets.size();
        diff[1]=candidateCount!=other.candidateCount;
        for(size_t i=0;i<(std::min)(buckets.size(),other.buckets.size());++i){
            const auto& a=buckets[i];const auto& b=other.buckets[i];
            diff[2]+=a.sourceBegin!=b.sourceBegin;
            for(size_t lane=0;lane<8;++lane)if(std::memcmp(a.bounds.data()+lane*4,b.bounds.data()+lane*4,4))++diff[lane==3 || lane==7?4:3];
            diff[5]+=a.candidates.size()!=b.candidates.size();
            for(size_t j=0;j<(std::min)(a.candidates.size(),b.candidates.size());++j){
                const auto& x=a.candidates[j];const auto& y=b.candidates[j];
                diff[6]+=x.id!=y.id;diff[7]+=std::memcmp(x.bounds.data(),y.bounds.data(),24)!=0;
                diff[8]+=x.includeMask!=y.includeMask || x.excludeMask!=y.excludeMask;diff[9]+=x.reserved!=y.reserved;
            }
        }
        return diff;
    }
    bool SameCandidates(const NativeSceneVisibility& other)const {
        const auto diff=CandidateDifferences(other);
        for(auto field:{0u,1u,3u,5u,6u,7u,8u})if(diff[field])return false;
        return true;
    }
private:
    bool Fail(uint32_t code){failure=code;return false;}
};
}
