#include "Render/NativePreparation.hpp"
#include "Render/NativeDepthPreparation.hpp"
#include <windows.h>
#include <mutex>

extern "C" {
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_EagerDepthMode{},CyberpunkVR_EagerDepthCompatible{},CyberpunkVR_EagerDepthVerified{};
__declspec(dllexport) uint32_t CyberpunkVR_EagerDepthStats[cvr::stereo::eager_depth::CounterCount]{};
}
namespace cvr::stereo::eager_depth {
namespace {
std::mutex mutex;Cache cache;
bool ReadMemory(uint64_t address,void* destination,size_t bytes){SIZE_T copied{};return address && address<=UINT64_MAX-bytes &&
    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),destination,bytes,&copied) && copied==bytes;}
bool Compatible(){
    static const bool result=[]{
        struct Region {uint32_t address,size;uint64_t hash;};
        // Existing detour prologues are excluded; their native bodies remain intact.
        const Region regions[]{{0x1D57230,0x4A,0x65D6E84FDC58533Dull},{0x15377C,0x9D,0xEA4AD0C3022BE2FFull},
            {0x1D57120,0xE0,0xD35284DC4BA124BEull},{0x379588,0x3D5,0x71E04F3A4D14F208ull},{0x379B28,0x91C,0xBF1B38E031480649ull}};
        std::array<uint8_t,4096> bytes{};const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        for(const auto& region:regions){if(!ReadMemory(base+region.address,bytes.data(),region.size))return false;
            uint64_t hash=14695981039346656037ull;for(UINT i=0;i<region.size;++i)hash=(hash^bytes[i])*1099511628211ull;
            if(hash!=region.hash)return false;}
        return true;
    }();
    CyberpunkVR_EagerDepthCompatible=result?1:2;return result;
}
}
void BeginRun(){
    std::lock_guard lock(mutex);cache.Reset();std::fill_n(CyberpunkVR_EagerDepthStats,CounterCount,0u);
    if(CyberpunkVR_EagerDepthMode.load()==1)CyberpunkVR_EagerDepthVerified=0;
}
void EndRun(){std::lock_guard lock(mutex);cache.Reset();}
bool Intercept(Prepare prepare,Gather gather,Filter filter,Finalize finalize,void* context,void* buckets,void* output,uint32_t flags0,uint32_t flags1){
    const auto mode=CyberpunkVR_EagerDepthMode.load(std::memory_order_relaxed);
    if(!mode || !preparation::RunActive() || preparation::Active() || CyberpunkVR_PreparationPairState.load()!=2 ||
        (mode==2 && !CyberpunkVR_EagerDepthVerified.load()) || !Compatible())return false;
    std::lock_guard lock(mutex);const auto frame=CyberpunkVR_PreparationRunFrames.load();
    const bool handled=cache.Intercept({ReadMemory,prepare,gather,filter,finalize},frame,CyberpunkVR_PreparationPairData[4],CyberpunkVR_PreparationPairData[3],
        CyberpunkVR_PreparationRunDepth[0].load()+1==frame,mode,context,buckets,output,flags0,flags1);
    std::copy(cache.counts.begin(),cache.counts.end(),CyberpunkVR_EagerDepthStats);
    if(mode==1){
        if(cache.counts[OutputDifferent] || cache.counts[Failure])CyberpunkVR_EagerDepthVerified=0;
        else if(cache.counts[OutputSame]>=30)CyberpunkVR_EagerDepthVerified=1;
    }
    return handled;
}
}
