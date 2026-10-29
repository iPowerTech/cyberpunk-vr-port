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
    Cadence::Next kind=Cadence::Next::Real;
};
// Hands each captured frame to the XR submission unchanged and tells a new real frame from a
// repeat of the one already displayed (FPS statistics).
class Presenter {
public:
    Selection Select(Frame,bool gameplay);
    void Submitted(const Selection&,bool accepted);
    void Reset();
private:
    Cadence cadence;
};
}
