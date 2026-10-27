#include "Render/NativeUploadReservation.hpp"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <stdexcept>

using namespace cvr::stereo::upload_reservation;
extern "C" {
void CvrNativeUploadReservationThunk();void CvrNativeUploadReservationAfterCallback();
void UploadReservationBaseline();void UploadReservationFixtureReturn();void UploadReservationProbe(void*,uint32_t);
uint64_t UploadReservationReturnVisited{},UploadReservationRoute{};
uint64_t CvrNativeUploadReservationXstateMask{};
uint32_t CvrNativeUploadReservationXstateBytes{512},CvrNativeUploadReservationXstateMode{};
alignas(16) ThunkFrame UploadReservationObserved{};
}
namespace {
ThunkFrame callbackFrame{};uint32_t calls{};bool unwound{},callbackAbi{},originalCallReturn{};
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool ConfigureXstate(){
    int cpu[4]{};__cpuid(cpu,0);const auto maximum=cpu[0];__cpuid(cpu,1);
    if(!(uint32_t(cpu[2])&(1u<<27)))return true;
    if(!(uint32_t(cpu[2])&(1u<<26)) || maximum<0xD)return false;
    const auto enabled=_xgetbv(0);__cpuidex(cpu,0xD,0);
    const auto supported=uint64_t(uint32_t(cpu[0]))|(uint64_t(uint32_t(cpu[3]))<<32);
    const auto bytes=uint32_t(cpu[1]);
    if(!ValidXstate(enabled,supported,bytes))return false;
    CvrNativeUploadReservationXstateMask=enabled;CvrNativeUploadReservationXstateBytes=bytes;CvrNativeUploadReservationXstateMode=1;return true;
}
bool SameState(const ThunkFrame& a,const ThunkFrame& b,size_t offsetA,size_t offsetB){
    const auto* x=a.xstate.data()+offsetA;const auto* y=b.xstate.data()+offsetB;
    // Ignore reserved legacy bytes and compare each enabled extended component.
    if(std::memcmp(x,y,5) || std::memcmp(x+6,y+6,26) || std::memcmp(x+160,y+160,16*16))return false;
    for(size_t i=0;i<8;++i)if(std::memcmp(x+32+i*16,y+32+i*16,10))return false;
    if(!CvrNativeUploadReservationXstateMode)return true;
    if(std::memcmp(x+512,y+512,16))return false;
    for(uint32_t component=2;component<64;++component)if(CvrNativeUploadReservationXstateMask&(uint64_t(1)<<component)){
        int cpu[4]{};__cpuidex(cpu,0xD,int(component));const auto bytes=uint32_t(cpu[0]),offset=uint32_t(cpu[1]);
        if(!bytes || offset>MaxXstateBytes || bytes>MaxXstateBytes-offset || std::memcmp(x+offset,y+offset,bytes))return false;
    }
    return true;
}
}
extern "C" void UploadReservationCheck(const ThunkFrame* frame,uint64_t flags)noexcept {
    callbackAbi=!(flags&0x400);
    ++calls;if(!frame)return;callbackFrame=*frame;
    originalCallReturn=*reinterpret_cast<const uintptr_t*>(frame->nativeStack-8)==reinterpret_cast<uintptr_t>(&UploadReservationFixtureReturn);
    CONTEXT context{};context.ContextFlags=CONTEXT_FULL;
    context.Rip=reinterpret_cast<uintptr_t>(&CvrNativeUploadReservationAfterCallback);
    context.Rsp=frame->nativeStack-0x4130;context.Rbp=context.Rsp;
    DWORD64 image{};const auto* function=RtlLookupFunctionEntry(context.Rip,&image,nullptr);
    if(!function)return;
    PVOID handler{};DWORD64 establisher{};
    RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,const_cast<PRUNTIME_FUNCTION>(function),&context,&handler,&establisher,nullptr);
    unwound=context.Rsp==frame->nativeStack && context.Rbp==frame->rbp &&
        context.Rip==reinterpret_cast<uintptr_t>(&UploadReservationFixtureReturn);
}
int main()try{
    Require(ConfigureXstate(),"CPU enabled XSAVE state must fit the fixed bound");
    for(uint32_t failed=0;failed<2;++failed){
        UploadReservationObserved={};UploadReservationProbe(reinterpret_cast<void*>(&UploadReservationBaseline),failed);const auto baseline=UploadReservationObserved;
        const auto offset=size_t(baseline.xstateAddress-reinterpret_cast<uintptr_t>(UploadReservationObserved.xstate.data()));
        Require(offset<64,"state area lies inside fixed alignment slack");
        calls=0;unwound=callbackAbi=originalCallReturn=false;callbackFrame={};UploadReservationObserved={};
        UploadReservationReturnVisited=UploadReservationRoute=0;
        UploadReservationProbe(reinterpret_cast<void*>(&CvrNativeUploadReservationThunk),failed);
        Require(!std::memcmp(&baseline,&UploadReservationObserved,offsetof(ThunkFrame,nativeStack)),"all GPRs and exact overwritten-instruction RFLAGS");
        Require(!(UploadReservationObserved.xstateAddress&63) && SameState(baseline,UploadReservationObserved,offset,offset),
            "all enabled vector state, including upper YMM, x87, and MXCSR preserved");
        Require(calls==(failed?0u:1u),"callback only runs for successful CAS ZF");
        Require(UploadReservationReturnVisited==1 && UploadReservationRoute==(failed?2u:1u),
            "both paths return to the real CALL target before the relocated JNE selects retry or success");
        if(!failed){
            Require(unwound && callbackAbi && originalCallReturn,"real CALL, unchanged shadow-stack-compatible return, canonical unwind, and callback DF ABI");
            Require(callbackFrame.r9==4000 && callbackFrame.rcx==4096 && callbackFrame.rdx==4608 && callbackFrame.r13==512 &&
                callbackFrame.rbx && callbackFrame.rbp==0x11111111 && callbackFrame.capacity==4096 && callbackFrame.r8==callbackFrame.capacity &&
                callbackFrame.nativeCaller==0xABCDEF0011223344ull &&
                (callbackFrame.flags&0x40) && (callbackFrame.flags&1) && (callbackFrame.flags&0x400),"exact pre-emulation successful-CAS witness");
        }
    }
    std::puts("PASS successful-CAS-only thunk, relocated JNE retry, unchanged real CALL return, GPR/RFLAGS/full enabled XSTATE preservation, and Windows unwind");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
