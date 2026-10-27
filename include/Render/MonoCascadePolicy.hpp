#pragma once
#include <cstdint>

namespace cvr::cascades {
struct Allocation {
    uintptr_t heap{};
    uint64_t offset{},bytes{};
    bool known{},committed{};
    bool CouldAlias(const Allocation& other) const {
        if(known && committed)return false;
        if(!known || !other.known)return true;
        if(other.committed || heap!=other.heap)return false;
        if(!bytes || !other.bytes || offset+bytes<offset || other.offset+other.bytes<other.offset)return true;
        return offset<other.offset+other.bytes && other.offset<offset+bytes;
    }
};
struct Atlas {
    uintptr_t resource{};
    uint32_t width{},height{},format{},slices{},mips{},mip{},first{},count{};
    uint32_t Mask() const {return count && count<=8 && first+count<=8?((1u<<count)-1)<<first:0;}
    bool Valid() const {return resource && width && height && width<=16384 && height<=16384 && slices<=8 && mips==1 && mip==0 && Mask() && first+count<=slices;}
    bool SameLayout(const Atlas& a) const {return width==a.width && height==a.height && format==a.format && slices==a.slices && mips==a.mips && mip==a.mip;}
    bool SameResource(const Atlas& a) const {return resource==a.resource && SameLayout(a);}
};
// A source preparation starts a new lease. MAIN can consume it exactly once.
// No state from a previous view pair survives a new preparation or invalidation.
struct Policy {
    uint64_t cycle{};
    Atlas atlas{};
    uint32_t active{},complete{},camera{},clearBits{},claimed{};
    bool clear{},mainSampling{},consumed{},reusing{},invalid{};
    uint64_t Begin(uint32_t count) {
        const auto next=cycle+1;*this={};cycle=next;active=count>=1 && count<=4?count:0;return cycle;
    }
    void Invalidate(){invalid=true;}
    bool SourceClear(const Atlas& a,uint32_t bits) {
        if(!active || !a.Valid() || a.first || a.count<active || clear){Invalidate();return false;}
        atlas=a;clearBits=bits;clear=true;invalid=false;return true;
    }
    void Camera(unsigned index,bool stable) {if(index<active && stable)camera|=1u<<index;else Invalidate();}
    void Complete(unsigned index,bool valid){if(index<active && valid)complete|=1u<<index;else Invalidate();}
    void MainSampling(uint32_t count,bool matching){mainSampling=matching && count==active;if(!mainSampling)Invalidate();}
    bool Ready() const {const auto mask=active?(1u<<active)-1:0;return mask && clear && !invalid && complete==mask && camera==mask && mainSampling && !consumed;}
    bool MainClear(const Atlas& a,uint32_t bits) {
        const bool match=Ready() && atlas.SameLayout(a) && atlas.first==a.first && atlas.count==a.count && bits==clearBits;
        consumed=true;reusing=match;return match;
    }
    bool Reuse(const Atlas& a,unsigned index) const {
        return reusing && !invalid && index<active && a.Valid() && atlas.SameResource(a) && a.Mask()==(1u<<index);
    }
    bool Claim(unsigned index) {
        if(!reusing || index>=active || (claimed&(1u<<index)))return false;
        claimed|=1u<<index;return true;
    }
};
}
