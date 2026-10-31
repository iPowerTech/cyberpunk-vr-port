#pragma once
#include <cstdint>

namespace cvr::framegen {
struct Identity {
    uint64_t serial{}, origin{};
    uint32_t nativeFrame{};
    uint64_t pose[2]{};
    double capturedMs{};
    bool stereo{};
};
// What one XR submission shows (Framegen/Presenter.hpp): a new real frame, the real frame shown last again,
// FSR3's frame interpolated between the real frame shown last and a new one, or that new real frame after it.
enum class Next { Repeat, Real, Midpoint, RealAfterMidpoint };
}
