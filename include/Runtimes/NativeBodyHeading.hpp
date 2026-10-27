#pragma once
#include <cmath>
#include <cstdint>

namespace cvr::body {
// The locomotion adapter owns the persistent body quaternion. This separate
// tracking reference only consumes native turns, never a requested body turn.
// Readers do not reconstruct it by subtracting an estimated turn from a camera.
class NativeBodyHeading {
public:
    static float Wrap(float yaw) { return std::remainder(yaw, 6.28318530718f); }
    void Reset() { *this = {}; }
    bool Matches(uintptr_t owner, uint64_t origin) const {
        return m_valid && owner && owner == m_owner && origin == m_origin;
    }
    bool OwnedBy(uintptr_t owner) const {return m_valid && owner && owner==m_owner;}
    bool Observe(uintptr_t owner, uint64_t origin, float bodyYaw) {
        if (!owner || !std::isfinite(bodyYaw)) return false;
        if (!Matches(owner, origin)) {
            if(!m_valid || owner!=m_owner)m_centreYaw=Wrap(bodyYaw);
            m_owner = owner; m_origin = origin; m_view = Wrap(bodyYaw); m_valid = true;
        }
        // A climb or another controller can turn the body. It is still a body
        // turn, not view input: observing it must never drag the tracking frame.
        m_body = Wrap(bodyYaw);
        return true;
    }
    void Commit(float bodyYaw, float nativeTurn) {
        if (!m_valid || !std::isfinite(bodyYaw) || !std::isfinite(nativeTurn)) return;
        m_view = Wrap(m_view + nativeTurn);
        m_body = Wrap(bodyYaw);
    }
    float View() const { return m_view; }
    float Body() const { return m_body; }
    float CentreYaw() const { return m_centreYaw; }
    float Offset() const { return Wrap(m_body - m_view); }
private:
    uintptr_t m_owner{};
    uint64_t m_origin{};
    float m_view{}, m_body{}, m_centreYaw{};
    bool m_valid{};
};
}
