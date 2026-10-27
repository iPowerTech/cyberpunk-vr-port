#pragma once
#include <cstdint>

namespace cvr::capture {
// Native request types written by RVA 1D87940: Photo Mode uses 1, while
// savegame screenshot.png uses 3. The +1C0 active byte is shared by both.
constexpr uint32_t PhotoModeRequest = 1;
constexpr bool OverrideSize(bool requested, bool active, uint32_t type,
                            uint32_t vrWidth, uint32_t vrHeight) {
    return requested && active && type == PhotoModeRequest && vrWidth && vrHeight;
}
constexpr bool PreserveReadbackSize(bool photoReadback, uint32_t type,
                                    uint32_t width, uint32_t height,
                                    uint32_t vrWidth, uint32_t vrHeight) {
    return photoReadback && type == PhotoModeRequest && vrWidth && vrHeight &&
        width == vrWidth && height == vrHeight;
}
}
