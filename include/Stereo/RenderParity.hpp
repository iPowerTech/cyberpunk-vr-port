#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace cvr::stereo {
bool RenderGraphCacheObservationInstalled();
// Recovered from dabinn TE6 highlight (see docs/te6-testing2-render-review-20260926.md).
inline uint64_t VrcamGraphHash(uint64_t hash,uint64_t viewName) {
    uint64_t value=0x9EDCB08A284EFDDFull;
    for(int i=0;i<8;++i){value^=uint8_t(hash);value*=0x100000001B3ull;hash>>=8;}
    for(int i=0;i<8;++i){value^=uint8_t(viewName);value*=0x100000001B3ull;viewName>>=8;}
    return value;
}
inline uint64_t PreserveViewBit33(uint64_t desired,uint64_t native) {
    constexpr uint64_t bit=1ull<<33;
    return (desired&~bit)|(native&bit);
}
inline bool IsMainAaContext(uintptr_t context,uint64_t name,uintptr_t mainContext) {
    return context && mainContext && context==mainContext && name==0;
}
struct RenderOwner {
    uintptr_t renderer{},player{},mainCamera{},eyeCamera{};
    uint64_t origin{},eyeName{};
    uint32_t width{},height{};
    bool operator==(const RenderOwner&) const = default;
    explicit operator bool() const {
        return renderer && player && mainCamera && eyeCamera && origin && eyeName && width && height;
    }
};
struct AaSample {
    RenderOwner owner{};
    uint32_t frame{},mode{};
    bool valid{};
    bool Read(const RenderOwner& now,uint32_t next,uint32_t& out) const {
        // VRCAM can build before MAIN. AA is a setting, not a GPU handle, so
        // the immediately preceding native frame is valid for this observation.
        if(!valid || !now || owner!=now || uint32_t(next-frame)>1)return false;
        out=mode;return true;
    }
};
using SkyRadiance = std::array<float,4>;
inline bool ValidSkyRadiance(const SkyRadiance& value) {
    for(float channel:value)if(!std::isfinite(channel) || channel<0)return false;
    for(int i=0;i<3;++i)if(!std::isfinite(value[i]*value[3]))return false;
    return true;
}
struct SkyRadianceSample {
    RenderOwner owner{};
    SkyRadiance color{};
    uint32_t frame{};
    bool valid{};
    bool Read(const RenderOwner& now,uint32_t next,SkyRadiance& out) const {
        if(!valid || !now || owner!=now || uint32_t(next-frame)>1 || !ValidSkyRadiance(color))return false;
        out=color;return true;
    }
};
struct FogAllocation {
    uintptr_t pool{},resource{},object{},srv{};
    bool operator==(const FogAllocation&) const = default;
    explicit operator bool() const {return pool && resource && object && srv;}
};
struct FogSample {
    RenderOwner owner{};
    FogAllocation allocation{};
    uintptr_t registry{};
    uint64_t pose{},stampUs{};
    uint32_t frame{},handle{};
    bool Eligible(const RenderOwner& now,uintptr_t reg,uint32_t next,uint64_t nextPose,uint64_t timeUs) const {
        // Live 2.31 trace: VRCAM runs first; three persistent input buffers rotate
        // between views. This is HISTORY, so the preceding MAIN frame is the
        // intended source. A new pose is normal then; same-frame pairs must agree.
        const uint32_t age=next-frame;
        return now && owner==now && registry==reg && reg && age<=1 && pose && nextPose && (age || pose==nextPose) &&
               handle && handle<=0x7fffffff && allocation && stampUs && timeUs>=stampUs && timeUs-stampUs<=100000;
    }
};
// POD-only bridge calls used from engine detours containing SEH guards.
void ClearPreparedGraph();
void ObservePreparedGraph(uintptr_t manager);
using FlagComputeFn=int64_t(__fastcall*)(void*,int64_t,int64_t,int64_t);
int64_t ComputeFlagsWithMainAa(FlagComputeFn original,void* a1,int64_t a2,int64_t context,int64_t view,bool* ready);
uint64_t MergeViewFlags(uint64_t desired,uint64_t native,bool ready);
void SynchronizeFogHistory(uintptr_t registry,uint32_t* handle,uint32_t logicalId,uintptr_t caller);
uintptr_t SetFogWorkContext(uintptr_t context);
}
