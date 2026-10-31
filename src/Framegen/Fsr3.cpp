// FSR3 frame generation -- see Framegen/Fsr3.hpp. Ported from the fork's FrameGen/FrameGenFsr3.cpp
// (vr_framegen): the FidelityFX contexts, the dilate pass and the dispatch are the fork's; the inputs
// are 0.1.9's per-eye depth/motion copies and captured eye images, and the result goes straight into
// a pool the XR loop copies from.
#include "Framegen/Fsr3.hpp"
#include "Framegen/Framegen.hpp"
#include "Framegen/Fsr3Dilate.hpp"

#include <windows.h>
#include <d3d12.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/ffx_opticalflow.h>
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

extern void Log(const char* fmt, ...);
extern "C" int CyberpunkVR_XrRateLog;   // Runtimes/OpenXRFrameLoop.cpp: the frame-path reports

namespace cvr::fsr3 {
namespace {
using Microsoft::WRL::ComPtr;

std::mutex settingsMutex;
Settings settings;
std::atomic<bool> enabled{false},opticalFlow{true},faulted{false},show{true};

std::mutex statusMutex;
Status status;
void SetText(const char* text) {std::lock_guard lock(statusMutex);std::snprintf(status.text,sizeof(status.text),"%s",text);}

// The report's per-second counters: what the XR loop showed, and the GPU time of our own lists.
std::atomic<uint32_t> shownMidpoint{},shownAfter{},shownReal{},shownRepeat{},realNoFrame{},realGap{},capturesNeverShown{};
std::atomic<uint32_t> calls{};
std::mutex gpuMutex;
double gpuSumMs=0,gpuDilateMs=0,gpuInterpolateMs=0;uint32_t gpuCount=0;

// ---- FidelityFX: one backend, per eye a frame interpolation and an optical flow context ----------
void* scratch=nullptr;
FfxInterface backend{};
bool backendReady=false;
FfxFrameInterpolationContext fiContext[2]{};
FfxOpticalflowContext ofContext[2]{};
bool fiValid[2]{},ofValid[2]{};
// Grow-only: the engine's dynamic resolution moves the render size, and a context dispatched beyond
// the size it was created for writes past its own buffers (the fork's first GPU hang).
uint32_t contextRenderW=0,contextRenderH=0,contextDisplayW=0,contextDisplayH=0;
bool firstDispatch[2]{};
ComPtr<ID3D12Resource> ofVector[2],ofScd[2];
int ofVectorW[2]{},ofVectorH[2]{};

void DestroyContexts() {
    for(int eye=0;eye<2;++eye) {
        if(fiValid[eye])ffxFrameInterpolationContextDestroy(&fiContext[eye]);
        if(ofValid[eye])ffxOpticalflowContextDestroy(&ofContext[eye]);
        fiValid[eye]=ofValid[eye]=false;
    }
}

bool EnsureContexts(ID3D12Device* device,uint32_t renderW,uint32_t renderH,uint32_t displayW,uint32_t displayH,bool inverted,bool infinite) {
    if(!backendReady) {
        // 2 eyes x (frame interpolation + optical flow).
        const size_t size=ffxGetScratchMemorySizeDX12(4);
        scratch=std::malloc(size);
        if(!scratch)return false;
        if(ffxGetInterfaceDX12(&backend,ffxGetDeviceDX12(device),scratch,size,4)!=FFX_OK) {
            std::free(scratch);scratch=nullptr;Log("[fsr3] ffxGetInterfaceDX12 failed\n");return false;
        }
        backendReady=true;
    }
    if(fiValid[0] && fiValid[1] && renderW<=contextRenderW && renderH<=contextRenderH &&
       displayW<=contextDisplayW && displayH<=contextDisplayH)return true;
    renderW=std::max(renderW,contextRenderW);renderH=std::max(renderH,contextRenderH);
    displayW=std::max(displayW,contextDisplayW);displayH=std::max(displayH,contextDisplayH);
    DestroyContexts();
    for(int eye=0;eye<2;++eye) {
        FfxFrameInterpolationContextDescription desc{};
        desc.backendInterface=backend;
        // The captures are the displayed SDR image (sRGB-encoded bytes), not scene-linear HDR.
        desc.flags=(inverted?FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INVERTED:0u)|(infinite?FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INFINITE:0u);
        desc.maxRenderSize={renderW,renderH};desc.displaySize={displayW,displayH};
        desc.backBufferFormat=FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
        if(ffxFrameInterpolationContextCreate(&fiContext[eye],&desc)!=FFX_OK) {
            Log("[fsr3] frame interpolation context eye %d failed\n",eye);DestroyContexts();return false;
        }
        fiValid[eye]=true;
        FfxOpticalflowContextDescription of{};
        of.backendInterface=backend;of.resolution={displayW,displayH};
        ofValid[eye]=ffxOpticalflowContextCreate(&ofContext[eye],&of)==FFX_OK;
        if(!ofValid[eye])Log("[fsr3] optical flow context eye %d failed: game motion vectors only\n",eye);
        // No history yet: the first dispatch after a (re)create resets.
        firstDispatch[eye]=true;
    }
    contextRenderW=renderW;contextRenderH=renderH;contextDisplayW=displayW;contextDisplayH=displayH;
    Log("[fsr3] contexts (re)created: render %ux%u display %ux%u\n",renderW,renderH,displayW,displayH);
    return true;
}

ID3D12Resource* MakeTexture(ID3D12Device* device,uint64_t w,uint32_t h,DXGI_FORMAT format,D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=1;
    d.MipLevels=1;d.Format=format;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* r=nullptr;
    return SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r))) ? r : nullptr;
}

// Optical flow's two outputs, sized as ffxOpticalflowGetSharedResourceDescriptions documents: one
// vector per 8x8 block of the display, and a fixed 3x1 scene change buffer.
bool EnsureOpticalFlow(ID3D12Device* device,int eye,int displayW,int displayH) {
    const int w=(displayW+7)/8,h=(displayH+7)/8;
    if(!ofVector[eye] || w>ofVectorW[eye] || h>ofVectorH[eye]) {
        ofVector[eye].Attach(MakeTexture(device,uint64_t(std::max(w,ofVectorW[eye])),uint32_t(std::max(h,ofVectorH[eye])),
            DXGI_FORMAT_R16G16_SINT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        if(!ofVector[eye])return false;
        ofVectorW[eye]=std::max(w,ofVectorW[eye]);ofVectorH[eye]=std::max(h,ofVectorH[eye]);
    }
    if(!ofScd[eye])ofScd[eye].Attach(MakeTexture(device,3,1,DXGI_FORMAT_R32_UINT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    return ofScd[eye]!=nullptr;
}

FfxResource Wrap(ID3D12Resource* resource,FfxResourceStates state,FfxSurfaceFormat format=FFX_SURFACE_FORMAT_UNKNOWN) {
    auto desc=GetFfxResourceDescriptionDX12(resource);
    if(format!=FFX_SURFACE_FORMAT_UNKNOWN)desc.format=format;
    return ffxGetResourceDX12(resource,desc,nullptr,state);
}

// ---- the interpolated frames, per eye, tagged with the capture they lead to --------------------
constexpr int kPoolSlots=3;
struct Slot {ComPtr<ID3D12Resource> texture;uint64_t serial{};};
std::mutex poolMutex;
Slot pool[2][kPoolSlots];
int poolNext[2]{};
// Replaced pool textures can still be in a copy the XR loop recorded; they are let go at shutdown only.
std::vector<ComPtr<ID3D12Resource>> retired;

ID3D12Resource* PoolSlot(ID3D12Device* device,int eye,const D3D12_RESOURCE_DESC& color) {
    auto& slot=pool[eye][poolNext[eye]];
    if(slot.texture) {
        // About to be overwritten: nothing may ask for what it held.
        std::lock_guard lock(poolMutex);slot.serial=0;
        const auto d=slot.texture->GetDesc();
        if(d.Width==color.Width && d.Height==color.Height)return slot.texture.Get();
        retired.push_back(slot.texture);slot={};
    }
    // The captures' format family, typeless as the second eye's captures are: the XR loop copies it
    // into the swapchain image or reads it through its sRGB decode exactly like a capture.
    ComPtr<ID3D12Resource> texture;
    texture.Attach(MakeTexture(device,color.Width,color.Height,DXGI_FORMAT_R8G8B8A8_TYPELESS,D3D12_RESOURCE_STATE_COPY_SOURCE));
    if(!texture)return nullptr;
    texture->SetName(L"CVR_FSR3_Interpolated");
    std::lock_guard lock(poolMutex);slot.texture=texture;slot.serial=0;
    return texture.Get();
}

// ---- our own command lists, on our own compute queue ---------------------------------------------
// COMPUTE, not DIRECT: everything recorded here -- the dilate pass, FidelityFX's frame interpolation and optical
// flow (their DX12 backend records dispatches, copies and barriers only) -- is compute work, and on a compute
// queue the GPU can run it alongside the game's graphics work. On the headset's DIRECT queue it took turns with
// the game's: fork67, same place, game 35-40 fps with FSR3 off, 33-34 with it interpolating but showing nothing.
ComPtr<ID3D12CommandQueue> computeQueue;
constexpr int kLists=3;
struct List {
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    uint64_t fence{};
    bool timed{};   // its two timestamps were resolved into the readback buffer
    std::shared_ptr<const framegen::Inputs> inputs[2];   // read by the GPU until the fence passes
};
List lists[kLists];
int listNext=0;
ComPtr<ID3D12Fence> fence;
uint64_t fenceValue=0;
HANDLE fenceEvent=nullptr;
// Five timestamps per list -- start, then per eye after its dilate and after its interpolation (the last one
// is the end) -- read back once the list's fence has passed. Elapsed GPU time: whatever else the GPU runs in
// between (other queues, other processes) is counted in it.
constexpr int kStamps=5;
ComPtr<ID3D12QueryHeap> timestamps;
ComPtr<ID3D12Resource> timestampReadback;
double ticksToMs=0;

bool EnsureLists(ID3D12Device* device) {
    if(fence)return true;
    if(!computeQueue) {
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;q.Priority=D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        if(FAILED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&computeQueue))))return false;
        computeQueue->SetName(L"CVR_FSR3_Compute");
    }
    for(auto& l:lists) {
        if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,IID_PPV_ARGS(&l.allocator))) ||
           FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COMPUTE,l.allocator.Get(),nullptr,IID_PPV_ARGS(&l.list))))return false;
        l.list->Close();
    }
    fenceEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    D3D12_QUERY_HEAP_DESC qh{};qh.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;qh.Count=kLists*kStamps;
    D3D12_HEAP_PROPERTIES rb{};rb.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=kLists*kStamps*sizeof(uint64_t);rd.Height=1;
    rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(device->CreateQueryHeap(&qh,IID_PPV_ARGS(&timestamps))) ||
       FAILED(device->CreateCommittedResource(&rb,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&timestampReadback))))
        timestamps.Reset();
    return fenceEvent && SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
}

// The list at `index` has left the GPU: add its time to the report.
void ReadTimestamps(int index,ID3D12CommandQueue* queue) {
    auto& l=lists[index];
    if(!l.timed || !timestampReadback)return;
    l.timed=false;
    if(!ticksToMs) {UINT64 frequency=0;if(FAILED(queue->GetTimestampFrequency(&frequency)) || !frequency)return;ticksToMs=1000.0/double(frequency);}
    const D3D12_RANGE range{size_t(index)*kStamps*8,size_t(index+1)*kStamps*8};
    void* data=nullptr;
    if(FAILED(timestampReadback->Map(0,&range,&data)))return;
    const auto* t=static_cast<const uint64_t*>(data)+index*kStamps;
    if(t[0]<=t[1] && t[1]<=t[2] && t[2]<=t[3] && t[3]<=t[4] && t[4]>t[0]) {
        std::lock_guard lock(gpuMutex);
        gpuSumMs+=double(t[4]-t[0])*ticksToMs;
        gpuDilateMs+=double((t[1]-t[0])+(t[3]-t[2]))*ticksToMs;
        gpuInterpolateMs+=double((t[2]-t[1])+(t[4]-t[3]))*ticksToMs;
        ++gpuCount;
    }
    const D3D12_RANGE none{0,0};
    timestampReadback->Unmap(0,&none);
}

void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}

// What the previous capture was, for "does this one follow it".
std::atomic<uint64_t> lastSerial{0};   // 0 = nothing to follow (also set from the settings thread)
uint64_t lastOrigin=0;
uint32_t lastFrame=0;

void Count(uint64_t Status::*counter) {std::lock_guard lock(statusMutex);++(status.*counter);}

void InterpolateUnguarded(ID3D12Device* device,const GpuWait (&waits)[2],ID3D12Resource* const colors[2],
    const std::shared_ptr<const framegen::Inputs> (&inputs)[2],uint64_t serial,double frameMs) {
    ++calls;
    for(int eye=0;eye<2;++eye) {
        const auto* in=inputs[eye].get();
        if(!colors[eye] || !in || !in->hasDepth || !in->hasMotion || !in->depth || !in->motion) {
            lastSerial=0;Count(&Status::skipped);SetText("Waiting for depth and motion vectors in both eyes");return;
        }
        if(in->motionWidth!=in->width || in->motionHeight!=in->height) {
            lastSerial=0;Count(&Status::skipped);SetText("Motion vectors are not at the render resolution: not supported");return;
        }
    }
    const auto& camera=inputs[0]->camera;
    const auto color=colors[0]->GetDesc();
    if(!EnsureLists(device) || !EnsureContexts(device,inputs[0]->width,inputs[0]->height,uint32_t(color.Width),color.Height,
        camera.inverted,camera.infinite) || !Fsr3Dilate_Init(device)) {
        faulted=true;SetText("FSR3 could not start: see cyberpunkvrport.log");return;
    }
    {std::lock_guard lock(statusMutex);status.ready=true;}
    auto* queue=computeQueue.Get();
    // A capture that follows the previous one in the same tracking space interpolates; any other only
    // primes the history (reset) and shows nothing in between.
    const uint64_t previous=lastSerial.load();
    const bool follows=previous && serial==previous+1 && inputs[0]->origin==lastOrigin &&
        inputs[0]->frameId==lastFrame+1 && inputs[1]->frameId==inputs[0]->frameId &&
        !inputs[0]->camera.reset && !inputs[1]->camera.reset && frameMs>0 && frameMs<=100;
    lastSerial=serial;lastOrigin=inputs[0]->origin;lastFrame=inputs[0]->frameId;

    const int index=listNext;
    auto& l=lists[index];listNext=(listNext+1)%kLists;
    if(l.fence && fence->GetCompletedValue()<l.fence) {
        fence->SetEventOnCompletion(l.fence,fenceEvent);
        if(WaitForSingleObject(fenceEvent,200)!=WAIT_OBJECT_0) {Count(&Status::skipped);lastSerial=0;return;}
    }
    ReadTimestamps(index,queue);
    l.inputs[0]=inputs[0];l.inputs[1]=inputs[1];
    if(FAILED(l.allocator->Reset()) || FAILED(l.list->Reset(l.allocator.Get(),nullptr))) {faulted=true;SetText("FSR3 command list failed");return;}
    auto* list=l.list.Get();
    const bool timed=timestamps && CyberpunkVR_XrRateLog;
    const auto stamp=[&](int at) {if(timed)list->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,UINT(index*kStamps+at));};
    stamp(0);
    const bool flow=opticalFlow.load(std::memory_order_relaxed);
    bool ok=true;
    for(int eye=0;eye<2 && ok;++eye) {
        const auto& in=*inputs[eye];
        const auto desc=colors[eye]->GetDesc();
        const int displayW=int(desc.Width),displayH=int(desc.Height);
        ID3D12Resource* output=PoolSlot(device,eye,desc);
        if(!output) {ok=false;break;}
        bool reset=!follows;
        if(firstDispatch[eye]) {reset=true;firstDispatch[eye]=false;}
        // The engine's vectors are UV offsets to the previous frame once its own scale is applied.
        const float scaleX=in.camera.motionScale[0]!=0 ? in.camera.motionScale[0] : 1.0f;
        const float scaleY=in.camera.motionScale[1]!=0 ? in.camera.motionScale[1] : 1.0f;
        const auto* dilated=Fsr3Dilate_Run(list,eye,in.depth.Get(),in.motion.Get(),int(in.width),int(in.height),scaleX,scaleY,in.camera.inverted);
        if(!dilated) {ok=false;break;}
        stamp(1+eye*2);
        Transition(list,colors[eye],D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list,output,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // Optical flow first, from the same color, as AMD's own FSR3 orchestration does.
        bool haveFlow=false;
        if(flow && ofValid[eye] && EnsureOpticalFlow(device,eye,displayW,displayH)) {
            FfxOpticalflowDispatchDescription of{};
            of.commandList=ffxGetCommandListDX12(list);
            of.color=Wrap(colors[eye],FFX_RESOURCE_STATE_COMPUTE_READ,FFX_SURFACE_FORMAT_R8G8B8A8_UNORM);
            of.opticalFlowVector=Wrap(ofVector[eye].Get(),FFX_RESOURCE_STATE_UNORDERED_ACCESS);
            of.opticalFlowSCD=Wrap(ofScd[eye].Get(),FFX_RESOURCE_STATE_UNORDERED_ACCESS);
            of.reset=reset;of.backbufferTransferFunction=FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;of.minMaxLuminance={0,1};
            haveFlow=ffxOpticalflowContextDispatch(&ofContext[eye],&of)==FFX_OK;
        }
        FfxFrameInterpolationDispatchDescription d{};
        d.commandList=ffxGetCommandListDX12(list);
        d.displaySize={uint32_t(displayW),uint32_t(displayH)};d.renderSize={in.width,in.height};
        d.currentBackBuffer=Wrap(colors[eye],FFX_RESOURCE_STATE_COMPUTE_READ,FFX_SURFACE_FORMAT_R8G8B8A8_UNORM);
        d.output=Wrap(output,FFX_RESOURCE_STATE_UNORDERED_ACCESS,FFX_SURFACE_FORMAT_R8G8B8A8_UNORM);
        d.dilatedDepth=Wrap(dilated->dilatedDepth,FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        d.dilatedMotionVectors=Wrap(dilated->dilatedMotionVectors,FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        d.reconstructPrevNearDepth=Wrap(dilated->reconstructPrevNearDepth,FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        d.interpolationRect={0,0,displayW,displayH};
        if(haveFlow) {
            d.opticalFlowVector=Wrap(ofVector[eye].Get(),FFX_RESOURCE_STATE_UNORDERED_ACCESS);
            d.opticalFlowSceneChangeDetection=Wrap(ofScd[eye].Get(),FFX_RESOURCE_STATE_UNORDERED_ACCESS);
            d.opticalFlowBufferSize={uint32_t((displayW+7)/8),uint32_t((displayH+7)/8)};
            d.opticalFlowScale={1.0f/float(displayW),1.0f/float(displayH)};
        }
        d.opticalFlowBlockSize=8;
        d.cameraNear=in.camera.nearPlane;d.cameraFar=in.camera.farPlane;
        d.cameraFovAngleVertical=in.camera.verticalFov;d.viewSpaceToMetersFactor=1.0f;
        d.frameTimeDelta=float(frameMs);d.reset=reset;
        d.backBufferTransferFunction=FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;d.minMaxLuminance[0]=0;d.minMaxLuminance[1]=1;
        const auto result=ffxFrameInterpolationDispatch(&fiContext[eye],&d);
        Transition(list,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(list,colors[eye],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        stamp(2+eye*2);
        if(result!=FFX_OK) {
            static int logs=0;if(logs++<8)Log("[fsr3] dispatch eye %d failed 0x%08X\n",eye,unsigned(result));
            ok=false;
        }
    }
    // Both eyes recorded: all five timestamps were written.
    if(timed && ok)list->ResolveQueryData(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,UINT(index*kStamps),kStamps,
        timestampReadback.Get(),uint64_t(index)*kStamps*8);
    l.timed=timed && ok;
    list->Close();
    // The barriers above leave every resource as it was found, so even a failed eye is submitted.
    for(const auto& wait:waits)if(wait.fence && wait.value)queue->Wait(wait.fence,wait.value);
    ID3D12CommandList* submit[]={list};queue->ExecuteCommandLists(1,submit);
    l.fence=++fenceValue;queue->Signal(fence.Get(),l.fence);
    if(!ok) {lastSerial=0;Count(&Status::skipped);SetText("FSR3 dispatch failed: see cyberpunkvrport.log");return;}
    if(!follows) {Count(&Status::primed);SetText("Priming: waiting for two consecutive frames");return;}
    {
        std::lock_guard lock(poolMutex);
        for(int eye=0;eye<2;++eye) {pool[eye][poolNext[eye]].serial=serial;poolNext[eye]=(poolNext[eye]+1)%kPoolSlots;}
    }
    Count(&Status::interpolated);
    SetText(flow ? "Interpolating: engine motion vectors and depth, optical flow" : "Interpolating: engine motion vectors and depth");
}
}  // namespace

Settings GetSettings() {std::lock_guard lock(settingsMutex);return settings;}
void SetSettings(Settings value) {
    std::lock_guard lock(settingsMutex);
    if(value.enabled!=settings.enabled)Log("[fsr3] frame generation %s\n",value.enabled?"on":"off");
    settings=value;
    enabled.store(value.enabled,std::memory_order_relaxed);
    opticalFlow.store(value.opticalFlow,std::memory_order_relaxed);
    show.store(value.show,std::memory_order_relaxed);
    // The engine's motion vectors are copied only while something interpolates with them.
    framegen::SetMotionCaptureEnabled(value.enabled);
    if(!value.enabled) {lastSerial=0;SetText("Off");}
}
bool Enabled() {return enabled.load(std::memory_order_relaxed) && !faulted.load(std::memory_order_relaxed);}
bool ShowGenerated() {return Enabled() && show.load(std::memory_order_relaxed);}
void Interpolate(ID3D12Device* device,const GpuWait (&waits)[2],ID3D12Resource* const colors[2],
    const std::shared_ptr<const framegen::Inputs> (&inputs)[2],uint64_t serial,double frameMs) {
    if(!Enabled() || !device || !colors)return;
    // A fault inside the SDK must not take the game down: it stops FSR3 until it is re-enabled.
    __try {InterpolateUnguarded(device,waits,colors,inputs,serial,frameMs);}
    __except(EXCEPTION_EXECUTE_HANDLER) {faulted=true;Log("[fsr3] dispatch FAULTED (SEH): FSR3 stopped\n");}
}
void QueueWaitForLast(ID3D12CommandQueue* queue) {
    if(queue && fence && fenceValue)queue->Wait(fence.Get(),fenceValue);
}
ID3D12Resource* Output(unsigned eye,uint64_t serial) {
    if(eye>1 || !serial)return nullptr;
    std::lock_guard lock(poolMutex);
    for(auto& slot:pool[eye])if(slot.serial==serial)return slot.texture.Get();
    return nullptr;
}
Status GetStatus() {
    std::lock_guard lock(statusMutex);auto result=status;result.faulted=faulted.load();
    if(result.faulted)std::snprintf(result.text,sizeof(result.text),"%s","Stopped by a fault: see cyberpunkvrport.log");
    else if(!enabled.load())std::snprintf(result.text,sizeof(result.text),"%s","Off");
    return result;
}
void ClearFault() {faulted=false;lastSerial=0;SetText("Restarted");}
void CountShown(Shown kind,uint64_t gap) {
    switch(kind) {
        case Shown::Midpoint:++shownMidpoint;break;
        case Shown::RealAfterMidpoint:++shownAfter;break;
        case Shown::Repeat:++shownRepeat;break;
        case Shown::Real:
            ++shownReal;
            if(gap==1)++realNoFrame;
            else if(gap>1) {++realGap;capturesNeverShown+=uint32_t(std::min<uint64_t>(gap-1,1000));}
            break;
    }
}
void Report() {
    static uint64_t lastInterpolated=0,lastPrimed=0,lastSkipped=0;
    const auto s=GetStatus();
    const uint64_t interpolated=s.interpolated-lastInterpolated,primed=s.primed-lastPrimed,skipped=s.skipped-lastSkipped;
    lastInterpolated=s.interpolated;lastPrimed=s.primed;lastSkipped=s.skipped;
    const uint32_t c=calls.exchange(0),m=shownMidpoint.exchange(0),a=shownAfter.exchange(0),r=shownReal.exchange(0),
        rp=shownRepeat.exchange(0),nf=realNoFrame.exchange(0),g=realGap.exchange(0),never=capturesNeverShown.exchange(0);
    double gpu=0,dilate=0,interpolate=0;
    {std::lock_guard lock(gpuMutex);
     if(gpuCount) {gpu=gpuSumMs/gpuCount;dilate=gpuDilateMs/gpuCount;interpolate=gpuInterpolateMs/gpuCount;}
     gpuSumMs=gpuDilateMs=gpuInterpolateMs=0;gpuCount=0;}
    if(!CyberpunkVR_XrRateLog || !Enabled())return;
    Log("[fsr3] dispatch GPU per capture, both eyes: %.2f ms = dilate %.2f + interpolation %.2f\n",gpu,dilate,interpolate);
    Log("[fsr3] captures %u: interpolated %llu, primed %llu, skipped %llu | GPU %.2f ms each | shown: midpoint %u, real after it %u, "
        "real %u (no interpolated frame %u, previous capture not shown %u: %u captures never shown), repeat %u\n",
        c,static_cast<unsigned long long>(interpolated),static_cast<unsigned long long>(primed),static_cast<unsigned long long>(skipped),
        gpu,m,a,r,nf,g,never,rp);
}
bool ParseSetting(const char* line,Settings& value) {
    int n{};
    if(sscanf_s(line,"xr_fsr3 = %d",&n)==1)value.enabled=n!=0;
    else if(sscanf_s(line,"xr_fsr3_optical_flow = %d",&n)==1)value.opticalFlow=n!=0;
    else return false;
    return true;
}
void WriteSettings(void* file,const Settings& s) {
    std::fprintf(static_cast<FILE*>(file),"xr_fsr3=%d\nxr_fsr3_optical_flow=%d\n",s.enabled,s.opticalFlow);
}
}
