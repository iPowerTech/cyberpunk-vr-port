#pragma once
#include <cstdint>

namespace cvr::stereo::early_depth {
inline constexpr uint32_t AllDraws=UINT32_MAX;
inline constexpr uint32_t MaxDraws=1024;
inline constexpr uint32_t MaxInstanceRows=MaxDraws*4;
inline constexpr uint32_t MaxInstanceBytes=8*1024*1024;
}
