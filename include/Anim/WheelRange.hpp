#pragma once
#include <algorithm>
#include <cmath>

namespace cvr::anim {
inline constexpr float WheelFullLockMin=30.f;
inline constexpr float WheelFullLockMax=180.f;
inline constexpr float WheelFullLockDefault=180.f;

// Physical hand travel from the grab reference to one full steering lock.
// This normalizes input only; cabin wheel travel is authored in vehicle graphs.
inline float WheelFullLockDegrees(float value) {
    return std::isfinite(value) && value>0
        ? std::clamp(value,WheelFullLockMin,WheelFullLockMax):WheelFullLockDefault;
}
inline float WheelFullLockForVehicle(float value,bool car) {
    // Cars get the extended range. Bikes/other mounts retain the previous
    // valid range and 90-degree fallback, with no doubled handlebar animation.
    return car ? WheelFullLockDegrees(value):
        (std::isfinite(value)&&value>=30.f&&value<=120.f ? value:90.f);
}
}
