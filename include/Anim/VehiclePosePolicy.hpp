#pragma once
#include <RED4ext/Scripting/Natives/Generated/game/PSMVehicle.hpp>

namespace cvr::anim {
inline bool IsDrivingVehicleState(int state) {
    return state==static_cast<int>(RED4ext::game::PSMVehicle::Driving) ||
           state==static_cast<int>(RED4ext::game::PSMVehicle::DriverCombat);
}
inline bool IsPassengerWindowCombat(int state) {
    return state==static_cast<int>(RED4ext::game::PSMVehicle::Combat);
}
}
