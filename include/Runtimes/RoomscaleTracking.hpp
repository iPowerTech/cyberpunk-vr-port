#pragma once
#include <openxr/openxr.h>

namespace cvr::roomscale {
constexpr bool HeadTrackingUsable(XrSessionState state, XrSpaceLocationFlags flags) {
    // FOCUS gates action input, not the validity of head tracking. In particular,
    // OpenXR Simulator's file-command channel also updates poses while VISIBLE.
    constexpr XrSpaceLocationFlags required = XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
        XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    return (state == XR_SESSION_STATE_VISIBLE || state == XR_SESSION_STATE_FOCUSED) &&
           (flags & required) == required;
}
}
