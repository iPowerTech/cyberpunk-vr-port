#pragma once
#include <array>
#include <cstdint>
#include <span>
#if defined(_WIN32) && defined(_M_X64)
#include <windows.h>
#endif

namespace cvr::stereo::scene_update::scope_liveness {
inline constexpr uint32_t MaxScopes=16,MaxFrames=128;
enum class Life:uint8_t {Unknown,Live,Absent};
struct Anchor {
    uint64_t cookie{},stack{},function{},low{},high{};
};
using Results=std::array<Life,MaxScopes>;

#if defined(_WIN32) && defined(_M_X64)
inline Anchor Capture(uint64_t cookie,uint64_t stack,uint64_t function)noexcept{
    ULONG_PTR low{},high{};GetCurrentThreadStackLimits(&low,&high);return {cookie,stack,function,low,high};
}
// A current-thread OS unwind walk proves an actual thunk frame, its native
// entry stack and instance cookie. Old stack bytes alone never prove liveness.
// Different stacks/fibers, broken unwind data and budget exhaustion are Unknown.
__declspec(noinline) inline Results Inspect(std::span<const Anchor> anchors,uint64_t excludeEntry=0,uint32_t maxFrames=MaxFrames)noexcept{
    Results result{};
    if(anchors.size()>MaxScopes || maxFrames>MaxFrames)return result;
    __try{
        ULONG_PTR low{},high{};GetCurrentThreadStackLimits(&low,&high);
        std::array<bool,MaxScopes> eligible{};
        for(size_t i=0;i<anchors.size();++i){const auto& anchor=anchors[i];
            eligible[i]=anchor.cookie && anchor.function && anchor.low==low && anchor.high==high &&
                anchor.stack>=low+16 && anchor.stack<=high-8 && !(anchor.stack&7);
            // A new Before call at exactly the old entry stack is a different
            // instance; its cookie slot has not been initialized yet.
            if(eligible[i] && excludeEntry && anchor.stack==excludeEntry)result[i]=Life::Absent;
        }
        CONTEXT context{};UNWIND_HISTORY_TABLE history{};RtlCaptureContext(&context);
        for(uint32_t frame=0;frame<maxFrames;++frame){
            if(!context.Rip || context.Rsp<low || context.Rsp>high-8 || (context.Rsp&7))break;
            const auto previous=context.Rsp;DWORD64 base{};
            auto* function=RtlLookupFunctionEntry(context.Rip,&base,&history);
            const uint64_t begin=function?base+function->BeginAddress:0;
            if(function){void* handler{};DWORD64 establisher{};
                RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,context.Rip,function,&context,&handler,&establisher,nullptr);
            }else{
                SIZE_T copied{};uint64_t caller{};
                if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(uintptr_t(context.Rsp)),&caller,8,&copied) || copied!=8)break;
                context.Rip=caller;context.Rsp+=8;
            }
            if(context.Rsp<=previous || context.Rsp>high)break;
            for(size_t i=0;i<anchors.size();++i){const auto& anchor=anchors[i];
                if(!eligible[i] || result[i]!=Life::Unknown)continue;
                if(begin==anchor.function && context.Rsp==anchor.stack+8){
                    SIZE_T copied{};uint64_t cookie{};
                    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(uintptr_t(anchor.stack-16)),&cookie,8,&copied) || copied!=8)continue;
                    result[i]=cookie==anchor.cookie?Life::Live:Life::Absent;
                }else if(context.Rsp>=anchor.stack+8)result[i]=Life::Absent;
            }
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){
        // A failed walk cannot authorize a write attribution or cancellation.
        result={};
    }
    return result;
}
#else
inline Anchor Capture(uint64_t cookie,uint64_t stack,uint64_t function)noexcept{return {cookie,stack,function,0,0};}
inline Results Inspect(std::span<const Anchor>,uint64_t=0,uint32_t=MaxFrames)noexcept{return {};}
#endif
}
