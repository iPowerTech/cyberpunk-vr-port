#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cvr::camera {
inline constexpr size_t TargetingCameraTransformOffset=0x31100;
inline constexpr size_t TargetingDefaultPositionOffset=0x311F0;
inline constexpr size_t TargetingDefaultForwardOffset=0x31200;
inline constexpr size_t TargetingDefaultAnglesOffset=0x31210;

// Re-aim the native head-gaze cache without moving its origin, camera or muzzle.
// The original update refreshes the cache before this is called on each frame.
inline bool CenterHeadGaze(void* state,uintptr_t player,uint64_t entity,float tangentY) {
    if(!state || !player || !entity || !std::isfinite(tangentY) || std::abs(tangentY)>1.f || tangentY==0)return false;
    auto* bytes=static_cast<uint8_t*>(state);
    uintptr_t owner{};uint64_t ownerEntity{};uint32_t active{};
    std::memcpy(&owner,bytes,sizeof(owner));std::memcpy(&ownerEntity,bytes+8,sizeof(ownerEntity));
    std::memcpy(&active,bytes+24,sizeof(active));
    if(owner!=player || ownerEntity!=entity || !active)return false;
    float pose[8]{},old[4]{};std::memcpy(pose,bytes+TargetingCameraTransformOffset,sizeof(pose));
    std::memcpy(old,bytes+TargetingDefaultForwardOffset,sizeof(old));
    for(float v:pose)if(!std::isfinite(v))return false;
    for(float v:old)if(!std::isfinite(v))return false;
    const float n=pose[4]*pose[4]+pose[5]*pose[5]+pose[6]*pose[6]+pose[7]*pose[7];
    if(n<.5f || n>1.5f)return false;
    const float inv=1/std::sqrt(n),x=pose[4]*inv,y=pose[5]*inv,z=pose[6]*inv,w=pose[7]*inv;
    const float f[]{2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w)};
    // Leave a non-camera ray (e.g. a scripted aiming override) alone.
    if(old[0]*f[0]+old[1]*f[1]+old[2]*f[2]<.98f)return false;
    const float up[]{2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y)};
    const float length=std::sqrt(1+tangentY*tangentY);
    const float forward[]{(f[0]+tangentY*up[0])/length,(f[1]+tangentY*up[1])/length,
                          (f[2]+tangentY*up[2])/length,old[3]};
    constexpr float degrees=180.f/3.14159265358979323846f;
    const float angles[]{0.f,-std::atan2(forward[2],std::hypot(forward[0],forward[1]))*degrees,
                        std::atan2(-forward[0],forward[1])*degrees};
    std::memcpy(bytes+TargetingDefaultForwardOffset,forward,sizeof(forward));
    std::memcpy(bytes+TargetingDefaultAnglesOffset,angles,sizeof(angles));
    // GetDefaultCrosshairData (2.31, +2512D70) reads the FPP source cache
    // directly, whereas native target selection uses the copy above.
    // Keep both directions in step; never move sourceCache+0x350 (origin).
    uintptr_t sourceCache{};std::memcpy(&sourceCache,bytes+0x30CC8,sizeof(sourceCache));
    if(sourceCache)std::memcpy(reinterpret_cast<void*>(sourceCache+0x370),forward,sizeof(forward));
    return true;
}

// 2.31 TargetingSystemUser: +4B9D84 first reads the active camera, then
// overwrites the default ray from the player's FPP cache. +23D544 uses that
// default ray to reject targets behind it before quest CameraFocus runs.
inline bool RefreshTakeoverTargeting(void* state,uintptr_t player,uint64_t entity) {
    if(!state || !player || !entity)return false;
    auto* bytes=static_cast<uint8_t*>(state);
    uintptr_t owner{};uint64_t ownerEntity{};uint32_t active{};
    std::memcpy(&owner,bytes,sizeof(owner));
    std::memcpy(&ownerEntity,bytes+8,sizeof(ownerEntity));
    std::memcpy(&active,bytes+24,sizeof(active));
    if(owner!=player || ownerEntity!=entity || !active)return false;
    float pose[8]{};std::memcpy(pose,bytes+TargetingCameraTransformOffset,sizeof(pose));
    for(float v:pose)if(!std::isfinite(v))return false;
    const float n=pose[4]*pose[4]+pose[5]*pose[5]+pose[6]*pose[6]+pose[7]*pose[7];
    if(n<.5f || n>1.5f)return false;
    const float inv=1/std::sqrt(n),x=pose[4]*inv,y=pose[5]*inv,z=pose[6]*inv,w=pose[7]*inv;
    const float position[]{pose[0],pose[1],pose[2],1.f};
    const float forward[]{2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),1.f};
    constexpr float degrees=180.f/3.14159265358979323846f;
    const float angles[]{0.f,-std::atan2(forward[2],std::hypot(forward[0],forward[1]))*degrees,
                        std::atan2(-forward[0],forward[1])*degrees};
    std::memcpy(bytes+TargetingDefaultPositionOffset,position,sizeof(position));
    std::memcpy(bytes+TargetingDefaultForwardOffset,forward,sizeof(forward));
    std::memcpy(bytes+TargetingDefaultAnglesOffset,angles,sizeof(angles));
    return true;
}
}
