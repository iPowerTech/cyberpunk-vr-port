#pragma once
#include "Runtimes/LadderClimbing.hpp"

namespace cvr::ladder {
inline void CorrectRungWrist(int side,Vec rungAxis,Vec ladderUp,Vec& wrist,Rotation& rotation) {
    if(side<0 || side>1 || !Normalize(rungAxis) || !Normalize(ladderUp))return;
    // A rail's native wrist sits above the grip when rotated onto a rung.
    // Keep the approved25deg pitch, then apply the 2cm downward grip
    // calibration in ladder axes, independent of head/wrist/body orientation.
    const Vec palm=PalmOffset(side),contact=wrist+rotation.Rotate(palm);
    constexpr float halfAngle=25.0f*.00872664626f;
    const float s=std::sin(halfAngle);
    const Rotation pitch{rungAxis.x*s,rungAxis.y*s,rungAxis.z*s,std::cos(halfAngle)};
    rotation=pitch*rotation;rotation.Normalize();
    wrist=contact-rotation.Rotate(palm)-ladderUp*.02f;
}
}
