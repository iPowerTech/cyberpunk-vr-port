#include "Framegen/Presenter.hpp"
#include <algorithm>
#include <cmath>
#include <psapi.h>

namespace cvr::framegen {
namespace {
bool ExternalBridgeLoaded() {
    static uint64_t nextCheck=0;static bool loaded=false;
    const auto now=GetTickCount64();if(now<nextCheck)return loaded;nextCheck=now+2000;loaded=false;
    HMODULE modules[1024]{};DWORD bytes{};
    if(EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&bytes)) {
        const auto count=std::min<size_t>(bytes/sizeof(HMODULE),std::size(modules));
        for(size_t i=0;i<count;++i) {
            wchar_t name[MAX_PATH]{};GetModuleBaseNameW(GetCurrentProcess(),modules[i],name,MAX_PATH);
            if(_wcsnicmp(name,L"XR_APILAYER_XRFrameBridge",23)==0) {loaded=true;break;}
        }
    }
    return loaded;
}
XrPosef Middle(const XrPosef& a,const XrPosef& b) {
    XrPosef out{};out.position={(a.position.x+b.position.x)*.5f,(a.position.y+b.position.y)*.5f,(a.position.z+b.position.z)*.5f};
    const auto& x=a.orientation;const auto& y=b.orientation;
    const float sign=x.x*y.x+x.y*y.y+x.z*y.z+x.w*y.w<0 ? -1.0f : 1.0f;
    out.orientation={x.x+sign*y.x,x.y+sign*y.y,x.z+sign*y.z,x.w+sign*y.w};
    auto& q=out.orientation;const float norm=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(norm>0) {q.x/=norm;q.y/=norm;q.z/=norm;q.w/=norm;}else q={0,0,0,1};
    return out;
}
bool SameProjection(const Frame& a,const Frame& b) {
    for(unsigned eye=0;eye<2;++eye) {
        const auto& x=a.fov[eye];const auto& y=b.fov[eye];
        if(std::abs(x.angleLeft-y.angleLeft)>.001f || std::abs(x.angleRight-y.angleRight)>.001f ||
           std::abs(x.angleUp-y.angleUp)>.001f || std::abs(x.angleDown-y.angleDown)>.001f)return false;
        const auto& q=a.pose[eye].orientation;const auto& r=b.pose[eye].orientation;
        if(std::abs(q.x*r.x+q.y*r.y+q.z*r.z+q.w*r.w)<.95f)return false;
    }
    return true;
}
}
Selection Presenter::Select(ID3D12Device* device,ID3D12CommandQueue* queue,Frame current,bool gameplay) {
    const auto settings=GetSettings();
    if(!gameplay || (pending.identity.serial && pending.identity.origin!=current.identity.origin))Reset();
    if(cadence.Pending())return {pending,Cadence::Next::PendingReal,{},false};
    Selection result{current,cadence.Select(current.identity.serial,false,false),{},false};
    if(!settings.enabled || !gameplay) {
        primed=false;previous={};generatedSerial=0;
        PacingReady(false);
        if(generator.Reset()) {ReleaseInputs();if(MetricsEnabled())SetVram(InputVram());}
        Status(settings.enabled ? "Paused in menus or untracked views" : "Disabled");return result;
    }
    if(ExternalBridgeLoaded()) {
        Reset();Status("External OFXR is loaded: disarm its tray and restart before native generation");return result;
    }
    if(!current.identity.inputs || !current.identity.stereo || !current.lease) {
        primed=false;PacingReady(false);Status("Waiting for matching depth and motion vectors in both eyes");return result;
    }
    if(current.identity.serial==generatedSerial)return result;
    const bool interpolate=primed && CanInterpolate(previous.identity,current.identity) && SameProjection(previous,current);
    const double milliseconds=interpolate ? current.identity.capturedMs-previous.identity.capturedMs : 1000.0/45;
    bool generated=false;
    if(!generator.Generate(device,queue,current.color,current.inputs,settings,milliseconds,!interpolate,++sdkFrame,&generated)) {
        primed=false;OnSkip();return result;
    }
    generatedSerial=current.identity.serial;
    PacingReady(true);
    if(interpolate && generated) {
        result.kind=Cadence::Next::Midpoint;result.synthetic=true;result.generatedFrom=current.lease;
        for(unsigned eye=0;eye<2;++eye) {
            result.frame.color[eye]=generator.Output(eye);
            result.frame.pose[eye]=Middle(previous.pose[eye],current.pose[eye]);
            result.frame.identity.pose[eye]=0; // synthetic pixels have a blended pose, not B's receipt
        }
        pending=current;
        Status("Generating with engine motion vectors and depth",true);
    } else {
        result.generatedFrom=current.lease;
        Status("Priming frame history",false);
    }
    // The SDK owns the previous-color history. Keep metadata only, not another
    // reference/pin/copy of the port's old capture buffers.
    previous=current;previous.lease.reset();previous.inputs[0].reset();previous.inputs[1].reset();
    previous.color[0]=previous.color[1]=nullptr;primed=true;
    return result;
}
void Presenter::Submitted(const Selection& result,ID3D12Fence* fence,uint64_t value,bool accepted) {
    if(result.frame.lease)result.frame.lease->fence=std::max(result.frame.lease->fence,value);
    if(result.generatedFrom)result.generatedFrom->fence=std::max(result.generatedFrom->fence,value);
    generator.ConsumerFence(fence,value);
    if(!accepted) {Reset();return;}
    cadence.Accepted(result.kind,result.frame.identity.serial);
    if(MetricsEnabled())OnSubmitted(result.frame.identity.serial,result.synthetic,result.kind==Cadence::Next::Repeat,NowMs());
    if(result.kind==Cadence::Next::PendingReal)pending={};
}
void Presenter::Reset() {
    PacingReady(false);
    cadence.Reset();pending={};previous={};primed=false;generatedSerial=0;
    if(generator.Reset())ReleaseInputs();
}
}
