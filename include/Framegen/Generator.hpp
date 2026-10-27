#pragma once
#include "Framegen/Inputs.hpp"
#include "Framegen/Framegen.hpp"
#include <memory>

namespace cvr::framegen {
// The eye camera a real frame was rendered and submitted with: orientation (x,y,z,w) in the XR
// space and the field of view's angles in radians (left, right, up, down). Recorded with a frame
// capture (FrameCapture.hpp) so a replay can show each output the way the headset runtime does.
struct EyeCamera {float orientation[4]{0,0,0,1};float fov[4]{};};
struct EyePair {EyeCamera previous,current;};
// Consumes the port's existing color snapshots. No private XR swapchains,
// duplicate color-capture ring, readback, or second presenter is created.
class Generator {
public:
    Generator();
    ~Generator();
    Generator(const Generator&)=delete;
    Generator& operator=(const Generator&)=delete;
    bool Generate(ID3D12Device*,ID3D12CommandQueue*,ID3D12Resource* colors[2],
        const std::shared_ptr<const Inputs> (&inputs)[2],const EyePair (&cameras)[2],const Settings&,
        double frameMs,bool reset,uint64_t frameId,bool* interpolated=nullptr);
    ID3D12Resource* Output(unsigned eye) const;
    void ConsumerFence(ID3D12Fence*,uint64_t);
    bool Idle() const;
    void Poll(); // release completed input leases even during repeats/real endpoints
    // Nonblocking retirement: false means a previous GPU consumer still owns it.
    bool Reset();
    uint64_t Bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
