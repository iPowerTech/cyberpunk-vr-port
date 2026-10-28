#pragma once
#include "Runtimes/RoomscaleMovement.hpp"   // HeadYaw
#include "Runtimes/TrackedBodyYaw.hpp"
#include "Utils/XrMath.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cvr::body {
// THE HEAD AND THE HANDS ON ONE SIDE ARE THE BODY'S FRONT. The torso estimate (TrackedBodyYaw) is relative: it adds
// up the turns it can prove, and only a turn of head and hands together proves one. The way back made in two goes --
// the head first, then the hands, or the other way round -- reads as a glance and a gesture, and the angle stayed in
// the body: turn right with both, come back in two goes, and the body was still right; repeated, it went round and
// round (2026-10-28). Where the hands are says where the torso faces: when that and the head agree for a moment and
// the body is off them, the body is brought round to the head at a steady rate, and the estimate starts again there.
class TrackedBodyRealign {
public:
    enum class Result { Idle, Turning, Done };
    static constexpr float AgreeDeg=25,OffDeg=15,DoneDeg=2,HoldSeconds=.35f,Rate=2.6f;   // rad/s, about 150 deg/s
    void Reset() { *this={}; }
    // headYaw, body: the follower's frame (the head's yaw relative to the view, the body's offset from it).
    // handsOff: where the hands put the torso's front, minus the head's yaw, from one tracking frame (HandsOffset).
    // `target` is written while it owns the body: the next angle, and on Done the head's.
    Result Step(float headYaw,float handsOff,bool valid,float body,uint64_t stampUs,float& target) {
        const float dt=m_stamp && stampUs>m_stamp && stampUs-m_stamp<250000 ? float(stampUs-m_stamp)*1e-6f : 0;
        m_stamp=stampUs;
        const bool agree=valid && std::isfinite(headYaw) && std::isfinite(body) && std::isfinite(handsOff) &&
            std::abs(Wrap(handsOff))<AgreeDeg*Deg;
        const float off=agree ? Wrap(headYaw-body) : 0;
        if(!m_turning) {
            m_agree=agree && std::abs(off)>OffDeg*Deg ? m_agree+dt : 0;
            if(m_agree<HoldSeconds)return Result::Idle;
            m_turning=true;
        }
        // The head or the hands going their own way end the turn where the body is.
        if(!agree) { m_turning=false;m_agree=0;return Result::Idle; }
        if(std::abs(off)<=DoneDeg*Deg) { m_turning=false;m_agree=0;target=headYaw;return Result::Done; }
        target=Wrap(body+std::clamp(off,-Rate*dt,Rate*dt));
        return Result::Turning;
    }
    // The torso's front from the hands' POSITIONS about the neck (the estimate's own 10 cm down, 8 cm back lever):
    // the way to the point between them when they are in front, the normal of the line through them when they hang
    // at the sides. Minus the head's yaw; false when the hands say nothing (both close in under the neck).
    static bool HandsOffset(const TrackingFrame& frame,float& out) {
        float head{};
        if(!frame.valid || !frame.tracked[0] || !frame.tracked[1] ||
           !cvr::roomscale::HeadYaw(frame.head.x,frame.head.y,frame.head.z,frame.head.w,&head))
            return false;
        XrVector3f hand[2];
        for(int h=0;h<2;++h) {
            const auto& p=frame.hands[h];
            hand[h]=RotateVector(frame.head,{p.x,p.y+.10f,p.z-.08f});
        }
        const float mx=(hand[0].x+hand[1].x)*.5f,mz=(hand[0].z+hand[1].z)*.5f;
        float fx{},fz{};
        if(std::hypot(mx,mz)>=.12f) { fx=mx;fz=mz; }
        else {
            const float lx=hand[1].x-hand[0].x,lz=hand[1].z-hand[0].z;   // left to right
            if(std::hypot(lx,lz)<.25f)return false;
            fx=lz;fz=-lx;
        }
        // The same angle HeadYaw gives: the rotation about +Y that turns -Z onto the direction.
        out=Wrap(std::atan2(-fx,-fz)-head);
        return std::isfinite(out);
    }
private:
    static constexpr float Deg=.01745329252f;
    static float Wrap(float a) { return std::remainder(a,6.28318530718f); }
    float m_agree{};
    uint64_t m_stamp{};
    bool m_turning=false;
};
}
