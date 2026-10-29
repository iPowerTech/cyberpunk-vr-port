#pragma once
#include "Framegen/Framegen.hpp"
#include "Framegen/FramePolicy.hpp"
#include "Framegen/Inputs.hpp"
#include <memory>
#include <openxr/openxr.h>

namespace cvr::framegen {
// Keeps a capture's images out of the capture pool's reuse until the submission that shows them has
// left the GPU: a real frame shown one cycle after its midpoint is no longer the newest capture.
struct ReadLease {
    using Release=void (*)(void*,ID3D12Resource*,uint64_t);
    void* owner{};Release release{};
    ID3D12Resource* resources[2]{};
    uint64_t fence{};
    ~ReadLease() {if(release)for(auto* resource:resources)if(resource)release(owner,resource,fence);}
};
struct Frame {
    Identity identity;
    ID3D12Resource* color[2]{};
    XrPosef pose[2]{};
    XrFovf fov[2]{};
    std::shared_ptr<const Inputs> inputs[2];
    std::shared_ptr<ReadLease> lease;
};
struct Selection {
    Frame frame;
    Cadence::Next kind=Cadence::Next::Real;
    bool synthetic=false;   // FSR3's interpolated frame
};
// Picks what the XR submission shows: the newest capture, a repeat, or with FSR3 on (Framegen/Fsr3.hpp)
// the frame interpolated before a new capture, then that capture.
class Presenter {
public:
    Selection Select(Frame,bool gameplay);
    void Submitted(const Selection&,uint64_t fenceValue,bool accepted);
    void Reset();
private:
    Cadence cadence;
    Frame pending;
    // The last real frame shown: a midpoint sits halfway between it and the next one.
    uint64_t previousSerial{},previousOrigin{};
    XrPosef previousPose[2]{};
    double previousCapturedMs{};
};
}
