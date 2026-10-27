#include "Render/NativeUploadReservation.hpp"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <stdexcept>

using namespace cvr::stereo::upload_reservation;
extern "C" {
void CvrNativeInstanceReservationThunk();void CvrNativeInstanceReservationAfterCallback();
void InstanceWitnessBaseline();void InstanceWitnessFixtureReturn();void InstanceWitnessProbe(void*,uint32_t);
uint64_t InstanceWitnessReturnVisited{},InstanceWitnessRoute{},InstanceWitnessFinalResult{};
uint64_t CvrNativeInstanceReservationXstateMask{};
uint32_t CvrNativeInstanceReservationXstateBytes{512},CvrNativeInstanceReservationXstateMode{},CvrNativeInstanceReservationXstateReady{1};
uint32_t CyberpunkVR_NativeInstanceObserverEnable{1};
alignas(16) ThunkFrame InstanceWitnessObserved{};
}
namespace {
ThunkFrame callbackFrame{};uint32_t calls{};bool unwound{},callbackAbi{},originalCallReturn{};
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
LONG ReportException(EXCEPTION_POINTERS* exception){
    std::fprintf(stderr,"Witness fixture exception %08lX RIP=%llX RSP=%llX RBP=%llX\n",exception->ExceptionRecord->ExceptionCode,
        exception->ContextRecord->Rip,exception->ContextRecord->Rsp,exception->ContextRecord->Rbp);
    return EXCEPTION_EXECUTE_HANDLER;
}
bool Probe(void* function,uint32_t flags){
    __try{InstanceWitnessProbe(function,flags);return true;}
    __except(ReportException(GetExceptionInformation())){return false;}
}
bool ConfigureXstate(){
    int cpu[4]{};__cpuid(cpu,0);const auto maximum=cpu[0];__cpuid(cpu,1);
    if(!(uint32_t(cpu[2])&(1u<<27)))return true;
    if(!(uint32_t(cpu[2])&(1u<<26)) || maximum<0xD)return false;
    const auto enabled=_xgetbv(0);__cpuidex(cpu,0xD,0);
    const auto supported=uint64_t(uint32_t(cpu[0]))|(uint64_t(uint32_t(cpu[3]))<<32);
    const auto bytes=uint32_t(cpu[1]);
    if(!ValidXstate(enabled,supported,bytes))return false;
    CvrNativeInstanceReservationXstateMask=enabled;CvrNativeInstanceReservationXstateBytes=bytes;CvrNativeInstanceReservationXstateMode=1;return true;
}
bool SameState(const ThunkFrame& a,const ThunkFrame& b,size_t offsetA,size_t offsetB){
    const auto* x=a.xstate.data()+offsetA;const auto* y=b.xstate.data()+offsetB;
    // Ignore reserved legacy bytes and compare each enabled extended component.
    if(std::memcmp(x,y,5) || std::memcmp(x+6,y+6,26) || std::memcmp(x+160,y+160,16*16))return false;
    for(size_t i=0;i<8;++i)if(std::memcmp(x+32+i*16,y+32+i*16,10))return false;
    if(!CvrNativeInstanceReservationXstateMode)return true;
    if(std::memcmp(x+512,y+512,16))return false;
    for(uint32_t component=2;component<64;++component)if(CvrNativeInstanceReservationXstateMask&(uint64_t(1)<<component)){
        int cpu[4]{};__cpuidex(cpu,0xD,int(component));const auto bytes=uint32_t(cpu[0]),offset=uint32_t(cpu[1]);
        if(!bytes || offset>MaxXstateBytes || bytes>MaxXstateBytes-offset || std::memcmp(x+offset,y+offset,bytes))return false;
    }
    return true;
}
}
extern "C" void InstanceWitnessCheck(const ThunkFrame* frame,uint64_t flags)noexcept {
    callbackAbi=!(flags&0x400);
    ++calls;if(!frame)return;callbackFrame=*frame;
    originalCallReturn=*reinterpret_cast<const uintptr_t*>(frame->nativeStack-8)==reinterpret_cast<uintptr_t>(&InstanceWitnessFixtureReturn);
    CONTEXT context{};context.ContextFlags=CONTEXT_FULL;
    context.Rip=reinterpret_cast<uintptr_t>(&CvrNativeInstanceReservationAfterCallback);
    context.Rsp=frame->nativeStack-0x4138;context.Rbp=context.Rsp;
    DWORD64 image{};const auto* function=RtlLookupFunctionEntry(context.Rip,&image,nullptr);
    if(!function)return;
    PVOID handler{};DWORD64 establisher{};
    RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,const_cast<PRUNTIME_FUNCTION>(function),&context,&handler,&establisher,nullptr);
    unwound=context.Rsp==frame->nativeStack && context.Rbp==frame->rbp &&
        context.Rip==reinterpret_cast<uintptr_t>(&InstanceWitnessFixtureReturn);
}
int main()try{
    Require(ConfigureXstate(),"CPU enabled XSAVE state must fit the fixed bound");
    const auto configuredMask=CvrNativeInstanceReservationXstateMask;
    const auto configuredMode=CvrNativeInstanceReservationXstateMode;
    for(uint32_t legacy=0;legacy<2;++legacy){
    CvrNativeInstanceReservationXstateMode=legacy?0:configuredMode;
    CvrNativeInstanceReservationXstateMask=legacy?0:configuredMask;
    for(uint32_t failed=0;failed<4;++failed){
        const bool wrapped=(failed&2)!=0;
        const uint64_t old=wrapped?131050:135168,begin=wrapped?131072:135168,end=wrapped?131200:135296,first=wrapped?0:4096;
        InstanceWitnessObserved={};Require(Probe(reinterpret_cast<void*>(&InstanceWitnessBaseline),failed),"Baseline exception");const auto baseline=InstanceWitnessObserved;
        const auto offset=size_t(baseline.xstateAddress-reinterpret_cast<uintptr_t>(InstanceWitnessObserved.xstate.data()));
        Require(offset<64,"state area lies inside fixed alignment slack");
        calls=0;unwound=callbackAbi=originalCallReturn=false;callbackFrame={};InstanceWitnessObserved={};
        InstanceWitnessReturnVisited=InstanceWitnessRoute=0;
        Require(Probe(reinterpret_cast<void*>(&CvrNativeInstanceReservationThunk),failed),"Witness thunk exception");
        Require(!std::memcmp(&baseline,&InstanceWitnessObserved,offsetof(ThunkFrame,nativeStack)),"all GPRs and exact overwritten-instruction RFLAGS");
        Require(!(InstanceWitnessObserved.xstateAddress&63) && SameState(baseline,InstanceWitnessObserved,offset,offset),
            "all enabled vector state, including upper YMM, x87, and MXCSR preserved");
        Require(calls==1,"success-edge callback must not re-test arbitrary native flags");
        Require(InstanceWitnessFinalResult==first,"native continuation must return the physical first instance in EAX");
        Require(InstanceWitnessReturnVisited==1 && InstanceWitnessRoute==(failed?2u:1u),
            "the real CALL continuation retains the original native flags");
        {
            Require(unwound && callbackAbi && originalCallReturn,"real CALL, unchanged shadow-stack-compatible return, canonical unwind, and callback DF ABI");
            Require(callbackFrame.r9==old && callbackFrame.rax==callbackFrame.r9 && callbackFrame.r11==begin &&
                callbackFrame.rcx==end && callbackFrame.r8==first && callbackFrame.rbx==128 && callbackFrame.r10 &&
                callbackFrame.capacity==131072 && callbackFrame.nativeCaller==0xABCDEF0011223344ull &&
                (callbackFrame.nativeStack&15)==8 && (callbackFrame.flags&1) && (callbackFrame.flags&0x400),
                "exact successful CAS range, physical first, pre-restore count and leaf stack alignment");
        }
    }
    }
    for(uint32_t passive=0;passive<2;++passive){
        CyberpunkVR_NativeInstanceObserverEnable=passive?1:0;
        CvrNativeInstanceReservationXstateReady=passive?0:1;
        InstanceWitnessObserved={};Require(Probe(reinterpret_cast<void*>(&InstanceWitnessBaseline),2),"Passive baseline exception");
        const auto baseline=InstanceWitnessObserved;
        const auto offset=size_t(baseline.xstateAddress-reinterpret_cast<uintptr_t>(InstanceWitnessObserved.xstate.data()));
        calls=0;InstanceWitnessObserved={};Require(Probe(reinterpret_cast<void*>(&CvrNativeInstanceReservationThunk),2),"Passive thunk exception");
        Require(!calls && !std::memcmp(&baseline,&InstanceWitnessObserved,offsetof(ThunkFrame,nativeStack)) &&
            SameState(baseline,InstanceWitnessObserved,offset,offset),"Disabled/unprepared observer must preserve the displaced native instruction without a callback");
    }
    CyberpunkVR_NativeInstanceObserverEnable=CvrNativeInstanceReservationXstateReady=1;
    std::puts("PASS instance success-edge witness, restored caller RBX, unchanged CALL return, all GPR/RFLAGS/enabled XSTATE, leaf Win64 alignment and unwind");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
