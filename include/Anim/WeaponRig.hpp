#pragma once

// ================================================================================================
// The weapon rig's three passes. Called from the pose-apply detour, each already guarded by its own
// condition there -- see src/Anim/WeaponRig.cpp for why the guards stayed behind.
// ================================================================================================

#include "Anim/VrikState.hpp"

#include <cstdint>

namespace cvr {
namespace anim {

void WeaponRigIdentifyAndWrite(void** a1, void** a2, unsigned int a4, void* poseDesc,
                               uint8_t* boneBuf, uintptr_t trackBuf);
void WeaponRigCensusNote(unsigned int a4, uintptr_t trackBuf);
void WeaponRigCaptureParts(uint8_t* boneBuf);

}  // namespace anim
}  // namespace cvr
