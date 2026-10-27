#pragma once
#include <cmath>
#include <cstdint>

namespace cvr::tracking {
// Exact first-order response for elapsed seconds. Unlike min(1, rate*dt),
// subdividing the same interval does not change the result or introduce a
// snap at dt=1/rate. expm1 keeps small time steps accurate.
inline float ExponentialFollow(float rate, float dt) {
    if (!std::isfinite(rate) || !std::isfinite(dt) || rate<=0 || dt<=0) return 0;
    return -std::expm1(-rate*dt);
}

// Filtering may interpolate samples only within one tracking coordinate frame.
// Recenter is a change of coordinates, not physical travel toward a new target.
template<class Position,class Orientation>
bool RebaseTrackingFilter(uint64_t origin, uint64_t nowUs,
                          const Position& targetPosition, const Orientation& targetOrientation,
                          bool& initialized, uint64_t& savedOrigin, uint64_t& lastUs,
                          Position& position, Orientation& orientation) {
    if (initialized && savedOrigin==origin) return false;
    initialized=true;
    savedOrigin=origin;
    lastUs=nowUs;
    position=targetPosition;
    orientation=targetOrientation;
    return true;
}
}
