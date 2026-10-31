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
Selection Presenter::Select(Frame current,bool gameplay,bool midpointShown) {
    if(!gameplay)Reset();
    if(!DepthCaptureEnabled() && !MotionCaptureEnabled())ReleaseInputs();
    if(MetricsEnabled())SetVram(InputVram());
    const uint64_t serial=current.identity.serial;
    if(!serial || serial==shownSerial)return {std::move(current),Next::Repeat,false};
    if(midpointShown)return {std::move(current),Next::RealAfterMidpoint,false};
    // FSR3 interpolated `serial` from the capture before it, and that one is the real frame shown last.
    ID3D12Resource* generated[2]{};
    if(gameplay && fsr3::ShowGenerated() && shownSerial && serial==shownSerial+1 && current.identity.origin==shownOrigin &&
       (generated[0]=fsr3::Output(0,serial))!=nullptr && (generated[1]=fsr3::Output(1,serial))!=nullptr) {
        Selection result{std::move(current),Next::Midpoint,true};
        result.frame.identity.capturedMs=(shownCapturedMs+result.frame.identity.capturedMs)*.5;
        for(unsigned eye=0;eye<2;++eye) {
            result.frame.color[eye]=generated[eye];
            result.frame.pose[eye]=Middle(shownPose[eye],result.frame.pose[eye]);
            result.frame.identity.pose[eye]=0;   // blended pixels have a blended pose, not the capture's receipt
        }
        return result;
    }
    return {std::move(current),Next::Real,false};
}
void Presenter::Submitted(const Selection& result,bool accepted) {
    if(!accepted)return;   // the XR loop takes the same capture again next cycle
    const auto& f=result.frame;
    if(fsr3::Enabled()) {
        const auto gap=shownSerial ? f.identity.serial-shownSerial : 0;
        fsr3::CountShown(result.kind==Next::Midpoint ? fsr3::Shown::Midpoint :
            result.kind==Next::RealAfterMidpoint ? fsr3::Shown::RealAfterMidpoint :
            result.kind==Next::Real ? fsr3::Shown::Real : fsr3::Shown::Repeat,gap);
    }
    if(result.kind==Next::Real || result.kind==Next::RealAfterMidpoint) {
        shownSerial=f.identity.serial;shownOrigin=f.identity.origin;shownCapturedMs=f.identity.capturedMs;
        shownPose[0]=f.pose[0];shownPose[1]=f.pose[1];
    }
    if(MetricsEnabled())OnSubmitted(f.identity.serial,result.synthetic,result.kind==Next::Repeat,NowMs());
}
void Presenter::Reset() {
    shownSerial=0;
    if(!DepthCaptureEnabled() && !MotionCaptureEnabled())ReleaseInputs();
}
}
