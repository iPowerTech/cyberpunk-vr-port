// Fsr3Headset -- see Runtimes/Fsr3Headset.hpp.
#include "Runtimes/Fsr3Headset.hpp"

#include <chrono>

extern void Log(const char* fmt, ...);

namespace cvr::fsr3 {
namespace {
void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}
}

bool Headset::Ensure(XrSession session,int64_t wantedFormat,int32_t wantedWidth,int32_t wantedHeight) {
    if(sets[0][0].handle && sets[1][1].handle && width==wantedWidth && height==wantedHeight && format==wantedFormat)return true;
    if(!session || !wantedFormat || wantedWidth<=0 || wantedHeight<=0)return false;
    std::lock_guard lock(mutex);
    for(auto& set:sets)for(auto& sc:set) {if(sc.handle)xrDestroySwapchain(sc.handle);sc={};}
    filled=false;shownSerial=0;shownSet=-1;
    for(int s=0;s<2;++s)for(int e=0;e<2;++e) {
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.format=wantedFormat;info.sampleCount=1;info.width=wantedWidth;info.height=wantedHeight;
        info.faceCount=1;info.arraySize=1;info.mipCount=1;
        auto& sc=sets[s][e];
        uint32_t count=0;
        if(XR_FAILED(xrCreateSwapchain(session,&info,&sc.handle)) || XR_FAILED(xrEnumerateSwapchainImages(sc.handle,0,&count,nullptr)) || !count) {
            Log("[fsr3] headset swapchain %d/%d could not be created\n",s,e);
            for(auto& set:sets)for(auto& other:set) {if(other.handle)xrDestroySwapchain(other.handle);other={};}
            return false;
        }
        sc.images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        xrEnumerateSwapchainImages(sc.handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(sc.images.data()));
    }
    width=wantedWidth;height=wantedHeight;format=wantedFormat;
    Log("[fsr3] headset swapchains ready: 2 sets x 2 eyes, %dx%d format %lld\n",width,height,static_cast<long long>(format));
    return true;
}

void Headset::Destroy() {
    std::lock_guard lock(mutex);
    for(auto& set:sets)for(auto& sc:set) {if(sc.handle)xrDestroySwapchain(sc.handle);sc={};}
    for(int i=0;i<kLists;++i) {
        if(lists[i])lists[i]->Release();
        if(allocators[i])allocators[i]->Release();
        lists[i]=nullptr;allocators[i]=nullptr;listFence[i]=0;
    }
    if(fence)fence->Release();
    if(event)CloseHandle(event);
    fence=nullptr;event=nullptr;fenceValue=0;filled=false;shownSerial=0;shownSet=-1;width=height=0;format=0;
}

bool Headset::Fill(ID3D12Device* device,ID3D12CommandQueue* queue,uint64_t frameSerial,ID3D12Resource* const interpolated[2],
    ID3D12Resource* const real[2],const XrPosef (&mid)[2],const XrPosef (&realPoses)[2],const XrFovf fovs[2],double ms) {
    if(!device || !queue)return false;
    for(int e=0;e<2;++e)if(!interpolated[e] || !real[e])return false;
    // The XR loop holds this from its pick to its xrEndFrame; a frame it is still showing is not replaced.
    std::unique_lock lock(mutex,std::defer_lock);
    if(!lock.try_lock_for(std::chrono::milliseconds(4)) || !sets[0][0].handle)return false;
    if(!fence) {
        for(int i=0;i<kLists;++i)
            if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocators[i]))) ||
               FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocators[i],nullptr,IID_PPV_ARGS(&lists[i]))))return false;
            else lists[i]->Close();
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!event || FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))) {fence=nullptr;return false;}
    }
    // An image whose wait timed out stays acquired and is waited for again next time; until all four are
    // ready nothing is copied and the XR loop does not pick these sets.
    bool ready=true;
    for(auto& set:sets)for(auto& sc:set) {
        if(!sc.acquired) {
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if(xrAcquireSwapchainImage(sc.handle,&acquire,&sc.index)!=XR_SUCCESS || sc.index>=sc.images.size()) {ready=false;continue;}
            sc.acquired=true;sc.waited=false;
        }
        if(!sc.waited) {
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=0;
            sc.waited=xrWaitSwapchainImage(sc.handle,&wait)==XR_SUCCESS;
            ready=ready && sc.waited;
        }
    }
    if(!ready)return false;
    const int i=next;next=(next+1)%kLists;
    if(listFence[i] && fence->GetCompletedValue()<listFence[i]) {
        fence->SetEventOnCompletion(listFence[i],event);
        if(WaitForSingleObject(event,50)!=WAIT_OBJECT_0)return false;
    }
    if(FAILED(allocators[i]->Reset()) || FAILED(lists[i]->Reset(allocators[i],nullptr)))return false;
    auto* list=lists[i];
    // Swapchain images rest in RENDER_TARGET, the sources in COPY_SOURCE (captures, FSR3's pool).
    for(int s=0;s<2;++s)for(int e=0;e<2;++e) {
        auto* dst=sets[s][e].images[sets[s][e].index].texture;
        Transition(list,dst,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(dst,s==0 ? interpolated[e] : real[e]);
        Transition(list,dst,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    list->Close();
    ID3D12CommandList* submit[]={list};queue->ExecuteCommandLists(1,submit);
    listFence[i]=++fenceValue;queue->Signal(fence,listFence[i]);
    for(auto& set:sets)for(auto& sc:set) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(sc.handle,&release);
        sc.acquired=sc.waited=false;
    }
    serial=frameSerial;frameMs=ms;filled=true;
    for(int e=0;e<2;++e) {midPose[e]=mid[e];realPose[e]=realPoses[e];fov[e]=fovs[e];}
    return true;
}

Headset::Pick Headset::Choose(XrTime displayTime,XrTime period,uint64_t newest,bool spaced,std::unique_lock<std::timed_mutex>& lock) {
    lock=std::unique_lock(mutex);
    bool clean=filled && serial;
    for(auto& set:sets)for(auto& sc:set)clean=clean && sc.handle && !sc.acquired;
    // The newest capture has no pair (not interpolated, or its copies could not be made): the loop's own path.
    if(!clean || newest>serial) {lock.unlock();return {};}
    Pick pick;pick.serial=serial;
    if(serial!=shownSerial) {shownSerial=serial;shownSet=0;midTime=displayTime;pick.fresh=true;}
    else if(shownSet==0 && (!spaced || displayTime+period/2>=midTime+XrTime(frameMs*0.5e6))) {shownSet=1;pick.fresh=true;}
    pick.set=shownSet;pick.synthetic=shownSet==0;
    for(int e=0;e<2;++e) {pick.pose[e]=pick.synthetic ? midPose[e] : realPose[e];pick.fov[e]=fov[e];}
    return pick;
}
}
