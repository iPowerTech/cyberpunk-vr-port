#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cvr::camera {
inline constexpr float SurveillancePi=3.14159265358979323846f;
inline float SurveillanceWrap(float angle) {return std::remainder(angle,2*SurveillancePi);}
struct SurveillanceAngles {float yaw{},pitch{};};
// The device update later adds CameraSystem's automatic rotation to the same
// Euler buffer. Preserve the actual mouse+HMD input across those modifiers;
// ownership/address matching prevents an unrelated native update using it.
class SurveillanceInputSample {
public:
    void Capture(uintptr_t component,float* input,bool enabled) {
        component_=enabled?component:0;input_=enabled?input:nullptr;
        if(input_)std::copy_n(input_,3,values_);
    }
    bool Finish(uintptr_t component,float* automatic,float* input) {
        const bool matched=component_ && component_==component && input_==input && automatic;
        if(matched) {std::copy_n(values_,3,input);std::fill_n(automatic,3,0.f);}
        component_=0;input_=nullptr;return matched;
    }
private:
    uintptr_t component_{};float* input_{};float values_[3]{};
};
inline float SurveillanceManualYaw(float current,float delta,float minimum,float maximum) {
    if(!std::isfinite(current+delta+minimum+maximum))return 0;
    if(minimum*minimum+maximum*maximum<1e-7f)return delta; // native unlimited yaw
    auto positive=[](float v){v=std::fmod(v,360.f);return v<0?v+360.f:v;};
    const float span=positive(maximum-minimum),from=positive(current-minimum);
    if(span<1e-5f)return 0;
    if(from>span+.001f)return delta; // leave authored out-of-range startup to the engine
    return std::clamp(from+delta,0.f,span)-from;
}
inline bool SurveillanceHeadAngles(float x,float y,float z,float w,float priorYaw,SurveillanceAngles& out) {
    const float n=x*x+y*y+z*z+w*w;
    if(!std::isfinite(n)||n<.5f||n>1.5f)return false;
    const float inv=1/std::sqrt(n);x*=inv;y*=inv;z*=inv;w*=inv;
    // OpenXR -> game quaternion: (x,-z,y,w), with game forward +Y.
    const float fx=2*(-x*z-y*w),fy=1-2*(x*x+y*y),fz=2*(-z*y+x*w);
    const float horizontal=std::hypot(fx,fy);
    out.yaw=horizontal>.02f ? std::atan2(-fx,fy):priorYaw;
    out.pitch=std::atan2(fz,horizontal);
    return std::isfinite(out.yaw+out.pitch);
}
// The animation motor consumes head DELTAS. Rendering keeps a separate manual
// base plus HMD, so asynchronous animation updates cannot feed HMD back twice.
class SurveillanceFollow {
public:
    SurveillanceAngles Step(uintptr_t source,uint64_t entity,uint64_t origin,uint64_t now,
                            SurveillanceAngles head,float nativeYaw,float ownerYaw,float manualYaw,
                            bool enabled,bool alignView=false,float nativePitch=0,float manualPitch=0) {
        if(!source||!entity||!origin||!std::isfinite(head.yaw+head.pitch+nativeYaw+ownerYaw+manualYaw+nativePitch+manualPitch)) {
            Reset();return {};
        }
        if(!valid_||source_!=source||entity_!=entity||origin_!=origin||now<stamp_||now-stamp_>500) {
            source_=source;entity_=entity;origin_=origin;
            localYaw_=SurveillanceWrap(nativeYaw-ownerYaw);valid_=true;headReady_=false;
            alignmentPending_=alignView;
        }
        // Manual input remains native. It adjusts the view base independently
        // of rotation injected by this driver. Owner motion is added at readout.
        if(enabled)localYaw_=SurveillanceWrap(localYaw_+manualYaw);
        SurveillanceAngles delta{};
        if(enabled && alignmentPending_) {
            // The sniper fires along its physical barrel. Match the rendered
            // base+HMD on acquisition before continuing with ordinary deltas.
            delta={SurveillanceWrap(ownerYaw+localYaw_+head.yaw-nativeYaw-manualYaw),
                   head.pitch-nativePitch-manualPitch};
            alignmentPending_=false;
        } else if(enabled && headReady_ && now>=stamp_ && now-stamp_<=250) {
            delta={SurveillanceWrap(head.yaw-last_.yaw),head.pitch-last_.pitch};
            // A tracking discontinuity must not slam the motor through a turn.
            if(std::abs(delta.yaw)>SurveillancePi*.5f || std::abs(delta.pitch)>SurveillancePi*.5f)delta={};
        }
        last_=head;stamp_=now;headReady_=enabled;
        return delta;
    }
    bool View(uintptr_t source,uint64_t entity,uint64_t origin,uint64_t now,float ownerYaw,float& yaw) const {
        if(!valid_||source_!=source||entity_!=entity||origin_!=origin||now<stamp_||now-stamp_>500||!std::isfinite(ownerYaw))return false;
        yaw=SurveillanceWrap(ownerYaw+localYaw_);return true;
    }
    float LastHeadYaw() const {return last_.yaw;}
    void Reset(){*this={};}
private:
    uintptr_t source_{};
    uint64_t entity_{},origin_{},stamp_{};
    SurveillanceAngles last_{};
    float localYaw_{};
    bool valid_{},headReady_{},alignmentPending_{};
};

bool SurveillanceViewYaw(uintptr_t lens,uintptr_t owner,uint64_t origin,float ownerYaw,float& yaw);
bool SniperHeadFollowActive();
}
