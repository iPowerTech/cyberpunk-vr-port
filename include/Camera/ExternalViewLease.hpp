#pragma once
#include <cstdint>

namespace cvr::camera {
struct ExternalViewKey {
    uintptr_t playerCamera{},eyeCamera{};
    uint64_t origin{};
    bool operator==(const ExternalViewKey&) const = default;
};
template<class T> struct ExternalViewLease {
    T value{};
    ExternalViewKey key{};
    uint64_t stamp{};
    void Clear(){stamp=0;}
    void Publish(const T& next,ExternalViewKey owner,uint64_t now){value=next;key=owner;stamp=now;}
    bool Read(ExternalViewKey owner,uint64_t now,T& output) const {
        // MAIN owns the lifetime, exactly like the device-camera handoff. A
        // missing update never selects the player's cabin camera. This is a
        // value snapshot, not a retained/dereferenced engine camera pointer.
        // Explicit FPP/device transitions clear it; owner/origin changes reject it.
        if(!stamp || !(owner==key) || now<stamp)return false;
        output=value;return true;
    }
};
}
