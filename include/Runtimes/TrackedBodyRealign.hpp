#pragma once
#include "Runtimes/RoomscaleMovement.hpp"   // HeadYaw
#include "Runtimes/TrackedBodyYaw.hpp"
#include "Utils/XrMath.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cvr::body {
// THE HEAD AND BOTH HANDS POINTING ONE WAY ARE THE BODY'S FRONT. The torso estimate (TrackedBodyYaw) is relative:
// it adds up the turns it can prove from the hands and never looks back at the head. A turn it missed -- the way
// back after a physical turn, with the controllers coming round together with the head -- stayed in the body for
// good: head and controllers faced ahead again, the body stayed turned, and so did the walk (2026-10-28). When the
// head and both controllers have agreed on a heading for a moment and the body is off it, the body is brought
// round to the head at a steady rate; the estimate then starts again from there.
class TrackedBodyRealign {
public:
    enum class Result { Idle, Turning, Done };
    static constexpr float AgreeDeg=25,OffDeg=15,DoneDeg=2,HoldSeconds=.35f,Rate=2.6f;   // rad/s, about 150 deg/s
    void Reset() { *this={}; }
    // headYaw, body: the follower's frame (the head's yaw relative to the view, the body's offset from it).
    // handOff: each controller's pointing yaw minus the head's, from one tracking frame (HandOffsets).
    // `target` is written while it owns the body: the next angle, and on Done the head's.
    Result Step(float headYaw,const float handOff[2],bool valid,float body,uint64_t stampUs,float& target) {
        const float dt=m_stamp && stampUs>m_stamp && stampUs-m_stamp<250000 ? float(stampUs-m_stamp)*1e-6f : 0;
        m_stamp=stampUs;
        const bool agree=valid && std::isfinite(headYaw) && std::isfinite(body) &&
            std::abs(Wrap(handOff[0]))<AgreeDeg*Deg && std::abs(Wrap(handOff[1]))<AgreeDeg*Deg;
        const float off=agree ? Wrap(headYaw-body) : 0;
        if(!m_turning) {
            m_agree=agree && std::abs(off)>OffDeg*Deg ? m_agree+dt : 0;
            if(m_agree<HoldSeconds)return Result::Idle;
            m_turning=true;
        }
        // A hand or the head going its own way ends the turn where the body is.
        if(!agree) { m_turning=false;m_agree=0;return Result::Idle; }
        if(std::abs(off)<=DoneDeg*Deg) { m_turning=false;m_agree=0;target=headYaw;return Result::Done; }
        target=Wrap(body+std::clamp(off,-Rate*dt,Rate*dt));
        return Result::Turning;
    }
    // Each controller's pointing yaw minus the head's, both from the one frame; false for a hand untracked, or
    // the head or a hand pointing straight up or down.
    static bool HandOffsets(const TrackingFrame& frame,float out[2]) {
        float head{};
        if(!frame.valid || !cvr::roomscale::HeadYaw(frame.head.x,frame.head.y,frame.head.z,frame.head.w,&head))
            return false;
        for(int h=0;h<2;++h) {
            if(!frame.tracked[h])return false;
            const auto q=MultiplyQuat(frame.head,frame.rotations[h]);
            float yaw{};
            if(!cvr::roomscale::HeadYaw(q.x,q.y,q.z,q.w,&yaw))return false;
            out[h]=Wrap(yaw-head);
        }
        return true;
    }
private:
    static constexpr float Deg=.01745329252f;
    static float Wrap(float a) { return std::remainder(a,6.28318530718f); }
    float m_agree{};
    uint64_t m_stamp{};
    bool m_turning=false;
};
}
