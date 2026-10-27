#include "Render/SinglePassTrace.hpp"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <mutex>

extern "C" {
__declspec(dllexport) uint32_t CyberpunkVR_SinglePassTraceRecordBytes=sizeof(cvr::stereo::trace::Record);
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_SinglePassTraceRequest{0};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_SinglePassTraceState{0};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_SinglePassTraceSeq{0};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_SinglePassTraceCount{0};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_SinglePassTraceNodeFilter{};
__declspec(dllexport) cvr::stereo::trace::Record CyberpunkVR_SinglePassTraceRecords[cvr::stereo::trace::Capacity]{};
}
namespace cvr::stereo::trace {
namespace {
std::mutex mutex;
uint32_t frame{},limit{};
std::atomic<uint64_t> epoch{0};
bool Read(uint64_t address,void* output,size_t size) {
    SIZE_T bytes{};
    return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,size,&bytes) && bytes==size;
}
// The current EXE's camera builder keeps context/camera/width/height in these
// nonvolatile registers. Unwind only our current thread, without suspending it.
// Inputs are copied while its stack frame is live; no pointer is retained for use.
void NativeInputs(Record& row) {
    __try {
        const auto game=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        CONTEXT context{};RtlCaptureContext(&context);
        ULONG_PTR low{},high{};GetCurrentThreadStackLimits(&low,&high);
        for(unsigned depth=0;depth<24 && context.Rsp>=low && context.Rsp+8<=high;++depth) {
            if(context.Rip>=game+0x1E2C94 && context.Rip<game+0x1E3858) {
                row.context=context.Rsi;row.cameraAddress=context.Rdi;
                row.width=static_cast<uint32_t>(context.R15);row.height=static_cast<uint32_t>(context.R14);
                if(!row.context || !row.cameraAddress || row.width>16384 || row.height>16384)return;
                if(!Read(row.context+0x18,&row.view,sizeof(row.view)) || !Read(row.cameraAddress,row.input.data(),row.input.size()))return;
                if(context.Rbp+0x8A8<=high && context.Rbp+0x8A0>=low) {
                    uintptr_t clip{};
                    if(!Read(context.Rbp+0x8A0,&clip,sizeof(clip)))return;
                    if(clip && !Read(clip,row.clip.data(),row.clip.size()))return;
                }
                row.inputValid=1;return;
            }
            DWORD64 image{};const auto function=RtlLookupFunctionEntry(context.Rip,&image,nullptr);
            if(function){PVOID handler{};DWORD64 establisher{};
                RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,function,&context,&handler,&establisher,nullptr);}
            else {if(!Read(context.Rsp,&context.Rip,sizeof(context.Rip)))return;context.Rsp+=8;}
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {row.inputValid=0;}
}
void Append(uint32_t kind,uint32_t node,int side,uint32_t index,const void* object,const void* data,Predict predict=nullptr) {
    std::lock_guard lock(mutex);
    if(CyberpunkVR_SinglePassTraceState.load(std::memory_order_relaxed)!=1)return;
    const auto count=CyberpunkVR_SinglePassTraceCount.load(std::memory_order_relaxed);
    if(count==Capacity){CyberpunkVR_SinglePassTraceState.store(3,std::memory_order_release);return;}
    CyberpunkVR_SinglePassTraceSeq.fetch_add(1,std::memory_order_acq_rel);
    auto& row=CyberpunkVR_SinglePassTraceRecords[count];row={};
    LARGE_INTEGER now{};QueryPerformanceCounter(&now);
    row.sequence=count+1;row.qpc=now.QuadPart;row.object=reinterpret_cast<uintptr_t>(object);
    row.frame=frame;row.node=node;row.kind=kind;row.index=index;row.side=side;row.thread=GetCurrentThreadId();
    if(data){std::memcpy(row.camera.data(),data,row.camera.size());NativeInputs(row);if(predict && row.inputValid)predict(row);}
    CyberpunkVR_SinglePassTraceCount.store(count+1,std::memory_order_relaxed);
    CyberpunkVR_SinglePassTraceSeq.fetch_add(1,std::memory_order_release);
}
}
void FrameBoundary() {
    if(CyberpunkVR_SinglePassTraceState.load(std::memory_order_relaxed)!=1 &&
       !CyberpunkVR_SinglePassTraceRequest.load(std::memory_order_relaxed))return;
    std::lock_guard lock(mutex);
    if(CyberpunkVR_SinglePassTraceState.load(std::memory_order_relaxed)==1) {
        ++frame;epoch.fetch_add(1,std::memory_order_release);
        if(frame>limit)CyberpunkVR_SinglePassTraceState.store(2,std::memory_order_release);
        return;
    }
    const auto request=CyberpunkVR_SinglePassTraceRequest.exchange(0,std::memory_order_acq_rel);
    if(!request)return;
    CyberpunkVR_SinglePassTraceSeq.fetch_add(1,std::memory_order_acq_rel);
    CyberpunkVR_SinglePassTraceCount.store(0,std::memory_order_relaxed);
    frame=1;limit=std::min(request,CyberpunkVR_SinglePassTraceNodeFilter.load()==0x23A938?8u:4u);epoch.fetch_add(1,std::memory_order_release);
    CyberpunkVR_SinglePassTraceState.store(1,std::memory_order_release);
    CyberpunkVR_SinglePassTraceSeq.fetch_add(1,std::memory_order_release);
}
void Camera(uint32_t node,int side,uint32_t index,const void* bytes,Predict predict) {
    if(CyberpunkVR_SinglePassTraceState.load(std::memory_order_relaxed)!=1 || !bytes || side<0 || side>1)return;
    const auto filter=CyberpunkVR_SinglePassTraceNodeFilter.load(std::memory_order_relaxed);if(filter && node!=filter)return;
    Append(1,node,side,index,nullptr,bytes,predict);
}
void CaptureNativeInputs(Record& row){NativeInputs(row);}
void List(uint32_t node,int side,const void* list) {
    if(CyberpunkVR_SinglePassTraceState.load(std::memory_order_relaxed)!=1 || !list || side<0 || side>1)return;
    const auto filter=CyberpunkVR_SinglePassTraceNodeFilter.load(std::memory_order_relaxed);if(filter && node!=filter)return;
    struct Last {uint64_t epoch{};const void* list{};uint32_t node{};int side=-1;};
    thread_local Last previous;
    const auto current=epoch.load(std::memory_order_acquire);
    if(previous.epoch==current && previous.list==list && previous.node==node && previous.side==side)return;
    previous={current,list,node,side};Append(2,node,side,0,list,nullptr);
}
}
