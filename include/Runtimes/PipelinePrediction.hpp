#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cvr::tracking {
// The render camera targets its estimated presentation slot, not the older XR cycle which happens
// to be running while the engine starts that camera. Bound/slew the additional horizon so a hitch
// or a changed cadence cannot turn a stale regression into a large prediction jump.
class PipelinePrediction {
    double leadNs{};
    int64_t lastDisplay{};
public:
    void Reset() {leadNs=0;lastDisplay=0;}
    int64_t Target(int64_t display,int64_t period,int64_t fitted,bool valid,float strength) {
        if(display<=0 || period<4000000 || period>40000000) {Reset();return display;}
        if(lastDisplay && (display<=lastDisplay || display-lastDisplay>250000000))Reset();
        const double step=lastDisplay ? std::min<int64_t>(display-lastDisplay,40000000) : period;
        lastDisplay=display;
        const double maximum=std::min<double>(2.0*period,25000000.0);
        // A fit beyond the short pipeline bound is not a license to extrapolate arbitrarily.
        const bool plausible=valid && fitted>=display-period && fitted<=display+4*period;
        const double desired=plausible ? std::clamp<double>(double(fitted-display),0,maximum) : 0;
        const double filtered=leadNs+(desired-leadNs)*(1.0-std::exp(-step/100000000.0));
        const double slew=step*.10; // at most 1.1 ms change per 90 Hz display interval
        leadNs=std::clamp(filtered,leadNs-slew,leadNs+slew);
        const float scale=std::isfinite(strength)?std::clamp(strength,0.0f,1.0f):0.0f;
        return display+static_cast<int64_t>(std::clamp(leadNs,0.0,maximum)*scale);
    }
};
}
