#pragma once
#include <cstddef>
#include <cstdint>

namespace cvr::markers {
// Synchronous native uploaders copy this scratch data before returning.
bool RewriteConstants(uint32_t size,const void* source,void* scratch);
}
