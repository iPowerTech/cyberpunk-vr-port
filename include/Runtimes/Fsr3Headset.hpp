#pragma once
#include <d3d12.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cstdint>
#include <mutex>
#include <vector>

// THE HEADSET SIDE OF FSR3 (Framegen/Fsr3.hpp). Two more sets of eye swapchains: set 0 takes the
// interpolated frame, set 1 the real one. Both are filled at the Present, right after FSR3, on the game's
// queue -- before the game queues its next frame, so neither copy waits for it. The XR loop then only
// says which set to show at each display cycle: the interpolated frame first, the real one half a frame
// later, each held until the next is due. It does no GPU work for them, so it keeps the display's pace
// while the GPU renders the next frame.
namespace cvr::fsr3 {
class Headset {
public:
    struct Pick {
        int set=-1;               // -1: nothing to show from here, the XR loop's own path
        uint64_t serial{};
        XrPosef pose[2]{};
        XrFovf fov[2]{};
        bool synthetic{},fresh{};   // fresh: first cycle that shows this set's current image
    };
    // XR thread: (re)creates both sets to match the eye swapchains.
    bool Ensure(XrSession,int64_t format,int32_t width,int32_t height);
    void Destroy();
    // Present thread, after FSR3 interpolated `serial`: copies interpolated[eye] into set 0 and real[eye]
    // into set 1 on `queue`. False leaves the sets as they were.
    bool Fill(ID3D12Device*,ID3D12CommandQueue*,uint64_t serial,ID3D12Resource* const interpolated[2],
        ID3D12Resource* const real[2],const XrPosef (&midPose)[2],const XrPosef (&realPose)[2],const XrFovf fov[2],double frameMs);
    // XR thread, once per display cycle. `newest` is the newest capture; `spaced` holds the real frame until
    // half a frame after the interpolated one (the submit thread), otherwise it goes out on the next cycle
    // (the inline pump, two cycles per Present). On a pick, `lock` keeps the sets as they are until the
    // cycle has called xrEndFrame.
    Pick Choose(XrTime displayTime,XrTime period,uint64_t newest,bool spaced,std::unique_lock<std::timed_mutex>& lock);
    XrSwapchain Handle(int set,unsigned eye) const {return sets[set][eye].handle;}
private:
    struct Swapchain {
        XrSwapchain handle=XR_NULL_HANDLE;
        std::vector<XrSwapchainImageD3D12KHR> images;
        uint32_t index{};
        bool acquired{},waited{};
    };
    Swapchain sets[2][2];
    int32_t width{},height{};
    int64_t format{};
    std::timed_mutex mutex;
    // What the sets hold.
    uint64_t serial{};
    XrPosef midPose[2]{},realPose[2]{};
    XrFovf fov[2]{};
    double frameMs{};
    bool filled{};
    // What the XR loop shows.
    uint64_t shownSerial{};
    int shownSet=-1;
    XrTime midTime{};
    // Our own lists on the game's queue.
    static constexpr int kLists=3;
    ID3D12CommandAllocator* allocators[kLists]{};
    ID3D12GraphicsCommandList* lists[kLists]{};
    uint64_t listFence[kLists]{};
    int next{};
    ID3D12Fence* fence{};
    uint64_t fenceValue{};
    HANDLE event{};
};
}
