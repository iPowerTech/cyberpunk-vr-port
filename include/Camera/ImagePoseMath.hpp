#pragma once
#include "Utils/XrMath.hpp"

namespace cvr::camera {
inline XrPosef RebaseImageEyePose(const XrPosef& previousHead,const XrPosef& previousEye,const XrPosef& imageHead) {
    const auto local=RelativePose(previousHead,previousEye);
    const auto offset=RotateVector(imageHead.orientation,local.position);
    return {MultiplyQuat(imageHead.orientation,local.orientation),
        {imageHead.position.x+offset.x,imageHead.position.y+offset.y,imageHead.position.z+offset.z}};
}
}
