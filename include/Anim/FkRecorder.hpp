#pragma once

// The animated-skeleton publish for the reload recorder. Guarded by g_VRRecordFK at the call site
// in src/Hooks/AnimPose.cpp -- see src/Anim/FkRecorder.cpp.

#include "Anim/VrikState.hpp"

#include <cstdint>

namespace cvr {
namespace anim {

void VrikPublishAnimatedFk(uint8_t* boneBuf);

}  // namespace anim
}  // namespace cvr
