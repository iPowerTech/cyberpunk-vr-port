#pragma once
#include <cstdint>
// Shared gameplay flags must update in both FPP and generic TPP serializers.
void RefreshPlayerCameraState();
// Identity only. A caller may compare an engine-owned vehicle against it, but
// must not dereference or retain this remembered address as an owning handle.
uintptr_t CurrentMountedVehicleIdentity();
