#pragma once
#include "Runtimes/RoomscaleMovement.hpp"

namespace cvr::roomscale {
// Identity comes from GetPlayer in the existing camera-state refresh, never
// from proximity to the camera. No runtime addresses survive a player change.
void SetPlayer(uintptr_t player);
uintptr_t PlayerIdentity();
bool IsPlayerMovement(const void* state);
bool IsPlayerProvider(const void* provider);
void ObserveMovement(void* state);
bool GameplayAllowed();
bool PhysicalBodyEnabled(); // VRIK/scene ownership, independent of the pause menu
bool PhysicalBodyHeadingOwned(); // retains same-frame view cancellation during rig rebinding
bool PhysicalBodyAllowed();
bool PoseFrameAllowed();
Vec2 CameraConsumed(uint64_t origin);
Vec2 CameraConsumed();
}
