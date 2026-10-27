#pragma once
#include <cmath>

namespace cvr::input {
// Hysteresis is shared by semi-auto, full-auto and charge weapons. A held
// value remains held; it is never converted into a repeating pulse train.
struct WeaponTrigger {
    static constexpr float Break=.85f,Reset=.20f;
    bool active{},down{},ready{};
    bool Update(bool enabled,float value) {
        if(!enabled || !std::isfinite(value)) {active=down=ready=false;return false;}
        if(!active) {active=true;down=false;ready=value<=Reset;}
        if(value<=Reset) {down=false;ready=true;}
        else if(ready && value>=Break) {down=true;ready=false;}
        return down;
    }
};
bool WeaponTriggerEnabled();
}
