#pragma once
// In-place constant patches of signed DXIL containers (docs/hidden-area-mask-20261009.md).
//
// A float constant in [1, 2) has bit 29 as its highest set bit, so in the LLVM bitcode of the DXIL
// part it is always six VBR6 chunks (36 bits) long. Writing another value from that range at the
// same bit offset keeps every other bit of the container where it was; only the container hash has
// to be recomputed, which is the DXBC MD5 variant D3D12 checks.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cvr::dxil_patch {
using Hash=std::array<uint8_t,16>;
// The hash stored at offset 4 of a container of `size` bytes.
Hash ContainerHash(const uint8_t* container,size_t size);
// Store ContainerHash at offset 4.
bool Sign(std::vector<uint8_t>& container);
// Byte range of the LLVM bitcode inside the container's DXIL part.
bool BitcodeRange(const uint8_t* container,size_t size,size_t& start,size_t& length);
bool PlaceholderValue(float);   // in [1, 2)
// Container bit offsets of every occurrence of the placeholder's VBR6 pattern in the bitcode.
std::vector<size_t> FindFloat(const uint8_t* container,size_t size,float placeholder);
// Write `value` (in [1, 2)) at a bit offset found for a placeholder. The hash is not updated.
bool WriteFloat(std::vector<uint8_t>& container,size_t bit,float value);
}
