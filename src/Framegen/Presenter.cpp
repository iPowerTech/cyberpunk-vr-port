#include "Framegen/Presenter.hpp"
#include "Framegen/Fsr3.hpp"
#include <algorithm>
#include <cmath>

namespace cvr::framegen {
Selection Presenter::Select(Frame current,bool gameplay) {
    const bool generate=gameplay && fsr3::Enabled();
    if(!gameplay)Reset();
    else if(!generate && cadence.Pending()) {cadence.Reset();pending={};}
    if(!DepthCaptureEnabled() && !MotionCaptureEnabled())ReleaseInputs();
    if(MetricsEnabled())SetVram(InputVram());
    // FSR3 interpolated `serial` from the capture before it; shown when that one was the last shown,
    // as itself or as the end of the midpoint just shown.
    ID3D12Resource* generated[2]{};
    const uint64_t serial=current.identity.serial;
    const auto interpolated=[&](uint64_t after,uint64_t origin) {
        return generate && after && serial==after+1 && current.identity.origin==origin &&
            (generated[0]=fsr3::Output(0,serial))!=nullptr && (generated[1]=fsr3::Output(1,serial))!=nullptr;
    };
    if(cadence.Pending()) {
        // The next capture is already here before the real frame after the midpoint could be shown:
        // its own midpoint (between that real frame and it) comes next instead, so every capture is
        // shown once and evenly spaced, as in the fork. The real frame is shown only when the loop has
        // the time for it.
        if(!interpolated(pending.identity.serial,pending.identity.origin))return {pending,Cadence::Next::PendingReal,false};
        fsr3::CountShown(fsr3::Shown::RealDropped,0);
        const auto& f=pending.identity;
        previousSerial=f.serial;previousOrigin=f.origin;previousCapturedMs=f.capturedMs;
        previousPose[0]=pending.pose[0];previousPose[1]=pending.pose[1];
        cadence.DropPending();pending={};
    }
    const bool midpoint=interpolated(previousSerial,previousOrigin);
    const auto kind=cadence.Select(serial,midpoint);
    // No new capture yet, the GPU still on the next one: as the fork does (OpenXRFrameLoop.cpp, the stale
    // cycle case of its frameGenEye substitution), the interpolated frame goes out again, with its own
    // halfway pose, instead of a repeat of the real one.
    if(kind==Cadence::Next::Repeat && generate && serial && serial==midpointSerial &&
       (generated[0]=fsr3::Output(0,serial))!=nullptr && (generated[1]=fsr3::Output(1,serial))!=nullptr) {
        Selection result{std::move(current),kind,true};
        result.frame.lease.reset();
        result.frame.identity.capturedMs=midpointCapturedMs;
        for(unsigned eye=0;eye<2;++eye) {
            result.frame.color[eye]=generated[eye];result.frame.pose[eye]=midpointPose[eye];result.frame.identity.pose[eye]=0;
        }
        return result;
    }
    if(kind!=Cadence::Next::Midpoint)return {std::move(current),kind,false};
    Selection result{current,kind,true};
    result.frame.lease.reset();
    result.frame.identity.capturedMs=(previousCapturedMs+current.identity.capturedMs)*.5;
    for(unsigned eye=0;eye<2;++eye) {
        result.frame.color[eye]=generated[eye];
        result.frame.pose[eye]=HalfwayPose(previousPose[eye],current.pose[eye]);
        result.frame.identity.pose[eye]=0;   // blended pixels have a blended pose, not the capture's receipt
        midpointPose[eye]=result.frame.pose[eye];
    }
    midpointSerial=serial;midpointCapturedMs=result.frame.identity.capturedMs;
    pending=std::move(current);
    return result;
}
void Presenter::Submitted(const Selection& result,uint64_t fenceValue,bool accepted) {
    if(result.frame.lease)result.frame.lease->fence=std::max(result.frame.lease->fence,fenceValue);
    if(!accepted) {Reset();return;}
    if(fsr3::Enabled()) {
        using K=Cadence::Next;
        const auto gap=previousSerial ? result.frame.identity.serial-previousSerial : 0;
        fsr3::CountShown(result.kind==K::Midpoint ? fsr3::Shown::Midpoint : result.kind==K::PendingReal ? fsr3::Shown::RealAfterMidpoint :
            result.kind==K::Real ? fsr3::Shown::Real : fsr3::Shown::Repeat,gap);
    }
    cadence.Accepted(result.kind,result.frame.identity.serial);
    if(result.kind==Cadence::Next::Real || result.kind==Cadence::Next::PendingReal) {
        const auto& f=result.frame;
        previousSerial=f.identity.serial;previousOrigin=f.identity.origin;previousCapturedMs=f.identity.capturedMs;
        previousPose[0]=f.pose[0];previousPose[1]=f.pose[1];
    }
    if(MetricsEnabled())OnSubmitted(result.frame.identity.serial,result.synthetic,result.kind==Cadence::Next::Repeat,NowMs());
    if(result.kind==Cadence::Next::PendingReal)pending={};
}
void Presenter::Reset() {
    cadence.Reset();pending={};previousSerial=0;midpointSerial=0;
    if(!DepthCaptureEnabled() && !MotionCaptureEnabled())ReleaseInputs();
}
}
