#pragma once
#include "Utils/XrMath.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cvr::anim {
// A MOTORCYCLE'S TWIST GRIP. The right hand on the bars turns the throttle as on a real bike: the grip rolled towards the
// rider (top of the hand back, wrist back) opens it. Measured about the recenter space's right axis, the same axis the
// handlebar steering takes for the bars (TrackedWheelSteering): a controller held along the bar or upright reads the
// same roll, and pushing and pulling the bars, which turns the hand about the vertical, is not counted.
// Zero is how the controller lay when it took hold of the bars; rolled the other way past it is the closed stop.
inline constexpr float BikeThrottleDeadDeg=3.f;   // wrist tremor opens nothing
inline constexpr float BikeThrottleFullMin=15.f;
inline constexpr float BikeThrottleFullMax=60.f;
inline constexpr float BikeThrottleFullDefault=30.f;
inline float BikeThrottleFullDegrees(float value) {
    return std::isfinite(value) && value>0 ? std::clamp(value,BikeThrottleFullMin,BikeThrottleFullMax):BikeThrottleFullDefault;
}
// Degrees `now` has turned about the space's +X since `ref` (positive: up goes back, the grip opening).
inline float TwistAboutRight(const XrQuaternionf& ref,const XrQuaternionf& now) {
    auto r=MultiplyQuat(now,ConjugateQuat(ref));
    if(r.w<0){r.x=-r.x;r.w=-r.w;}
    return 2.f*std::atan2(r.x,r.w)*57.295779513f;
}
inline float BikeThrottleFor(float degrees,float full) {
    if(!std::isfinite(degrees))return 0;
    full=BikeThrottleFullDegrees(full);
    return std::clamp((degrees-BikeThrottleDeadDeg)/(full-BikeThrottleDeadDeg),0.f,1.f);
}

class BikeTwistThrottle {
public:
    void Reset() { *this={}; }
    float Degrees() const { return m_degrees; }
    // held: the right hand has the bars. hand: its orientation in the recenter space, when `valid`.
    // A lost controller closes the throttle and keeps the zero; a new tracking space takes a new zero.
    float Update(bool held,bool valid,XrQuaternionf hand,uint64_t origin,float full) {
        if(!held){Reset();return 0;}
        const float norm=hand.x*hand.x+hand.y*hand.y+hand.z*hand.z+hand.w*hand.w;
        if(!valid || !std::isfinite(norm) || norm<.5f || norm>1.5f){m_degrees=0;return 0;}
        const float scale=1/std::sqrt(norm);
        hand={hand.x*scale,hand.y*scale,hand.z*scale,hand.w*scale};
        if(!m_zeroed || origin!=m_origin){m_zero=hand;m_zeroed=true;m_origin=origin;m_degrees=0;return 0;}
        m_degrees=TwistAboutRight(m_zero,hand);
        return BikeThrottleFor(m_degrees,full);
    }
private:
    XrQuaternionf m_zero{0,0,0,1};
    uint64_t m_origin{};
    float m_degrees{};
    bool m_zeroed{};
};
}
