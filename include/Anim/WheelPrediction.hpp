#pragma once
#include "Anim/WheelRange.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cvr::anim {
// A small feed-forward term, never a filtered/reintegrated steering angle.
// Four observations must show a continuing turn. The slowest of their three
// interval speeds limits extrapolation; stops/reversals drop it immediately.
class WheelPrediction {
public:
    static constexpr float MaxMs=8.f,MaxLeadDegrees=.5f;
    static float ClampMs(float value) {return std::isfinite(value) ? std::clamp(value,0.f,MaxMs):0.f;}
    void Reset() { *this={}; }
    void Push(float angle,uint64_t stamp) {
        if(!std::isfinite(angle) || !stamp) {Reset();return;}
        if(m_count && (stamp<=m_points[m_count-1].stamp || stamp-m_points[m_count-1].stamp>75000))Reset();
        if(m_count && stamp-m_points[m_count-1].stamp<1000) {
            m_points[m_count-1]={angle,stamp};m_velocity=0;return;
        }
        if(m_count==m_points.size()) {
            for(size_t i=1;i<m_count;++i)m_points[i-1]=m_points[i];
            --m_count;
        }
        m_points[m_count++]={angle,stamp};m_velocity=0;
        if(m_count<4)return;
        float speed=720.f;int direction=0;
        for(size_t i=1;i<m_count;++i) {
            const float delta=m_points[i].angle-m_points[i-1].angle;
            const float dt=float(m_points[i].stamp-m_points[i-1].stamp)*1e-6f;
            if(std::abs(delta)<.002f || dt<=0)return;
            const float v=delta/dt;
            if(std::abs(v)<3.f || std::abs(v)>720.f)return;
            const int sign=v>0 ? 1:-1;
            if(direction && sign!=direction)return;
            direction=sign;speed=std::min(speed,std::abs(v));
        }
        m_velocity=float(direction)*speed;
    }
    float Angle(float physical,uint64_t now,float horizonMs,float limit,float dead) const {
        const float ms=ClampMs(horizonMs);
        if(!ms || !m_velocity || m_count<4 || !std::isfinite(physical))return physical;
        const auto stamp=m_points[m_count-1].stamp;
        const auto interval=stamp-m_points[m_count-2].stamp;
        const uint64_t lease=std::clamp(interval*2,uint64_t(25000),uint64_t(60000));
        if(now<stamp || now-stamp>lease)return physical;
        limit=WheelFullLockDegrees(limit);
        dead=std::isfinite(dead)&&dead>=0&&dead<=20 ? dead:1.5f;
        // Fade over two degrees at neutral/full lock. The lead cannot cross
        // either boundary or pre-empt the player's configured center deadzone.
        const float envelope=std::clamp(std::min(std::abs(physical)-dead,limit-std::abs(physical))/2.f,0.f,1.f);
        const float lead=std::clamp(m_velocity*ms*.001f,-MaxLeadDegrees,MaxLeadDegrees)*envelope;
        return physical+lead;
    }
private:
    struct Point {float angle{};uint64_t stamp{};};
    std::array<Point,4> m_points{};
    size_t m_count{};
    float m_velocity{};
};
}
