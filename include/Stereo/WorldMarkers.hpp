#pragma once
#include <cstddef>
#include <cstdint>

namespace cvr::markers {
// The uploads RewriteConstants looks at: the canvas camera (Render/WorldMarkerProjection.hpp kInkCameraBytes)
// and a text glyph's matrix.
inline constexpr uint32_t kRewriteCameraBytes=864,kRewriteTextBytes=112;
inline constexpr uint32_t kRewriteScratchBytes=944;
// Synchronous native uploaders copy this scratch data before returning; it holds kRewriteCameraBytes.
bool RewriteConstants(uint32_t size,const void* source,void* scratch,uint32_t* shipped);
}
