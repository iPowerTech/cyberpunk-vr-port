#pragma once
#include "Framegen/Inputs.hpp"
#include <cstdint>
#include <memory>

// FSR3 FRAME GENERATION -- the fork's vr_framegen generator on AMD FidelityFX frame interpolation
// (SDK fsr3-v3.0.4, cmake/Fsr3.cmake). One interpolated frame per eye between two consecutive
// captures, from the captured eye images and the engine's own depth and motion vectors (dilated
// first, Framegen/Fsr3Dilate.hpp), optionally with FidelityFX optical flow on top.
//
// It runs right after a capture is submitted (Runtimes/OpenXRCapture.cpp), on the game's queue, in
// its own command list: the interpolated frame is on the GPU before the capture that follows it is
// handed to the XR loop, which shows it first and the real capture after (Framegen/Presenter.cpp).
namespace cvr::fsr3 {
struct Settings {
    bool enabled=false;
    bool opticalFlow=true;   // FidelityFX optical flow for what the engine's vectors miss
};
Settings GetSettings();
void SetSettings(Settings);
bool Enabled();   // switched on and not stopped by a fault
// After `serial`'s capture is submitted on `queue`: interpolate between the previous capture and this
// one. colors are the two eye images as captured (they rest in COPY_SOURCE), inputs their depth and
// motion vectors. A capture that does not follow the previous one only primes the history.
void Interpolate(ID3D12Device*,ID3D12CommandQueue*,ID3D12Resource* const colors[2],
    const std::shared_ptr<const framegen::Inputs> (&inputs)[2],uint64_t serial,double frameMs);
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
// RealDropped: the real frame after a midpoint gave way to the next capture's midpoint.
enum class Shown { Midpoint, RealAfterMidpoint, Real, Repeat, RealDropped };
void CountShown(Shown,uint64_t gap);
// [fsr3] line of the XR frame-path report (F10 GENERAL > Diagnostics), once a second.
void Report();
bool ParseSetting(const char*,Settings&);
void WriteSettings(void* file,const Settings&);
}
