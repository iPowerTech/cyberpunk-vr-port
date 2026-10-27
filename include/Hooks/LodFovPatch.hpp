#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace cvr::lod_patch {

// CP2077 2.5 / 6AD55DAF. Full instruction guards include the RIP displacements.
// The main builder and constructor now test +399 and write +398 between tanf,
// squaring and scaling. The old contiguous patterns silently matched other builders.
struct Site { const char* name; uintptr_t rva; const char* bytes; size_t size; };
inline constexpr char kMainBytes[] =
    "\xF3\x0F\x10\x43\x20\xF3\x0F\x59\x05\xBF\x3E\x39\x03\xE8\xDA\xE5\x67\x01"
    "\xF6\x83\x99\x03\x00\x00\x01\xF3\x0F\x59\xC0\x40\x88\xB3\x98\x03\x00\x00"
    "\xF3\x0F\x59\x05\xD8\x43\x39\x03\xF3\x0F\x5F\x05\xB8\x42\x39\x03\xF3\x0F\x11\x43\x24";
inline constexpr char kConstructorBytes[] =
    "\xF3\x0F\x59\x05\x2F\x65\x5A\x03\xF3\x0F\x5F\x05\x0F\x64\x5A\x03\xF3\x0F\x11\x43\x24";
inline constexpr char kPlaneBytes[] =
    "\xF3\x0F\x59\x05\xB0\x71\x39\x03\xF3\x0F\x5F\x05\x90\x70\x39\x03\xF3\x0F\x11\x43\x24";
inline constexpr char kSetFovBytes[] =
    "\xF3\x0F\x59\x05\xB7\xA6\x74\x01\xF3\x0F\x5F\x05\x97\xA5\x74\x01\xF3\x0F\x11\x43\x24";
inline constexpr Site kMain{"MAIN", 0x425A44, kMainBytes, sizeof(kMainBytes)-1};
inline constexpr Site kTails[] = {
    {"constructor", 0x213911, kConstructorBytes, sizeof(kConstructorBytes)-1},
    {"plane override", 0x422C90, kPlaneBytes, sizeof(kPlaneBytes)-1},
    {"SetFov", 0x206F789, kSetFovBytes, sizeof(kSetFovBytes)-1},
};
inline constexpr uintptr_t kScaleRva = 0x37B9E48;
inline constexpr uintptr_t kFloorRva = 0x37B9D30;
inline constexpr float kScale = 1.6984007f;

inline bool Matches(const uint8_t* image, size_t size, const Site& site) {
    return image && site.rva <= size && site.size <= size-site.rva &&
           std::memcmp(image+site.rva, site.bytes, site.size)==0;
}
inline bool ConstantsMatch(const uint8_t* image, size_t size) {
    if (!image || size<kScaleRva+sizeof(float) || size<kFloorRva+sizeof(float)) return false;
    float scale{}, floor{};
    std::memcpy(&scale,image+kScaleRva,sizeof(scale));
    std::memcpy(&floor,image+kFloorRva,sizeof(floor));
    return scale==kScale && floor==1.0f;
}

// Only the constant multiply is replaced. In particular, the stereo TEST/MOV
// before it and the MAXSS/store/conditional branch after it remain in place.
// Save flags as well as RAX: the constructor's following JE consumes that TEST.
inline size_t EmitThreshold(uint8_t* code, uintptr_t codeAddress, uintptr_t enable,
                            uintptr_t scale, uintptr_t continuation) {
    auto reachable=[&](size_t end) {
        const auto distance=int64_t(continuation)-int64_t(codeAddress+end);
        return distance>=std::numeric_limits<int32_t>::min() && distance<=std::numeric_limits<int32_t>::max();
    };
    if (!code || !reachable(33) || !reachable(54)) return 0;
    size_t n=0;
    auto byte=[&](uint8_t v){code[n++]=v;};
    auto imm64=[&](uintptr_t v){std::memcpy(code+n,&v,8);n+=8;};
    auto jump=[&]() {
        byte(0xE9);
        const auto rel=int32_t(int64_t(continuation)-int64_t(codeAddress+n+4));
        std::memcpy(code+n,&rel,4);n+=4;
    };
    byte(0x9C); byte(0x50);                            // pushfq; push rax
    byte(0x48); byte(0xB8); imm64(enable);              // mov rax, enable
    byte(0x83); byte(0x38); byte(0);                   // cmp dword [rax], 0
    byte(0x74); byte(0x10);                           // je native multiply
    byte(0xB8); byte(0); byte(0); byte(0x80); byte(0x3F);// mov eax, 1.0f
    byte(0x66); byte(0x0F); byte(0x6E); byte(0xC0);     // movd xmm0, eax
    byte(0x58); byte(0x9D); jump();                    // restore and continue
    byte(0x48); byte(0xB8); imm64(scale);
    byte(0xF3); byte(0x0F); byte(0x59); byte(0);        // mulss xmm0, [rax]
    byte(0x58); byte(0x9D); jump();
    return n;
}

} // namespace cvr::lod_patch
