#pragma once

#include <cstdint>

// Pure pixel-format conversion + file write for the FSR3 frame-gen debug capture (FrameGenFsr3.cpp
// owns the actual D3D12 readback: create the staging buffer, CopyTextureRegion, Map/Unmap). Kept
// separate because none of this needs a D3D12 type -- it just turns mapped bytes into a file.
//
// The scratch interpolated-output texture is R16G16B16A16_FLOAT (half-float RGBA, scene-linear).
// Written out as an 8-bit BGR .bmp: Reinhard tonemap + sRGB encode per channel, alpha dropped.
// mappedData/rowPitch come straight from ID3D12Resource::Map on the readback buffer -- rowPitch is
// the buffer's own (possibly padded) stride, not width*8.
bool FrameGenCapture_WriteBmp(const char* path, const uint8_t* mappedData, uint32_t rowPitch,
                               int width, int height);
