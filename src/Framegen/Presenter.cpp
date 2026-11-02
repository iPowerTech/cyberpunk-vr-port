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
XrQuaternionf Multiply(const XrQuaternionf& p,const XrQuaternionf& q) {
    return {p.w*q.x+p.x*q.w+p.y*q.z-p.z*q.y, p.w*q.y-p.x*q.z+p.y*q.w+p.z*q.x,
            p.w*q.z+p.x*q.y-p.y*q.x+p.z*q.w, p.w*q.w-p.x*q.x-p.y*q.y-p.z*q.z};
}
// b moved on past it by `f` of the step from a to b: position b + f(b - a), and the rotation from a to b applied
// again, f of it, on top of b.
XrPosef Ahead(const XrPosef& a,const XrPosef& b,float f) {
    XrPosef out{};
    out.position={b.position.x+(b.position.x-a.position.x)*f,b.position.y+(b.position.y-a.position.y)*f,
                  b.position.z+(b.position.z-a.position.z)*f};
    XrQuaternionf from=a.orientation;
    if(from.x*b.orientation.x+from.y*b.orientation.y+from.z*b.orientation.z+from.w*b.orientation.w<0)
        from={-from.x,-from.y,-from.z,-from.w};
    auto step=Multiply(b.orientation,{-from.x,-from.y,-from.z,from.w});   // b = step * a
    if(step.w<0)step={-step.x,-step.y,-step.z,-step.w};
    const float s=std::sqrt((std::max)(0.0f,1.0f-step.w*step.w));
    out.orientation=b.orientation;
    if(s>1e-6f) {
        const float half=std::acos((std::min)(1.0f,step.w))*f;
        const float k=std::sin(half)/s;
        auto& q=out.orientation=Multiply({step.x*k,step.y*k,step.z*k,std::cos(half)},b.orientation);
        const float norm=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
        if(norm>0) {q.x/=norm;q.y/=norm;q.z/=norm;q.w/=norm;}else q=b.orientation;
    }
    return out;
}
}
Selection Presenter::Select(Frame current,bool gameplay,bool midpointShown,bool midpointFits,ID3D12Resource* const* extrapolated) {
    if(!gameplay)Reset();
    if(!DepthCaptureEnabled() && !MotionCaptureEnabled())ReleaseInputs();
    if(MetricsEnabled())SetVram(InputVram());
    const uint64_t serial=current.identity.serial;
    // The real frame shown last again -- moved half a game frame ahead when its extrapolated frame is there. The
    // pose goes ahead with the pixels, by the share of the head's motion one game frame is (the vectors cover one).
    if(serial && serial==shownSerial && gameplay && extrapolated && extrapolated[0] && extrapolated[1] &&
       previousSerial && shownSerial>previousSerial && current.identity.origin==shownOrigin) {
        Selection result{std::move(current),Next::Extrapolated,true};
        const float f=0.5f/float(shownSerial-previousSerial);
        for(unsigned eye=0;eye<2;++eye) {
            result.frame.color[eye]=extrapolated[eye];
            result.frame.pose[eye]=Ahead(previousPose[eye],shownPose[eye],f);
            result.frame.identity.pose[eye]=0;   // a pose of its own, not the capture's receipt
        }
        return result;
    }
    if(!serial || serial==shownSerial)return {std::move(current),Next::Repeat,false};
    if(midpointShown)return {std::move(current),Next::RealAfterMidpoint,false};
    // FSR3 interpolated `serial` from the capture before it, and that one is the real frame shown last.
    ID3D12Resource* generated[2]{};
    if(gameplay && midpointFits && fsr3::ShowGenerated() && shownSerial && serial==shownSerial+1 && current.identity.origin==shownOrigin &&
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
            result.kind==Next::Extrapolated ? fsr3::Shown::Extrapolated :
            result.kind==Next::Real ? fsr3::Shown::Real : fsr3::Shown::Repeat,gap);
    }
    if(result.kind==Next::Real || result.kind==Next::RealAfterMidpoint) {
        // The one before keeps its pose only when it is the same tracking space.
        previousSerial=f.identity.origin==shownOrigin ? shownSerial : 0;
        previousPose[0]=shownPose[0];previousPose[1]=shownPose[1];
        shownSerial=f.identity.serial;shownOrigin=f.identity.origin;shownCapturedMs=f.identity.capturedMs;
        shownPose[0]=f.pose[0];shownPose[1]=f.pose[1];
    }
    if(MetricsEnabled())OnSubmitted(f.identity.serial,result.synthetic,result.kind==Next::Repeat,NowMs());
}
void Presenter::Reset() {
    shownSerial=0;previousSerial=0;
    if(!DepthCaptureEnabled() && !MotionCaptureEnabled())ReleaseInputs();
}
}
