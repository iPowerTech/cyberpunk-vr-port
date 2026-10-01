#pragma once
#include "Framegen/Inputs.hpp"
#include "Framegen/Framegen.hpp"
#include <memory>

namespace cvr::framegen {
// Consumes the port's existing color snapshots. No private XR swapchains,
// duplicate color-capture ring, readback, or second presenter is created.
class Generator {
public:
    Generator();
    ~Generator();
    Generator(const Generator&)=delete;
    Generator& operator=(const Generator&)=delete;
    bool Generate(ID3D12Device*,ID3D12CommandQueue*,ID3D12Resource* colors[2],
        const std::shared_ptr<const Inputs> (&inputs)[2],const Settings&,
        double frameMs,bool reset,uint64_t frameId,bool* interpolated=nullptr);
    ID3D12Resource* Output(unsigned eye) const;
    void ConsumerFence(ID3D12Fence*,uint64_t);
    bool Idle() const;
    // Nonblocking retirement: false means a previous GPU consumer still owns it.
    bool Reset();
    uint64_t Bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
