#pragma once
#include "Utils/XrMath.hpp"
#include <RED4ext/Scripting/Natives/Generated/game/PSMSwimming.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cvr::swimming {
inline bool InWater(int state) {
    return state==int(RED4ext::game::PSMSwimming::Surface) || state==int(RED4ext::game::PSMSwimming::Diving);
}
inline bool StateFresh(uintptr_t owner,uintptr_t current,uint64_t stamp,uint64_t now,int state) {
    return owner && owner==current && stamp && now>=stamp && now-stamp<=250000 && InWater(state);
}
inline bool PresenceFresh(uintptr_t owner,uintptr_t current,uint64_t stamp,uint64_t now,int state,bool waterContext) {
    // SwimmingTransitionEvents temporarily publishes Default while diving.
    // Physical submersion keeps the water posture through this real PSM gap.
    return owner && owner==current && stamp && now>=stamp && now-stamp<=250000 && (InWater(state)||waterContext);
}
struct Sample {
    XrVector3f head{},hands[2]{}; // recenter-space metres, before weapon offsets
    XrQuaternionf orientation{0,0,0,1};
    uint64_t sequence{},stampUs{},origin{};
    bool valid{};
};
inline XrVector3f Sub(XrVector3f a,XrVector3f b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline float Dot(XrVector3f a,XrVector3f b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline float Length(XrVector3f a) { return std::sqrt(Dot(a,a)); }
inline bool Finite(XrVector3f a) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z); }
inline float Smooth(float t) { t=std::clamp(t,0.0f,1.0f);return t*t*(3-2*t); }
inline XrVector3f HandInTrackingSpace(XrVector3f head,XrQuaternionf rotation,XrVector3f local) {
    const auto p=RotateVector(rotation,local);return {head.x+p.x,head.y+p.y,head.z+p.z};
}

struct Motion { float forward{};bool stroke{};bool boost{};float speed{}; };
// Fast swimming latches SprintToggled in the game's state machine. A normal
// stroke briefly goes below the live 0.80 input threshold (script fallback:
// 0.90) until fast-state feedback clears, then returns to full ordinary forward
// input. Physical sprint wins. A 0.85 cap cannot clear the live game's latch.
inline float ForwardAction(float forward,bool boost,int fast,bool manualSprint=false) {
    return !boost && fast==1 && !manualSprint ? std::min(forward,.75f):forward;
}
class Breaststroke {
public:
    enum class Phase { Reach,Pull,Recover };
    void Reset() { *this={}; }
    Phase CurrentPhase() const { return m_phase; }
    float Speed() const { return m_speed; }
    Motion Update(const Sample& p,uint64_t now,bool allowed) {
        float norm=p.orientation.x*p.orientation.x+p.orientation.y*p.orientation.y+
            p.orientation.z*p.orientation.z+p.orientation.w*p.orientation.w;
        bool valid=allowed && p.valid && p.sequence && p.origin && p.stampUs && now>=p.stampUs &&
            now-p.stampUs<=250000 && std::isfinite(norm) && norm>.5f && norm<1.5f && Finite(p.head);
        for(const auto hand:p.hands)valid=valid && Finite(hand) && Length(Sub(hand,p.head))<2.0f;
        if(!valid) { Reset();return {}; }
        if(m_origin!=p.origin) { Reset();m_origin=p.origin; }
        if(p.sequence==m_sequence)return Output(now,false);
        if(m_stamp && (p.stampUs<=m_stamp || p.stampUs-m_stamp>150000)) {
            Reset();m_origin=p.origin;
        }
        const float dt=m_stamp?float(p.stampUs-m_stamp)*1e-6f:0;
        XrVector3f movement[2]{};
        if(m_stamp) {
            for(int i=0;i<2;++i) {
                movement[i]=Sub(p.hands[i],m_previous[i]);
                if(Length(movement[i])>std::max(.15f,5.0f*dt)) {
                    Reset();m_origin=p.origin;return {};
                }
            }
        }
        m_sequence=p.sequence;m_stamp=p.stampUs;
        for(int i=0;i<2;++i)m_previous[i]=p.hands[i];
        XrVector3f forward=RotateVector(p.orientation,{0,0,-1});
        float length=Length(forward);
        if(length<.01f) { Reset();return {}; }
        forward={forward.x/length,forward.y/length,forward.z/length};
        const XrVector3f right=RotateVector(p.orientation,{1,0,0});
        const XrVector3f up=RotateVector(p.orientation,{0,1,0});
        const auto left=Sub(p.hands[0],p.head),r=Sub(p.hands[1],p.head);
        const float width=Dot(Sub(r,left),right);
        const bool reach=Dot(left,forward)>.22f && Dot(r,forward)>.22f && width>.06f && width<.50f &&
            Dot(left,right)<.12f && Dot(r,right)>-.12f &&
            Dot(left,up)>-.85f && Dot(left,up)<.30f && Dot(r,up)>-.85f && Dot(r,up)<.30f &&
            std::abs(Dot(Sub(left,r),up))<.30f;
        bool stroke=false;
        if(m_phase==Phase::Recover && reach && now-m_lastStroke>250000)m_phase=Phase::Reach;
        if(m_phase==Phase::Reach) {
            if(!reach)m_reachSince=0;
            else if(!m_reachSince)m_reachSince=p.stampUs;
            else if(p.stampUs-m_reachSince>=60000) {
                m_phase=Phase::Pull;m_armedAt=0;m_forward=forward;m_right=right;
                m_driving=false;m_progress=0;m_fastSince=m_slowSince=0;m_velocity[0]=m_velocity[1]=0;
                for(int i=0;i<2;++i) { m_start[i]=p.hands[i];m_out[i]=0; }
            }
        } else if(m_phase==Phase::Pull) {
            float pull[2];
            for(int i=0;i<2;++i) {
                const auto travel=Sub(p.hands[i],m_start[i]);
                pull[i]=-Dot(travel,m_forward);
                m_out[i]=std::max(m_out[i],Dot(travel,m_right)*(i ? 1.0f:-1.0f));
                if(dt>0) {
                    const float speed=-Dot(movement[i],m_forward)/dt;
                    m_velocity[i]+=(speed-m_velocity[i])*(1-std::exp(-dt/.05f));
                }
            }
            m_speed=std::max(0.f,(m_velocity[0]+m_velocity[1])*.5f);
            // Holding the ready pose must not periodically time out just as
            // the swimmer starts. Time the stroke from actual hand movement.
            if(!m_armedAt && (pull[0]>.03f || pull[1]>.03f || m_out[0]>.03f || m_out[1]>.03f))
                m_armedAt=p.stampUs;
            const float elapsed=m_armedAt ? float(p.stampUs-m_armedAt)*1e-6f:0;
            const float progress=(pull[0]+pull[1])*.5f;
            if(elapsed>2.2f || Dot(forward,m_forward)<.5f || pull[0]<-.08f || pull[1]<-.08f) {
                m_phase=Phase::Reach;m_reachSince=0;
                if(m_provisional)m_until=std::min(m_until,now+60000);
            } else {
                // Start native movement while both hands are already pulling,
                // without waiting for the complete 20cm stroke. Prediction is
                // short-lived: stopping/aborting this partial pull cannot buy a
                // full glide. A completed stroke still commits the normal glide.
                const bool early=elapsed>=.04f && pull[0]>.035f && pull[1]>.035f && progress>.05f &&
                    std::abs(pull[0]-pull[1])<.12f && m_out[0]>.02f && m_out[1]>.02f &&
                    progress/elapsed>.15f && m_velocity[0]>.08f && m_velocity[1]>.08f;
                const bool advancing=progress>m_progress+.002f;
                if(early && advancing) {
                    if(m_until<=now) {m_began=now;m_boost=false;}
                    const uint64_t previewEnd=now+120000;
                    if(previewEnd>m_until){m_until=previewEnd;m_provisional=true;}
                    m_driving=true;m_progress=progress;
                }
                // Filter velocity rather than dividing total travel by a timer
                // that began after movement: that overestimates gentle pulls.
                // Hysteresis and sustained evidence reject tracker-noise spikes.
                if(m_driving && m_speed>=.75f && std::min(m_velocity[0],m_velocity[1])>.35f) {
                    if(!m_fastSince)m_fastSince=p.stampUs;
                    if(p.stampUs-m_fastSince>=40000)m_boost=true;
                    m_slowSince=0;
                } else if(m_driving && early && m_speed<.45f) {
                    if(!m_slowSince)m_slowSince=p.stampUs;
                    if(p.stampUs-m_slowSince>=80000)m_boost=false;
                    m_fastSince=0;
                } else {m_fastSince=m_slowSince=0;}
                if(elapsed>=.12f && pull[0]>.16f && pull[1]>.16f && progress>.20f &&
                   std::abs(pull[0]-pull[1])<.20f && m_out[0]>.045f && m_out[1]>.045f && progress/elapsed>.15f) {
                    stroke=true;m_phase=Phase::Recover;m_reachSince=0;m_lastStroke=now;
                    if(m_until<=now){m_began=now;m_boost=false;}
                    m_until=now+1150000;m_provisional=false;
                }
            }
        }
        return Output(now,stroke);
    }
private:
    float Forward(uint64_t now) const {
        if(!m_until || now>=m_until || now<m_began)return 0;
        return Smooth(float(now-m_began)/35000.0f)*Smooth(float(m_until-now)/(m_provisional?60000.0f:180000.0f));
    }
    Motion Output(uint64_t now,bool stroke) const {const float forward=Forward(now);return {forward,stroke,forward>0 && m_boost,m_speed};}
    Phase m_phase{};
    uint64_t m_sequence{},m_stamp{},m_origin{},m_reachSince{},m_armedAt{},m_lastStroke{},m_began{},m_until{};
    XrVector3f m_start[2]{},m_previous[2]{},m_forward{},m_right{};
    float m_out[2]{};
    float m_velocity[2]{},m_speed{},m_progress{};
    uint64_t m_fastSince{},m_slowSince{};
    bool m_driving{},m_boost{},m_provisional{};
};

// Raise separated hands toward chest level, then push both down through the
// water. Absolute tracking travel prevents a moving head from inventing lift.
class AscendStroke {
public:
    void Reset() { *this={}; }
    Motion Update(const Sample& p,uint64_t now,bool allowed) {
        const float norm=p.orientation.x*p.orientation.x+p.orientation.y*p.orientation.y+
            p.orientation.z*p.orientation.z+p.orientation.w*p.orientation.w;
        if(!allowed || !p.valid || !p.sequence || !p.origin || !p.stampUs || now<p.stampUs ||
           now-p.stampUs>250000 || !Finite(p.head) || !std::isfinite(norm) || norm<.5f || norm>1.5f) {
            Reset();return {};
        }
        for(auto hand:p.hands)if(!Finite(hand) || Length(Sub(hand,p.head))>2) { Reset();return {}; }
        if(m_origin!=p.origin) { Reset();m_origin=p.origin; }
        auto output=[&](bool stroke) {
            const float strength=m_until>now && now>=m_began
                ? Smooth(float(now-m_began)/80000)*Smooth(float(m_until-now)/160000):0;
            return Motion{strength,stroke};
        };
        if(p.sequence==m_sequence)return output(false);
        if(m_stamp && (p.stampUs<=m_stamp || p.stampUs-m_stamp>150000)) { Reset();m_origin=p.origin; }
        if(m_stamp)for(int i=0;i<2;++i)
            if(Length(Sub(p.hands[i],m_previous[i]))>std::max(.15f,5.0f*float(p.stampUs-m_stamp)*1e-6f)) {
                Reset();return {};
            }
        m_sequence=p.sequence;m_stamp=p.stampUs;
        for(int i=0;i<2;++i)m_previous[i]=p.hands[i];
        const float width=Length(Sub(p.hands[1],p.hands[0]));
        bool ready=width>.30f && width<1.1f && std::abs(p.hands[0].y-p.hands[1].y)<.20f;
        for(auto hand:p.hands)ready=ready && hand.y-p.head.y>-.38f && hand.y-p.head.y<.30f;
        if(m_recover && ready && now-m_lastStroke>250000)m_recover=false;
        if(m_recover)return output(false);
        if(!m_armed) {
            if(!ready)m_readySince=0;
            else if(!m_readySince)m_readySince=p.stampUs;
            else if(p.stampUs-m_readySince>=60000) {
                m_armed=true;m_started=0;m_headStart=p.head;for(int i=0;i<2;++i)m_start[i]=p.hands[i];
            }
            return output(false);
        }
        float down[2],relativeDown[2];bool lateral=false;
        for(int i=0;i<2;++i) {
            const auto d=Sub(p.hands[i],m_start[i]);down[i]=-d.y;
            relativeDown[i]=down[i]+p.head.y-m_headStart.y;
            lateral=lateral || std::hypot(d.x,d.z)>.18f;
        }
        if(!m_started && (down[0]>.03f || down[1]>.03f))m_started=p.stampUs;
        const float elapsed=m_started ? float(p.stampUs-m_started)*1e-6f:0;
        if(lateral || elapsed>1.8f || down[0]<-.15f || down[1]<-.15f) {
            m_armed=false;m_readySince=0;return output(false);
        }
        if(elapsed>=.12f && down[0]>.22f && down[1]>.22f && relativeDown[0]>.18f && relativeDown[1]>.18f &&
           std::abs(down[0]-down[1])<.16f && (down[0]+down[1])*.5f/elapsed>.20f) {
            m_armed=false;m_recover=true;m_readySince=0;m_lastStroke=now;
            if(m_until<=now)m_began=now;
            m_until=now+800000;return output(true);
        }
        return output(false);
    }
private:
    uint64_t m_origin{},m_sequence{},m_stamp{},m_readySince{},m_started{},m_lastStroke{},m_until{},m_began{};
    XrVector3f m_start[2]{},m_previous[2]{},m_headStart{};
    bool m_armed{},m_recover{};
};

// ToggleSprint is LShift/L3. Keep our press through a boost, and do not press
// again when fast swimming was already active before the new stroke.
class SprintButton {
public:
    void Reset() { *this={}; }
    bool Update(bool wanted,int fast,uint64_t now) {
        if(!wanted) { Reset();return false; }
        if(!m_active) { m_active=true;m_down=fast!=1;m_sawFast=fast==1; }
        if(fast==1) { m_sawFast=true;return m_down; }
        if(fast==0 && m_sawFast && m_down) {
            m_down=false;m_sawFast=false;m_releaseUntil=now+60000;
        } else if(!m_down && now>=m_releaseUntil)m_down=true;
        return m_down;
    }
private:
    bool m_active{},m_down{},m_sawFast{};
    uint64_t m_releaseUntil{};
};
}
