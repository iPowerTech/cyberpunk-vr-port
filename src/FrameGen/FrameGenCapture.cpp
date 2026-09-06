#include "FrameGen/FrameGenCapture.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

float HalfToFloat(uint16_t h) {
    uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x3FFu;
    uint32_t f;
    if (exp == 0) {
        if (mant == 0) {
            f = sign;  // +/-0
        } else {
            // Subnormal half -> normalize into a normal float.
            int e = -1;
            do {
                mant <<= 1;
                ++e;
            } while ((mant & 0x400u) == 0);
            mant &= 0x3FFu;
            // e counts shifts-minus-one (starts at -1, do-while increments once per shift), so
            // the true shift count is e+1 and the biased exponent is 113-(e+1) = 112-e. Traced
            // against the smallest denormal (mant=1) to confirm: expected float exponent 103,
            // this gives 112-9=103. (Previously off by one -- decoded subnormals 2x too bright;
            // negligible for this capture's typical HDR range but wrong is wrong.)
            f = sign | ((112u - static_cast<uint32_t>(e)) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        f = sign | 0x7F800000u | (mant << 13);  // inf/NaN
    } else {
        f = sign | ((exp + 112u) << 23) | (mant << 13);
    }
    float out;
    std::memcpy(&out, &f, sizeof(out));
    return out;
}

// Reinhard tonemap (matches the write-back codec's own decode direction in
// Hooks/NgxEvalPassthrough.cpp -- same convention, so this capture reads the way that code does)
// then sRGB encode, per channel.
uint8_t ToneMapEncode(float c) {
    c = c < 0.0f ? 0.0f : c;
    c = c / (1.0f + c);
    c = (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
    c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    return static_cast<uint8_t>(c * 255.0f + 0.5f);
}

#pragma pack(push, 1)
struct BmpFileHeader {
    uint16_t type = 0x4D42;  // 'BM'
    uint32_t fileSize = 0;
    uint16_t reserved1 = 0;
    uint16_t reserved2 = 0;
    uint32_t dataOffset = 54;
};
struct BmpInfoHeader {
    uint32_t headerSize = 40;
    int32_t width = 0;
    int32_t height = 0;
    uint16_t planes = 1;
    uint16_t bitsPerPixel = 24;
    uint32_t compression = 0;
    uint32_t imageSize = 0;
    int32_t xPixelsPerMeter = 2835;
    int32_t yPixelsPerMeter = 2835;
    uint32_t colorsUsed = 0;
    uint32_t colorsImportant = 0;
};
#pragma pack(pop)

}  // namespace

bool FrameGenCapture_WriteBmp(const char* path, const uint8_t* mappedData, uint32_t rowPitch,
                               int width, int height) {
    if (!path || !mappedData || width <= 0 || height <= 0) return false;

    const uint32_t outRowBytes = (static_cast<uint32_t>(width) * 3 + 3) & ~3u;  // 4-byte aligned
    std::vector<uint8_t> pixels(static_cast<size_t>(outRowBytes) * height, 0);

    for (int y = 0; y < height; ++y) {
        // BMP rows are bottom-up; source rows are top-down.
        const uint8_t* srcRow = mappedData + static_cast<size_t>(y) * rowPitch;
        uint8_t* dstRow = pixels.data() + static_cast<size_t>(height - 1 - y) * outRowBytes;
        for (int x = 0; x < width; ++x) {
            const uint16_t* texel = reinterpret_cast<const uint16_t*>(srcRow) + x * 4;  // RGBA16F
            const float r = HalfToFloat(texel[0]);
            const float g = HalfToFloat(texel[1]);
            const float b = HalfToFloat(texel[2]);
            uint8_t* dstPixel = dstRow + x * 3;
            dstPixel[0] = ToneMapEncode(b);  // BMP is BGR
            dstPixel[1] = ToneMapEncode(g);
            dstPixel[2] = ToneMapEncode(r);
        }
    }

    BmpFileHeader fh;
    BmpInfoHeader ih;
    ih.width = width;
    ih.height = height;
    ih.imageSize = static_cast<uint32_t>(pixels.size());
    fh.fileSize = fh.dataOffset + ih.imageSize;

    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) return false;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    fwrite(pixels.data(), 1, pixels.size(), f);
    fclose(f);
    return true;
}
