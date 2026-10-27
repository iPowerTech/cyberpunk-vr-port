#pragma once
#include <cstdint>
namespace cvr::camera {
struct MainAimPose {float position[3]{},rotation[4]{};uint64_t poseId{};};
// Completed MAIN pose, centred between the eyes. A single snapshot must supply
// both origin and orientation of the native vehicle targeting query.
bool ReadMainAimPose(MainAimPose& out);
// Fresh world-space gaze from the completed external MAIN camera. No new XR
// locate and no change to the game camera's native orbit/base.
bool ReadExternalMainDirection(float out[3],uint64_t* poseId);
}
