#pragma once
#include "Framegen/Inputs.hpp"
#include <cstdint>
#include <memory>

// FSR3 FRAME GENERATION -- the fork's vr_framegen generator on AMD FidelityFX frame interpolation
// (SDK fsr3-v3.0.4, cmake/Fsr3.cmake). One interpolated frame per eye between two consecutive
// captures, from the captured eye images and the engine's own depth and motion vectors (dilated
// first, Framegen/Fsr3Dilate.hpp), optionally with FidelityFX optical flow on top.
//
// It runs in the headset's path, not the game's: the XR loop (Runtimes/OpenXRFrameLoop.cpp) takes each
// new capture once the game's GPU has finished it, interpolates on FSR3's own compute queue and shows
// the interpolated frame first and the real capture half a game frame after (Framegen/Presenter.cpp).
// The headset's queue waits for the interpolated frame; the game's queue never waits for it.
namespace cvr::fsr3 {
struct Settings {
    bool enabled=false;
    bool opticalFlow=true;   // FidelityFX optical flow for what the engine's vectors miss
    // The interpolated frames go to the headset. Off, FSR3 still interpolates and the headset gets only
    // the real frames: what showing them costs, apart from what computing them costs. Not saved.
    bool show=true;
};
Settings GetSettings();
void SetSettings(Settings);
bool Enabled();   // switched on and not stopped by a fault
bool ShowGenerated();   // Enabled() and its frames go to the headset
struct GpuWait {ID3D12Fence* fence;uint64_t value;};
// On FSR3's own COMPUTE queue -- async compute, next to the game's rendering on the GPU rather than in turn with
// it -- once each of `waits` is reached there (the capture; the XR loop's copies so far, which read the
// interpolated frames rewritten here): interpolate between the previous capture and this one. colors are the
// two eye images as captured (they rest in COPY_SOURCE), inputs their depth and motion vectors. A capture that
// does not follow the previous one only primes the history.
void Interpolate(ID3D12Device*,const GpuWait (&waits)[2],ID3D12Resource* const colors[2],
    const std::shared_ptr<const framegen::Inputs> (&inputs)[2],uint64_t serial,double frameMs);
// `queue` waits on the GPU for the last Interpolate: its frames, and its reads of the capture.
void QueueWaitForLast(ID3D12CommandQueue* queue);
// The interpolation's GPU time, both eyes, smoothed (0 until measured). An interpolated frame fits between two real
// ones only if it is made in half the time between them; for game frames shorter than twice this none is inserted
// (the XR loop asks, CountTooFast counts it for the report).
double GenerationMs();
void CountTooFast();
// An interpolated frame made but not shown: the game's next capture is due before the display period its real
// frame would move to, and would go a period late (the XR loop decides).
void CountNoSlot();
// None interpolated: the game makes frames faster than the display shows them, there is no period for one (the XR
// loop decides). Its real frames' copies then wait for no dispatch.
void CountOutrun();
// The frame interpolated just before `serial`'s capture, per eye (resting in COPY_SOURCE, the
// capture's own size and format), or null when there is none.
ID3D12Resource* Output(unsigned eye,uint64_t serial);
struct Status {
    bool ready{},faulted{};
    uint64_t interpolated{},primed{},skipped{};
    char text[160]{};
};
Status GetStatus();
void ClearFault();
// What the XR loop showed with FSR3 on (Framegen/Presenter.cpp), for the once-a-second report:
// `gap` is how many captures on from the last real frame shown this one is (1 = the next one).
enum class Shown { Midpoint, RealAfterMidpoint, Real, Repeat };
void CountShown(Shown,uint64_t gap);
// [fsr3] line of the XR frame-path report (F10 GENERAL > Diagnostics), once a second.
void Report();
bool ParseSetting(const char*,Settings&);
void WriteSettings(void* file,const Settings&);
}
