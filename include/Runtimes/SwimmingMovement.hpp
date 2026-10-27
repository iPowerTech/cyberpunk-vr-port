#pragma once
#include <algorithm>
#include <cmath>
#include <openxr/openxr.h>

namespace cvr::swimming {
struct Vector {
    float x{},y{},z{}; // game world axes, Z up
    Vector operator+(Vector b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vector operator-(Vector b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vector operator*(float k) const { return {x*k,y*k,z*k}; }
};
inline float Dot(Vector a,Vector b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline bool Finite(Vector v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }

// Diving already includes the camera/HMD pitch in native.z (measured at
// +/-0.6rad). Rotate heading only: adding another pitch doubles vertical
// propulsion and also changes its speed. Buoyancy remains entirely native.
inline Vector Redirect(Vector native,XrVector3f headForward,float trackingYaw,bool /*diving*/) {
    if(!Finite(native) || !std::isfinite(trackingYaw))return native;
    Vector direction{headForward.x,-headForward.z,0};
    const float norm=Dot(direction,direction);
    if(!Finite(direction) || norm<1e-8f)return native;
    direction=direction*(1/std::sqrt(norm));
    const float c=std::cos(trackingYaw),s=std::sin(trackingYaw);
    direction={c*direction.x-s*direction.y,s*direction.x+c*direction.y,direction.z};
    const float distance=std::hypot(native.x,native.y);
    return {distance*direction.x,distance*direction.y,native.z};
}
}
