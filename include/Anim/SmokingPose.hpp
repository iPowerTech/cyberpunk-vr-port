#pragma once

// ================================================================================================
// SmokingPose: called from the pose-apply detour, each already guarded there.
// See src/Anim/SmokingPose.cpp for why the guards stayed behind.
// ================================================================================================

#include "Anim/VrikState.hpp"

#include <cstdint>

namespace cvr {
namespace anim {

void VrikSmokingCigPose(uint8_t* boneBuf);
void VrikSmokingLighterPose(uint8_t* boneBuf);

}  // namespace anim
}  // namespace cvr
