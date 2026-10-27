#include "Render/NativeSourceSrvBinding.hpp"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <stdexcept>

using namespace cvr::stereo::source_srv;
extern "C" {
void SourceSrvNative();void SourceSrvProbe(void*,uint32_t);void SourceSrvProbeReturn();
void CvrSourceSrvIndividualAfterCallback();void CvrSourceSrvCachedAfterCallback();
void* CvrSourceSrvIndividualOriginal=reinterpret_cast<void*>(&SourceSrvNative);
void* CvrSourceSrvCachedOriginal=reinterpret_cast<void*>(&SourceSrvNative);
std::atomic<uint32_t> CyberpunkVR_SourceSrvBindingEnable{0};
uint64_t CvrSourceSrvXstateMask{};
uint32_t CvrSourceSrvXstateBytes{512},CvrSourceSrvXstateMode{},CvrSourceSrvXstateReady{1};
alignas(16) ThunkFrame SourceSrvNativeInputs{};
uint64_t SourceSrvNativeStackArgs[2]{},SourceSrvReturnValue{};
}
namespace {
ThunkFrame callbackFrame;uint32_t calls{},observedRoute{};bool callbackAbi{},unwound{};
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool Configure(){
    int cpu[4]{};__cpuid(cpu,0);const auto maximum=cpu[0];__cpuid(cpu,1);
    if(!(uint32_t(cpu[2])&(1u<<27)))return true;
    if(!(uint32_t(cpu[2])&(1u<<26)) || maximum<0xD)return false;
    const auto enabled=_xgetbv(0);__cpuidex(cpu,0xD,0);
    const auto supported=uint64_t(uint32_t(cpu[0]))|(uint64_t(uint32_t(cpu[3]))<<32);
    if(!detail::ValidXstate(enabled,supported,uint32_t(cpu[1])))return false;
    CvrSourceSrvXstateMask=enabled;CvrSourceSrvXstateBytes=uint32_t(cpu[1]);CvrSourceSrvXstateMode=1;return true;
}
bool SameState(const ThunkFrame& a,const ThunkFrame& b,size_t offset){
    const auto* x=a.xstate.data()+offset;const auto* y=b.xstate.data()+offset;
    if(std::memcmp(x,y,5) || std::memcmp(x+6,y+6,26) || std::memcmp(x+160,y+160,256))return false;
    for(size_t i=0;i<8;++i)if(std::memcmp(x+32+i*16,y+32+i*16,10))return false;
    if(!CvrSourceSrvXstateMode)return true;
    if(std::memcmp(x+512,y+512,16))return false;
    for(uint32_t component=2;component<64;++component)if(CvrSourceSrvXstateMask&(uint64_t(1)<<component)){
        int cpu[4]{};__cpuidex(cpu,0xD,int(component));const auto bytes=uint32_t(cpu[0]),at=uint32_t(cpu[1]);
        if(!bytes || at>MaxXstateBytes || bytes>MaxXstateBytes-at || std::memcmp(x+at,y+at,bytes))return false;
    }
    return true;
}
}
extern "C" void SourceSrvCheck(uint32_t route,const ThunkFrame* frame,uint64_t flags)noexcept {
    ++calls;observedRoute=route;callbackAbi=!(flags&0x400);if(!frame)return;callbackFrame=*frame;
    CONTEXT context{};context.ContextFlags=CONTEXT_FULL;
    context.Rip=reinterpret_cast<uintptr_t>(route?&CvrSourceSrvCachedAfterCallback:&CvrSourceSrvIndividualAfterCallback);
    context.Rsp=frame->stack-0x4128;context.Rbp=context.Rsp;
    DWORD64 image{};auto* function=RtlLookupFunctionEntry(context.Rip,&image,nullptr);if(!function)return;
    PVOID handler{};DWORD64 establisher{};
    RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,function,&context,&handler,&establisher,nullptr);
    unwound=context.Rsp==frame->stack+8 && context.Rbp==frame->rbp && context.Rip==reinterpret_cast<uintptr_t>(&SourceSrvProbeReturn);
}
int main()try{
    Require(Configure(),"CPU enabled state must fit fixed bound");
    const std::array targets{reinterpret_cast<void*>(&CvrSourceSrvIndividualDetour),reinterpret_cast<void*>(&CvrSourceSrvCachedDetour)};
    for(uint32_t route=0;route<2;++route)for(uint32_t mode=0;mode<(route?4u:6u);++mode){
        const auto arguments=mode>=4?mode:mode==3?route+2:route;
        SourceSrvNativeInputs={};SourceSrvProbe(reinterpret_cast<void*>(&SourceSrvNative),arguments);const auto baseline=SourceSrvNativeInputs;
        const auto stateOffset=size_t(baseline.xstateAddress-reinterpret_cast<uintptr_t>(SourceSrvNativeInputs.xstate.data()));
        Require(stateOffset<64,"64-byte state alignment slack");
        CyberpunkVR_SourceSrvBindingEnable=mode!=0;CvrSourceSrvXstateReady=mode!=1;
        calls=0;callbackAbi=unwound=false;SourceSrvNativeInputs={};SourceSrvReturnValue=0;
        SourceSrvProbe(targets[route],arguments);
        Require(!std::memcmp(&baseline,&SourceSrvNativeInputs,offsetof(ThunkFrame,xstateAddress)),
            "all input GPR/RFLAGS, original native RSP and caller preserved");
        Require(SameState(baseline,SourceSrvNativeInputs,stateOffset),"all enabled vector/x87/MXCSR state preserved");
        Require(SourceSrvNativeStackArgs[0]==0x99999999 && SourceSrvNativeStackArgs[1]==0x12345678 &&
            SourceSrvReturnValue==0xABCDEF0011223344ull,"original stack arguments/home space and native return preserved");
        const bool active=mode==2 || mode>=4;
        Require(calls==(active?1u:0u),"disabled, unavailable XSTATE and unrelated bindings dispatch without callbacks");
        if(active)Require(observedRoute==route && callbackAbi && unwound && callbackFrame.caller==baseline.caller &&
            callbackFrame.stack==baseline.stack && (callbackFrame.flags&0x400),"before-only callback has original frame, DF ABI and valid unwind");
    }
    std::puts("PASS individual/cached transparent tail-entry thunks, disabled FLAGS passthrough, full XSTATE, original RA/stack arguments/return and unwind");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
