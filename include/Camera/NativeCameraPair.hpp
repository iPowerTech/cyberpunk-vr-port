#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace cvr::camera {
using FixedPosition=std::array<int32_t,3>;
struct CameraEntitySample {
    std::array<float,4> cameraQuat{},entityQuat{};
    std::array<float,3> cameraMinusEntity{},bodyMinusEntity{};
    bool valid{};
};
inline CameraEntitySample MakeCameraEntitySample(const FixedPosition& centre,
        const FixedPosition& bodyBase,const FixedPosition& entity,
        const float* cameraQuat,const float* entityQuat) {
    CameraEntitySample out{};
    float span=0;
    for(int k=0;k<3;++k) {
        // Subtract BEFORE conversion: world floats lose sub-millimetre bits in
        // Night City, whereas these small relative distances retain them.
        out.cameraMinusEntity[k]=float(int64_t(centre[k])-entity[k])/131072.0f;
        out.bodyMinusEntity[k]=float(int64_t(bodyBase[k])-entity[k])/131072.0f;
        span+=out.cameraMinusEntity[k]*out.cameraMinusEntity[k];
    }
    for(int side=0;side<2;++side) {
        const float* src=side ? entityQuat : cameraQuat;
        auto& dst=side ? out.entityQuat : out.cameraQuat;
        double norm=0;for(int k=0;k<4;++k)norm+=double(src[k])*src[k];
        if(!std::isfinite(norm) || norm<.5 || norm>1.5)return out;
        const float inv=float(1/std::sqrt(norm));
        for(int k=0;k<4;++k)dst[k]=src[k]*inv;
    }
    out.valid=std::isfinite(span) && span>=.0001f && span<9.0f;
    return out;
}
enum class CameraPacketState { Valid, Unavailable, Invalidated };
inline CameraPacketState CameraPacketStatus(uintptr_t owner,uint64_t origin,uint64_t stamp,
        uintptr_t currentOwner,uint64_t currentOrigin,uint64_t now,bool valid,bool unavailable) {
    if(!owner || owner!=currentOwner || origin!=currentOrigin)return CameraPacketState::Invalidated;
    if(!valid)return unavailable ? CameraPacketState::Unavailable : CameraPacketState::Invalidated;
    if(!stamp || now<stamp || now-stamp>250)return CameraPacketState::Unavailable;
    return CameraPacketState::Valid;
}
}
