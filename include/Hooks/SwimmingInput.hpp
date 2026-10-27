#pragma once
#include <cstdint>
#include <openxr/openxr.h>
struct VRControllerState;
namespace cvr::swimming {
struct Input { float forward{};bool water{};float ascend{};bool dive{};bool boost{}; };
// Water posture is independent of the gesture switch and controller tracking.
bool Active();
void BindNativeInput(void* locomotion);
float NativeAction(int action);
bool ReadSteering(XrVector3f* direction,int* state);
void PublishState(uintptr_t player,int state,int fast,bool paused,bool waterContext);
void ResetInput();
Input UpdateInput(const VRControllerState& controllers,bool manualBack);
bool SprintInput(bool wanted);
float DebugValue(int index);
}
