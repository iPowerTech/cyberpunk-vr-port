#pragma once
#include "Camera/AnchorTranslation.hpp"

namespace cvr::camera {
// Neutral rig geometry only: no animated feet, current head rotation, previous
// bake, or rendered-camera feedback enters the camera mount.
inline AnchorVector NeckCameraOffset(AnchorVector neutralNeck,float eyeHeight,
                                     AnchorVector referenceCamera,bool female=false) {
    return {neutralNeck.x-referenceCamera.x,
            neutralNeck.y+(female ? .10f:.15f)-referenceCamera.y,eyeHeight+.10f-referenceCamera.z};
}
inline AnchorVector NeckCameraEyeOffset(AnchorVector neutralNeck,AnchorVector neutralEyes,bool female=false) {
    return NeckCameraOffset(neutralNeck,neutralEyes.z,neutralEyes,female);
}
bool ReadNeckCameraMount(AnchorVector* offset);
bool ReadNeckCameraEyeOffset(AnchorVector* offset);
}
