#pragma once
#include <cstddef>
#include <cstdint>

namespace cvr::reflex {
inline constexpr int DefaultMode=0;
// Streamline ReflexOptions v1, as supplied by CP2077 2.31. Keep the cap,
// marker policy and extension chain intact when selecting a latency mode.
struct Options {
    void* next{};
    uint64_t type[2]{0x49026d0bf03af81aULL,0x3454215e96c451a6ULL};
    uint32_t version{1},padding{};
    int32_t mode{};
    uint32_t frameLimitUs{};
    uint8_t useMarkersToOptimize{},padding2{};
    uint16_t virtualKey{};
    uint32_t idThread{};
};
static_assert(sizeof(Options)==48 && offsetof(Options,mode)==32 &&
              offsetof(Options,frameLimitUs)==36 && offsetof(Options,idThread)==44);
inline bool KnownOptions(const Options& value) {
    return value.version==1 && value.type[0]==0x49026d0bf03af81aULL &&
           value.type[1]==0x3454215e96c451a6ULL;
}
inline int NormalizeMode(int mode) {return mode>=-1 && mode<=2?mode:-1;}
inline bool OverrideOptions(const Options& source,int mode,bool active,Options& result) {
    if(!active || mode<0 || mode>2 || !KnownOptions(source) || source.mode==mode)return false;
    result=source;
    result.mode=mode;
    return true;
}
}
