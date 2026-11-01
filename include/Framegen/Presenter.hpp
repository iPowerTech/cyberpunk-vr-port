#pragma once
#include "Framegen/Framegen.hpp"
#include "Framegen/FramePolicy.hpp"
#include "Framegen/Inputs.hpp"
#include <memory>
#include <openxr/openxr.h>

namespace cvr::framegen {
struct Frame {
    Identity identity;
    ID3D12Resource* color[2]{};
    XrPosef pose[2]{};
    XrFovf fov[2]{};
    std::shared_ptr<const Inputs> inputs[2];
};
struct Selection {
    Frame frame;
    Next kind=Next::Real;
    bool synthetic=false;   // FSR3's interpolated frame
};
// Picks what the XR submission shows for the capture the XR loop took. The loop hands it every capture once,
// in the order the game made them (Runtimes/OpenXRManager.hpp, m_captureQueue); with FSR3's frames shown,
// the frame interpolated between the real frame shown last and this capture goes first, then -- on the
// loop's next cycle, `midpointShown` -- the capture itself. `midpointFits`: the game's next capture is not due
// before the display period the real one moves to (the XR loop decides); else only the capture goes. No real
// frame is replaced or given up here.
class Presenter {
public:
    Selection Select(Frame,bool gameplay,bool midpointShown,bool midpointFits=true);
    void Submitted(const Selection&,bool accepted);
    void Reset();
private:
    // The last real frame shown: a midpoint sits halfway between it and the next one.
    uint64_t shownSerial{},shownOrigin{};
    XrPosef shownPose[2]{};
    double shownCapturedMs{};
};
}
