#pragma once
#include <algorithm>

namespace cvr::vrik {
// The same posture is used for camera calibration and the live bone solve.
// Concentrate the rounding in the upper thoracic chain, keep the pelvis upright.
inline float SpineFlex(int index,int count) {
    const float t=count>1 ? std::clamp(float(index)/float(count-1),0.0f,1.0f):0;
    return -.209439510f*t*t; // 12 degrees at the top
}
inline constexpr float NeckFlex=-.104719755f;
inline constexpr float UpperNeckFlex=-.034906585f;
inline constexpr float ChestRetraction=.02f; // model-back, from the anatomical pivot
}
