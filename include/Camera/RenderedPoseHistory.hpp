#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <limits>

namespace cvr::camera {
enum class RenderedPoseMatch { Missing, Exact, Ambiguous };
inline bool SameRenderedRotation(const float* a,const float* b) {
    double aa=0,bb=0,dot=0;
    for(int k=0;k<4;++k) { aa+=double(a[k])*a[k];bb+=double(b[k])*b[k];dot+=double(a[k])*b[k]; }
    if(!std::isfinite(aa+bb+dot) || aa<.5 || aa>1.5 || bb<.5 || bb>1.5)return false;
    const double ai=1/std::sqrt(aa),bi=(dot<0 ? -1 : 1)/std::sqrt(bb);
    double error=0;for(int k=0;k<4;++k) { const double d=a[k]*ai-b[k]*bi;error+=d*d; }
    // Engine matrix->quaternion round trips change component bits. Live VRCAM
    // differed by 0.000395deg (neutral) and 0.001405deg (40deg pitch), despite
    // the same fixed-point eye position. Bound the round trip at 0.002deg;
    // position, eye and origin still match exactly.
    return error<=3.0461742e-10;
}

// The quaternion alone cannot identify a translation-only frame. Keep the
// completed per-eye camera transform beside its actual XR label. A collision
// can make even the full transform repeat with different labels; reject that
// ambiguity rather than silently attaching the newest label to old pixels.
template<class Pose, std::size_t Capacity = 64>
class RenderedPoseHistory {
    static_assert(Capacity>0);
public:
    void Push(uint32_t view, const float q[4], const int32_t position[3], const Pose& pose) {
        if (!pose.valid || !std::isfinite(pose.posX) || !std::isfinite(pose.posY) ||
            !std::isfinite(pose.posZ) || !std::isfinite(pose.oriX) || !std::isfinite(pose.oriY) ||
            !std::isfinite(pose.oriZ) || !std::isfinite(pose.oriW)) return;
        std::lock_guard lock(m_mutex);
        auto& entry=m_entries[m_head % Capacity];
        for (int i=0;i<4;++i) entry.quat[i]=q[i];
        for (int i=0;i<3;++i) entry.position[i]=position[i];
        entry.view=view; entry.pose=pose; entry.id=m_head++;
    }

    RenderedPoseMatch Find(uint32_t view, const float q[4], const int32_t position[3],
                           uint64_t origin, Pose* out, uint32_t* age, uint32_t* ties) const {
        std::lock_guard lock(m_mutex);
        const auto count=m_head<Capacity ? m_head : Capacity;
        const Entry* best=nullptr;
        uint32_t matches=0;
        bool ambiguous=false;
        for (uint64_t i=1;i<=count;++i) {
            const auto& entry=m_entries[(m_head-i)%Capacity];
            if (entry.view!=view || entry.pose.originSerial!=origin) continue;
            bool same=true;
            for (int k=0;k<3;++k) same=same && entry.position[k]==position[k];
            same=same && SameRenderedRotation(entry.quat.data(),q);
            if (!same) continue;
            ++matches;
            if (!best) best=&entry;
            else if (!SameLabel(best->pose,entry.pose,position)) ambiguous=true;
        }
        if (ties) *ties=matches;
        if (ambiguous) return RenderedPoseMatch::Ambiguous;
        if (!best) return RenderedPoseMatch::Missing;
        if (out) *out=best->pose;
        if (age) *age=static_cast<uint32_t>(m_head-1-best->id);
        return RenderedPoseMatch::Exact;
    }

private:
    static double LabelPositionTolerance(const int32_t position[3]) {
        // CCT positions pass through world floats before the camera's fixed-
        // point write. At y=-2456m one ULP is 0.244mm. Live identical camera
        // writes carried head labels 0.222mm apart after physical motion.
        // Bound the vector error (rotation independent), and never allow the
        // tolerance to grow beyond 0.25mm at distant world coordinates.
        double squared=0;
        for(int k=0;k<3;++k) {
            const float world=std::abs(float(position[k])/131072.0f);
            const double ulp=std::nextafter(world,std::numeric_limits<float>::infinity())-world;
            squared+=ulp*ulp;
        }
        return std::clamp(std::sqrt(squared),.00001,.00025);
    }
    static bool SameLabel(const Pose& a, const Pose& b, const int32_t position[3]) {
        const double tolerance=LabelPositionTolerance(position);
        const double x=double(a.posX)-b.posX,y=double(a.posY)-b.posY,z=double(a.posZ)-b.posZ;
        if(x*x+y*y+z*z>tolerance*tolerance)return false;
        const float qa[4]={a.oriX,a.oriY,a.oriZ,a.oriW},qb[4]={b.oriX,b.oriY,b.oriZ,b.oriW};
        return SameRenderedRotation(qa,qb);
    }
    struct Entry {
        std::array<float,4> quat{};
        std::array<int32_t,3> position{};
        Pose pose{};
        uint64_t id{};
        uint32_t view{};
    };
    mutable std::mutex m_mutex;
    std::array<Entry,Capacity> m_entries{};
    uint64_t m_head{};
};
}
