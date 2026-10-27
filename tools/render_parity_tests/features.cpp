#include "Stereo/FrameGraphFeatures.hpp"
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <thread>

void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main()try {
    using namespace cvr::stereo;
    MainViewFeatureCache cache;
    constexpr uintptr_t mainContext=0x24990f00e10,otherContext=mainContext+0x1000;
    constexpr FrameGraphFlags rtOn{0x3C9C3D7DAF057F53,0x0517F028},rtOff{0x3C00017FAF65FF53,0x0517F028};
    FrameGraphFlags observed{1,2};
    Check(!cache.Read(mainContext,observed) && observed==FrameGraphFlags{1,2},"empty cache changed fallback");
    Check(cache.Observe(mainContext,mainContext,0,rtOn),"MAIN RT-on sample rejected");
    Check(cache.Read(mainContext,observed) && observed==rtOn,"MAIN sample missing");
    Check(cache.Observe(mainContext,mainContext,0,rtOff) && cache.Read(mainContext,observed) && observed==rtOff,
        "RT-off flags with fewer bits did not replace RT-on template");
    Check(!cache.Observe(otherContext,mainContext,0,{~0ull,~0ull}),"unnamed reflection/UI view poisoned MAIN template");
    Check(!cache.Observe(mainContext,mainContext,0x77AD6D6871650500,rtOn),"VRCAM fed its own forced flags back into MAIN");
    Check(!cache.Observe(mainContext,mainContext,0,{}) && cache.Read(mainContext,observed) && observed==rtOff,
        "cleared context flags destroyed a valid producer sample");
    Check(!cache.Read(otherContext,observed) && !cache.Read(0,observed),"replaced/unbound MAIN reused an old owner's flags");
    const FrameGraphFlags noUpscaler{rtOff.f0,rtOff.f1&~0x3A0ull};
    Check(cache.Observe(mainContext,mainContext,0,noUpscaler) && cache.Read(mainContext,observed) && observed==noUpscaler,
        "disabled upscaler bits were retained");
    Check(cache.Observe(mainContext,mainContext,0,rtOn) && cache.Read(mainContext,observed) && observed==rtOn,"RT re-enable failed");
    constexpr FrameGraphFlags pairA{1,0xAAAAAAAA},pairB{2,0xBBBBBBBB};
    cache.Observe(mainContext,mainContext,0,pairA);
    std::atomic<bool> started=false,done=false,torn=false;
    std::thread writer([&]{while(!started.load()){}for(unsigned i=0;i<100000;++i)cache.Observe(mainContext,mainContext,0,i%2?pairA:pairB);done=true;});
    started=true;
    do {
        FrameGraphFlags pair;
        if(!cache.Read(mainContext,pair) || (pair!=pairA && pair!=pairB))torn=true;
    }while(!done.load());
    writer.join();Check(!torn.load(),"feature words came from different publications");
    constexpr uint64_t amdMain=0x0507F008,forcedRtt=0x0517F009;
    const auto safe=SupportedShadingRateFlags(forcedRtt,0);
    Check(safe==(amdMain|1ull),"recorded AMD graph still requests an unsupported shading-rate image");
    Check((safe^forcedRtt)==ShadingRateImageFlagF1,"unrelated graph feature changed");
    Check(SupportedShadingRateFlags(amdMain,0)==amdMain,"already supported AMD flags changed");
    Check(SupportedShadingRateFlags(safe|forcedRtt,0)==safe,"builder OR reinstated unsupported VRS");
    // Model the native guarded allocator from the supplied dump. The failing
    // graph takes ceil(width/tile); the corrected graph never enters it.
    auto allocate=[](uint64_t flags,unsigned width,unsigned tile){
        if(!(flags&ShadingRateImageFlagF1))return 0u;
        if(!tile)throw std::runtime_error("native divide by zero would execute");
        return (width+tile-1)/tile;
    };
    Check(allocate(safe,1832,0)==0,"unsupported allocation reached the native divider");
    for(unsigned tile:{1u,8u,16u,32u}) {
        Check(SupportedShadingRateFlags(forcedRtt,tile)==forcedRtt,"supported device lost VRS");
        Check(allocate(SupportedShadingRateFlags(forcedRtt,tile),1832,tile)==(1832+tile-1)/tile,"supported allocation dimensions changed");
    }
    for(unsigned bit=0;bit<64;++bit) {
        const uint64_t flags=1ull<<bit;
        Check(SupportedShadingRateFlags(flags,0)==(bit==20?0:flags),"another feature was masked");
        Check(SupportedShadingRateFlags(flags,16)==flags,"NVIDIA-capable mask changed");
    }
    std::cout<<"PASS RT on/off/re-enable, producer identity, coherent flag pair, recorded AMD divide-by-zero graph and supported VRS\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
