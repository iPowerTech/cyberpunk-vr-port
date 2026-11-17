#pragma once
#include <algorithm>
#include <cmath>

// ENGINE HAPTICS (fork) -- what the hand on the wheel feels from the engine while driving.
//
// The source is the engine's revolutions as the game itself publishes them for the rev counter
// (the vehicle blackboard's Vehicle.RPMValue / RPMMax, read by mods/cet/CyberpunkVRPort_Driving and
// handed over with SetVRVehicleEngine). Revs rise with the throttle, drop at a gear change and sit at
// idle when the car stands, so the hand follows the engine and not the loudness of the sound.
//
// THE "FREQUENCY" IS A BEAT RATE. The controllers render OpenXR frequency badly (Runtimes/Haptics.cpp),
// so the engine is a train of short beats: at idle few and faint, on the throttle denser and stronger.
// The XR frame thread plays them, so the fastest rate stays well under the display rate.
namespace cvr::haptics {

struct EngineBeat { float amplitude{}; float periodSec{}; float pulseSec{}; };

inline constexpr float kEngineAmpIdle  = 0.06f;   // barely there with the engine idling
inline constexpr float kEngineAmpFull  = 0.55f;   // at the rev limit
inline constexpr float kEngineRateIdle = 8.0f;    // beats per second at idle
inline constexpr float kEngineRateFull = 38.0f;   // ...and at the rev limit
inline constexpr float kEnginePulseMin = 0.014f;  // a beat under ~12 ms can fall between two XR frames

// Revs as a fraction of the rev limit. No usable limit: the 7100 the game's overheat effect counts to.
inline float EngineLoad(float rpm, float rpmMax) {
    if (!std::isfinite(rpm) || rpm <= 0.0f) return 0.0f;
    if (!std::isfinite(rpmMax) || rpmMax < 100.0f) rpmMax = 7100.0f;
    return std::clamp(rpm / rpmMax, 0.0f, 1.0f);
}

inline EngineBeat EngineBeatFor(float load) {
    if (!std::isfinite(load)) load = 0.0f;
    load = std::clamp(load, 0.0f, 1.0f);
    EngineBeat beat;
    beat.amplitude = kEngineAmpIdle + (kEngineAmpFull - kEngineAmpIdle) * load;
    const float rate = kEngineRateIdle + (kEngineRateFull - kEngineRateIdle) * load;
    beat.periodSec = 1.0f / rate;
    beat.pulseSec = std::max(kEnginePulseMin, 0.45f * beat.periodSec);
    return beat;
}

}  // namespace cvr::haptics
