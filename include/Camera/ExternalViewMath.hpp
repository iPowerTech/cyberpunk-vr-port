#pragma once
#include "Utils/XrMath.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace cvr::camera {
struct ExternalViewPose {std::array<int32_t,3> position{};XrQuaternionf rotation{0,0,0,1};};
inline bool AimAtPoint(const XrVector3f& origin,const XrVector3f& target,XrVector3f& output) {
    const double x=double(target.x)-origin.x,y=double(target.y)-origin.y,z=double(target.z)-origin.z;
    const double length=std::sqrt(x*x+y*y+z*z);
    if(!std::isfinite(length) || length<1e-6)return false;
    output={float(x/length),float(y/length),float(z/length)};return true;
}
inline bool NormalizeExternal(XrQuaternionf& q) {
    const double n=double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w;
    if(!std::isfinite(n)||n<.5||n>1.5)return false;
    const float s=float(1/std::sqrt(n));q={q.x*s,q.y*s,q.z*s,q.w*s};return true;
}
inline bool PlanarExternalForward(XrQuaternionf q,XrVector3f& output) {
    if(!NormalizeExternal(q))return false;
    const auto forward=RotateVector(q,{0,1,0});
    const float length=std::hypot(forward.x,forward.y);
    if(!std::isfinite(length)||length<.05f)return false;
    output={forward.x/length,forward.y/length,0};return true;
}
inline bool ShiftExternal(ExternalViewPose& pose,const XrVector3f& delta) {
    auto position=pose.position;const float v[]{delta.x,delta.y,delta.z};
    for(int i=0;i<3;++i) {
        if(!std::isfinite(v[i]))return false;
        const double shifted=double(position[i])+std::round(double(v[i])*131072.0);
        if(shifted<std::numeric_limits<int32_t>::min()||shifted>std::numeric_limits<int32_t>::max())return false;
        position[i]=int32_t(shifted);
    }
    pose.position=position;return true;
}
inline bool ExternalEye(ExternalViewPose& pose,float offset) {
    if(!std::isfinite(offset)||std::abs(offset)>1)return false;
    const auto right=RotateVector(pose.rotation,{1,0,0});
    return ShiftExternal(pose,{right.x*offset,right.y*offset,right.z*offset});
}
inline bool ComposeExternal(const ExternalViewPose& base,const XrPosef& head,float scale,
                            float mainEyeOffset,ExternalViewPose& output) {
    ExternalViewPose result=base;auto h=head.orientation;
    if(!NormalizeExternal(result.rotation)||!NormalizeExternal(h)||!std::isfinite(scale)||scale<=0||scale>100)return false;
    const auto& q=result.rotation;
    const float yaw=std::atan2(2*(q.w*q.z+q.x*q.y),1-2*(q.y*q.y+q.z*q.z));
    const float c=std::cos(yaw),s=std::sin(yaw);
    const XrVector3f delta{(c*head.position.x+s*head.position.z)*scale,
                          (s*head.position.x-c*head.position.z)*scale,head.position.y*scale};
    result.rotation=MultiplyQuat(result.rotation,{h.x,-h.z,h.y,h.w});
    if(!NormalizeExternal(result.rotation)||!ShiftExternal(result,delta)||!ExternalEye(result,mainEyeOffset))return false;
    output=result;return true;
}
struct ExternalViewInput {ExternalViewPose pose;float weight;};
inline bool BlendExternal(std::span<const ExternalViewInput> input,ExternalViewPose& output) {
    if(input.empty())return false;
    double sum=0;std::array<double,3> position{};XrQuaternionf rotation{0,0,0,0},reference{};bool have=false;
    for(const auto& value:input) {
        if(!std::isfinite(value.weight)||value.weight<0)return false;
        if(value.weight==0)continue;
        auto q=value.pose.rotation;if(!NormalizeExternal(q))return false;
        if(!have){reference=q;have=true;}
        const float sign=q.x*reference.x+q.y*reference.y+q.z*reference.z+q.w*reference.w<0 ? -1.f:1.f;
        const float weight=input.size()==1 ? 1.f:value.weight;
        sum+=weight;for(int i=0;i<3;++i)position[i]+=double(value.pose.position[i])*weight;
        rotation.x+=q.x*weight*sign;rotation.y+=q.y*weight*sign;rotation.z+=q.z*weight*sign;rotation.w+=q.w*weight*sign;
    }
    if(!have||std::abs(sum-1)>1e-5)return false;
    const double norm=double(rotation.x)*rotation.x+double(rotation.y)*rotation.y+double(rotation.z)*rotation.z+double(rotation.w)*rotation.w;
    if(!std::isfinite(norm)||norm<1e-8)return false;
    const float inverse=float(1/std::sqrt(norm));rotation={rotation.x*inverse,rotation.y*inverse,rotation.z*inverse,rotation.w*inverse};
    ExternalViewPose result;result.rotation=rotation;
    for(int i=0;i<3;++i) {
        if(position[i]<std::numeric_limits<int32_t>::min()||position[i]>std::numeric_limits<int32_t>::max())return false;
        result.position[i]=int32_t(std::llround(position[i]));
    }
    output=result;return true;
}
}
