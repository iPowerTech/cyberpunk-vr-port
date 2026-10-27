#pragma once
#include <cmath>
#include <cstdint>

namespace cvr::markers {
struct MarkerOwner {uintptr_t root{};float depth{};};
// A flattened effect can carry one exact widget owner. Mixed overlays must
// retain their individual projections; never guess by screen-space bounds.
inline bool MergeEffectOwner(MarkerOwner& common,MarkerOwner next) {
    if(!next.root || !std::isfinite(next.depth) || next.depth<=0)return false;
    if(common.root && common.root!=next.root)return false;
    if(!common.root)common=next;
    return true;
}
}
