#pragma once
#include <cmath>
#include <cstdint>
#include "Render/WorldMarkerReprojection.hpp"

namespace cvr::markers {
struct MarkerOwner {uintptr_t root{};float depth{};SourceProjection source{};MarkerAnchor anchor{};};
// A flattened effect can carry one exact widget owner. Mixed overlays must
// retain their individual projections; never guess by screen-space bounds.
inline bool MergeEffectOwner(MarkerOwner& common,MarkerOwner next) {
    if(!next.root || !std::isfinite(next.depth) || next.depth<=0)return false;
    if(common.root && common.root!=next.root)return false;
    if(!common.root)common=next;
    else if(std::memcmp(&common.source,&next.source,sizeof(SourceProjection)) || common.anchor.valid!=next.anchor.valid ||
            std::memcmp(common.anchor.world,next.anchor.world,sizeof(next.anchor.world)) ||
            std::memcmp(common.anchor.canvas,next.anchor.canvas,sizeof(next.anchor.canvas))) {common.source={};common.anchor={};}
    return true;
}
}
