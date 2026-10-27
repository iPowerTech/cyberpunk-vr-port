#pragma once
#include <cstdint>
#include <limits>

namespace cvr::detail {
struct VrcamRenderRect {
    int32_t left{},top{},right{},bottom{};
    bool Empty() const {return left>=right || top>=bottom;}
    bool operator==(const VrcamRenderRect&) const = default;
};

struct PendingVrcamViewport {
    // 2.31: +4E4FCC copies RectCompute's output into view+14. The earlier
    // +4E3E82 call only computes dimensions and must not consume this override.
    static constexpr uintptr_t CommitCallerRva=0x4E4FCC;
    static constexpr uintptr_t InputOffset=0x21C0;
    uintptr_t input{};
    uint32_t width{},height{};

    bool Matches(uintptr_t source,uintptr_t callerRva) const {
        return input && source==input && callerRva==CommitCallerRva;
    }
    bool Apply(uintptr_t source,uintptr_t callerRva,const VrcamRenderRect& bounds,VrcamRenderRect& output) {
        if(!Matches(source,callerRva))return false;
        const auto w=width,h=height;
        *this={};
        constexpr auto limit=static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
        if(w>limit || h>limit)return false;
        if(w && h)output={0,0,static_cast<int32_t>(w),static_cast<int32_t>(h)};
        else {
            // First setup computes view+44/+4C only AFTER committing the rect.
            // Use the native destination bounds while those prior-frame sizes
            // are still zero, otherwise RT cannot find any drawable view.
            if(bounds.Empty())return false;
            output=bounds;
        }
        return true;
    }
};
}
