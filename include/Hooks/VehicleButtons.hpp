#pragma once
#include "Anim/VehiclePosePolicy.hpp"
#include <cstdint>

namespace cvr::input {
struct VehicleButtons {
    uint16_t owned{};
    uint16_t exitHold{};   // held for CyberpunkVR_VehicleExitHoldMs, this button leaves the car (as B)
    bool confirmOnA{},autodriveOnR3{};
};
inline VehicleButtons VehicleButtonPolicy(bool mounted,int state) {
    // Seated, X is nobody's but the game's: Vehicle_Horn, and Choice1 in a conversation. The exit is
    // the game's own B, owned so a brush of it at speed does nothing and re-emitted only once held.
    // R3 is the autopilot: Vehicle_Autodrive sits on L3, which here is the D-pad modifier and half of
    // the radio chord, so it would be one slip away from taking the car over.
    // Window combat keeps vanilla X reload and R3; B still exits only when held.
    if(cvr::anim::IsPassengerWindowCombat(state))return {0x2000,0x2000,false,false};
    return mounted ? VehicleButtons{0x2000|0x0080,0x2000,false,true} : VehicleButtons{};
}
class VehicleExitHold {
public:
    bool Update(bool allowed,bool held,uint64_t now,uint64_t holdMs) {
        if(!allowed || !held) { m_down=false;return false; }
        if(!m_down) { m_down=true;m_since=now; }
        return now>=m_since && now-m_since>=holdMs;
    }
private:
    uint64_t m_since{};
    bool m_down{};
};
}
