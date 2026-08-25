#pragma once

// ================================================================================================
// ReloadPose: called from the pose-apply detour, each already guarded there.
// See src/Anim/ReloadPose.cpp for why the guards stayed behind.
// ================================================================================================

#include "Anim/VrikState.hpp"

#include <cstdint>

namespace cvr {
namespace anim {

// The resting left hand: captures it while unarmed, replays it while armed. Must run BEFORE
// VrikReloadFingerPose so a preview blends out of the resting fingers, not over them.
void VrikRestFingerPose(uint8_t* boneBuf);
// Disk half of the same feature, called from the frame loop: never from the pose path.
void RestFingerTick();
void VrikReloadFingerPose(uint8_t* boneBuf);

}  // namespace anim
}  // namespace cvr
