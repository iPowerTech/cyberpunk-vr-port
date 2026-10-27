#pragma once
#include "Anim/ScenePolicy.hpp"

namespace cvr::roomscale {
// Follow the VRIK scene setting instead of imposing a second Tier1-only gate.
inline bool IsVrikPoseTierAllowed(int tier,int suspendTier) {
    return tier >= static_cast<int>(RED4ext::GameplayTier::Tier1_FullGameplay) &&
           tier <= static_cast<int>(RED4ext::GameplayTier::Tier5_Cinematic) &&
           !cvr::anim::ShouldSuspendVrik(tier,suspendTier);
}

inline bool IsVrikBodyMovementAllowed(int bindingMode,bool headAimActive,bool inVehicle,
                                     int tier,int suspendTier) {
    return !inVehicle && cvr::anim::IsControllerVrikEnabled(bindingMode,headAimActive) &&
           IsVrikPoseTierAllowed(tier,suspendTier);
}
}
