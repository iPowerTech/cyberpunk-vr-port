#pragma once
#include "Anim/VehiclePosePolicy.hpp"
#include <cstdint>

namespace cvr::input {
struct VehicleButtons {
    uint16_t owned{};
    bool exitOnX{},confirmOnA{};
};
inline VehicleButtons VehicleButtonPolicy(bool mounted,int state) {
    // Window combat keeps vanilla X reload. B remains protected from an
    // accidental exit; the ordinary seat's X hold and A mirror are suspended.
    if(cvr::anim::IsPassengerWindowCombat(state))return {0x2000,false,false};
    return mounted ? VehicleButtons{0x6000,true,true} : VehicleButtons{};
}
class VehicleExitHold {
public:
    bool Update(bool allowed,bool xHeld,uint64_t now,uint64_t holdMs) {
        if(!allowed || !xHeld) { m_down=false;return false; }
        if(!m_down) { m_down=true;m_since=now; }
        return now>=m_since && now-m_since>=holdMs;
    }
private:
    uint64_t m_since{};
    bool m_down{};
};
}
