#include "Framegen/Presenter.hpp"

namespace cvr::framegen {
Selection Presenter::Select(Frame current,bool gameplay) {
    if(!gameplay)Reset();
    if(!DepthCaptureEnabled())ReleaseInputs();
    if(MetricsEnabled())SetVram(InputVram());
    const auto kind=cadence.Select(current.identity.serial);
    return {std::move(current),kind};
}
void Presenter::Submitted(const Selection& result,bool accepted) {
    if(!accepted) {Reset();return;}
    cadence.Accepted(result.kind,result.frame.identity.serial);
    if(MetricsEnabled())OnSubmitted(result.frame.identity.serial,false,result.kind==Cadence::Next::Repeat,NowMs());
}
void Presenter::Reset() {
    cadence.Reset();
    if(!DepthCaptureEnabled())ReleaseInputs();
}
}
