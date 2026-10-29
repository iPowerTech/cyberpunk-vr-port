#include "Framegen/Presenter.hpp"
#include "Framegen/Fsr3.hpp"
#include <algorithm>
#include <cmath>

namespace cvr::framegen {
namespace {
XrPosef Middle(const XrPosef& a,const XrPosef& b) {
    XrPosef out{};out.position={(a.position.x+b.position.x)*.5f,(a.position.y+b.position.y)*.5f,(a.position.z+b.position.z)*.5f};
    const auto& x=a.orientation;const auto& y=b.orientation;
    const float sign=x.x*y.x+x.y*y.y+x.z*y.z+x.w*y.w<0 ? -1.0f : 1.0f;
    out.orientation={x.x+sign*y.x,x.y+sign*y.y,x.z+sign*y.z,x.w+sign*y.w};
    auto& q=out.orientation;const float norm=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(norm>0) {q.x/=norm;q.y/=norm;q.z/=norm;q.w/=norm;}else q={0,0,0,1};
    return out;
}
}
Selection Presenter::Select(Frame current,bool gameplay) {
    const bool generate=gameplay && fsr3::Enabled();
    if(!gameplay)Reset();
    else if(!generate && cadence.Pending()) {cadence.Reset();pending={};}
    if(!DepthCaptureEnabled() && !MotionCaptureEnabled())ReleaseInputs();
    if(MetricsEnabled())SetVram(InputVram());
    if(cadence.Pending())return {pending,Cadence::Next::PendingReal,false};
    // FSR3 interpolated `serial` from the capture before it; shown only when that one was the last shown.
    ID3D12Resource* generated[2]{};
    const uint64_t serial=current.identity.serial;
    const bool midpoint=generate && previousSerial && serial==previousSerial+1 && current.identity.origin==previousOrigin &&
        (generated[0]=fsr3::Output(0,serial))!=nullptr && (generated[1]=fsr3::Output(1,serial))!=nullptr;
    const auto kind=cadence.Select(serial,midpoint);
    if(kind!=Cadence::Next::Midpoint)return {std::move(current),kind,false};
    Selection result{current,kind,true};
    result.frame.lease.reset();
    result.frame.identity.capturedMs=(previousCapturedMs+current.identity.capturedMs)*.5;
    for(unsigned eye=0;eye<2;++eye) {
        result.frame.color[eye]=generated[eye];
        result.frame.pose[eye]=Middle(previousPose[eye],current.pose[eye]);
        result.frame.identity.pose[eye]=0;   // blended pixels have a blended pose, not the capture's receipt
    }
    pending=std::move(current);
    return result;
}
void Presenter::Submitted(const Selection& result,uint64_t fenceValue,bool accepted) {
    if(result.frame.lease)result.frame.lease->fence=std::max(result.frame.lease->fence,fenceValue);
    if(!accepted) {Reset();return;}
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
    cadence.Reset();pending={};previousSerial=0;
    if(!DepthCaptureEnabled() && !MotionCaptureEnabled())ReleaseInputs();
}
}
