#pragma once
#include <algorithm>
#include <cmath>

namespace cvr::roomscale {
// Body-only follow: enter at the outer radius, settle at the centre, then
// re-arm the whole free zone. Head/camera tracking is never filtered here.
class BodyFollowZone {
public:
    void Reset() { m_following=false;m_previousRadius=0; }
    bool Following() const { return m_following; }
    float Fraction(float distance,float radius,float dt) {
        if(!std::isfinite(distance) || !std::isfinite(radius) || !std::isfinite(dt) || dt<=0) {
            Reset();return 0;
        }
        if(radius<=0) { Reset();return 1; }
        const bool widened=radius>m_previousRadius;
        m_previousRadius=radius;
        // Looking down may widen the cone during an already active turn.
        // Let a newly admitted head angle hold the chest/belt still immediately.
        if(widened && distance<=radius) { m_following=false;return 0; }
        if(!m_following && distance<=radius)return 0;
        m_following=true;
        const float fraction=-std::expm1(-std::min(dt,.25f)/.025f);
        if(distance*(1-fraction)<=radius*.05f) { Reset();return 1; }
        return fraction;
    }
private:
    bool m_following{};
    float m_previousRadius{};
};
}
