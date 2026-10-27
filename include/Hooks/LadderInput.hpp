#pragma once
#include <cstdint>
#include "Runtimes/LadderClimbing.hpp"
struct VRControllerState;
namespace cvr::ladder {
bool Active();
void ReadHandKinds(int* kinds);
void Publish(uintptr_t player,int detailed,bool paused,const Geometry* geometry);
bool PublishTop(uintptr_t player,Vec ladderPosition,Vec origin,Vec right,Vec normal,Vec up);
void UpdateInput(const VRControllerState& controllers);
void ResetInput();
float MixMove(float original,float rawX,float rawY);
float NativeAction();
void ConstrainHand(int side,float* targetModel,float* handRotation,const float* entityPosition,const float* entityRotation);
float DebugValue(int index);
bool SetSimulatorGrip(float left,float right,int milliseconds);
}
