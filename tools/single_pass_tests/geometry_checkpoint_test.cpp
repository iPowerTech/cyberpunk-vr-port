#include "Render/GeometryTargetCheckpoint.hpp"
#include "Render/CommandResources.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace cvr::stereo;
static void Require(bool value){if(!value)throw std::runtime_error("Geometry checkpoint assertion failed");}
static void Check(HRESULT value){if(FAILED(value))throw std::runtime_error("Geometry checkpoint D3D12 failure: "+std::to_string(unsigned(value)));}
static D3D12_RESOURCE_STATES Writable(UINT slot){return slot==4?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET;}
struct Native {
    scene_state::GeometryTargets targets;
    ComPtr<ID3D12DescriptorHeap> rtvs,dsv;
    Native(ID3D12Device* device,UINT colors,UINT width=16,UINT height=8){
        const std::array<DXGI_FORMAT,5> formats{DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R32G8X24_TYPELESS};
        targets.colors=colors;D3D12_DESCRIPTOR_HEAP_DESC heap{D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
        Check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&dsv)));
        if(colors){heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=colors;Check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&rtvs)));}
        D3D12_HEAP_PROPERTIES memory{};memory.Type=D3D12_HEAP_TYPE_DEFAULT;
        for(UINT slot=0;slot<5;++slot)if(slot==4 || slot<colors){
            D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;
            desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=formats[slot];
            desc.Flags=slot==4?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL:D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            auto& resource=targets.resources[slot];Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&desc,Writable(slot),nullptr,IID_PPV_ARGS(&resource)));
            auto& handle=targets.handles[slot];const auto format=slot==4?DXGI_FORMAT_D32_FLOAT_S8X24_UINT:formats[slot];
            if(slot==4){handle=dsv->GetCPUDescriptorHandleForHeapStart();D3D12_DEPTH_STENCIL_VIEW_DESC view{};view.Format=format;view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
                device->CreateDepthStencilView(resource.Get(),&view,handle);
            }else{handle={rtvs->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(slot)*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV)};
                device->CreateRenderTargetView(resource.Get(),nullptr,handle);}
            targets.views[slot]={slot+1,reinterpret_cast<uintptr_t>(resource.Get()),width,height,UINT(formats[slot]),UINT(format),UINT(desc.Flags),0,0};
        }
    }
    void Clear(ID3D12GraphicsCommandList* list,bool post){
        const FLOAT before[4]{.125f,.25f,.375f,.5f},after[4]{.875f,.75f,.625f,1};
        for(UINT slot=0;slot<targets.colors;++slot)list->ClearRenderTargetView(targets.handles[slot],post?after:before,0,nullptr);
        list->ClearDepthStencilView(targets.handles[4],D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,post?.75f:.25f,post?173:37,0,nullptr);
    }
};
struct Readback {
    struct Plane {UINT slot{},plane{},width{},rows{},stride{},component{},bytes{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};};
    ComPtr<ID3D12Resource> buffer;std::vector<Plane> planes;UINT64 size{};
    void Record(ID3D12Device* device,ID3D12GraphicsCommandList* list,const scene_state::GeometryTargets& targets){
        for(UINT slot=0;slot<5;++slot)if(slot==4 || slot<targets.colors){
            const auto desc=targets.resources[slot]->GetDesc();
            for(UINT plane=0;plane<(slot==4?2u:1u);++plane){Plane item;item.slot=slot;item.plane=plane;item.width=UINT(desc.Width);
                UINT64 rowBytes{},total{};device->GetCopyableFootprints(&desc,plane,1,size,&item.layout,&item.rows,&rowBytes,&total);
                Require(total!=UINT64_MAX && item.rows==desc.Height && rowBytes%desc.Width==0);item.stride=UINT(rowBytes/desc.Width);
                item.bytes=slot==4 && plane?1u:4u;item.component=slot==4 && plane && item.stride==8?4u:0u;
                Require(item.component+item.bytes<=item.stride);size=(item.layout.Offset+UINT64(item.layout.Footprint.RowPitch)*item.rows+511)&~UINT64(511);planes.push_back(item);
            }
        }
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=size;desc.Height=1;
        desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES memory{};memory.Type=D3D12_HEAP_TYPE_READBACK;
        Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer)));
        for(UINT slot=0;slot<5;++slot)if(slot==4 || slot<targets.colors){
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition={targets.resources[slot].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,Writable(slot),D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&barrier);
            for(const auto& plane:planes)if(plane.slot==slot){D3D12_TEXTURE_COPY_LOCATION from{},to{};
                from.pResource=targets.resources[slot].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=plane.plane;
                to.pResource=buffer.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=plane.layout;
                list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
            }
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
        }
    }
    std::vector<std::vector<uint8_t>> Read(){
        void* mapped{};const D3D12_RANGE read{0,SIZE_T(size)};Check(buffer->Map(0,&read,&mapped));Require(mapped!=nullptr);
        std::vector<std::vector<uint8_t>> result;
        for(const auto& plane:planes){auto& bytes=result.emplace_back();
            for(UINT y=0;y<plane.rows;++y)for(UINT x=0;x<plane.width;++x){
                const auto* at=static_cast<const uint8_t*>(mapped)+plane.layout.Offset+UINT64(y)*plane.layout.Footprint.RowPitch+x*plane.stride+plane.component;
                bytes.insert(bytes.end(),at,at+plane.bytes);
            }
        }
        const D3D12_RANGE none{};buffer->Unmap(0,&none);return result;
    }
};
int main()try{
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    Check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> messages;Check(device.As(&messages));
    ComPtr<ID3D12CommandQueue> queue;const D3D12_COMMAND_QUEUE_DESC queueDesc{};Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)));
    for(UINT colors:{0u,3u,4u}){
        Native native(device.Get(),colors),differentShape(device.Get(),colors,8,8);
        auto before=std::make_unique<GeometryTargetCheckpoint>(),after=std::make_unique<GeometryTargetCheckpoint>();
        Require(!before->Captured() && !before->Targets().resources[4]);
        auto invalid=native.targets;invalid.colors=2;Require(FAILED(before->Create(device.Get(),invalid)));
        invalid=native.targets;invalid.views[4].viewFlags=D3D12_DSV_FLAG_READ_ONLY_DEPTH;Require(FAILED(before->Create(device.Get(),invalid)));
        Require(FAILED(before->Create(nullptr,native.targets)));Check(before->Create(device.Get(),native.targets));Check(after->Create(device.Get(),native.targets));
        Require(FAILED(before->Create(device.Get(),native.targets)) && !before->Targets().resources[4]);
        ComPtr<ID3D12CommandAllocator> allocator,copyAllocator;ComPtr<ID3D12GraphicsCommandList> list,copyList;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,IID_PPV_ARGS(&copyAllocator)));
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COPY,copyAllocator.Get(),nullptr,IID_PPV_ARGS(&copyList)));
        Require(!before->Capture(nullptr,native.targets) && !before->Capture(copyList.Get(),native.targets));Check(copyList->Close());
        Require(!before->Capture(list.Get(),differentShape.targets));
        invalid=native.targets;invalid.views[4].identity=0;Require(!before->Capture(list.Get(),invalid));
        invalid=native.targets;invalid.handles[4]={};Require(!before->Capture(list.Get(),invalid));
        if(colors){invalid=native.targets;invalid.resources[1]=invalid.resources[0];invalid.views[1]=invalid.views[0];invalid.handles[1]=invalid.handles[0];Require(!before->Capture(list.Get(),invalid));}
        Require(!before->Captured() && !before->Targets().resources[4]);native.Clear(list.Get(),false);
        Require(before->Capture(list.Get(),native.targets) && before->Captured());
        Require(!before->Capture(list.Get(),native.targets));
        const auto& beforeTargets=before->Targets();Require(beforeTargets.colors==colors);
        for(UINT slot=0;slot<5;++slot)if(slot==4 || slot<colors){const auto& view=beforeTargets.views[slot];
            Require(beforeTargets.resources[slot] && beforeTargets.resources[slot]!=native.targets.resources[slot] && beforeTargets.handles[slot].ptr && view.identity &&
                view.resource==reinterpret_cast<uintptr_t>(beforeTargets.resources[slot].Get()) && !view.viewFlags);}
        Readback nativeBefore,privateBefore,nativeAfter,privateAfter;
        nativeBefore.Record(device.Get(),list.Get(),native.targets);
        native.Clear(list.Get(),true);Require(after->Capture(list.Get(),native.targets));
        privateBefore.Record(device.Get(),list.Get(),before->Targets());
        nativeAfter.Record(device.Get(),list.Get(),native.targets);privateAfter.Record(device.Get(),list.Get(),after->Targets());
        Require(before->Targets().resources[4]!=after->Targets().resources[4]);
        // The recorded batch, not local checkpoint/native owners, must retain
        // all copy resources until the queue completes.
        native.targets={};before.reset();after.reset();
        Check(list->Close());ID3D12CommandList* command=list.Get();auto resources=cvr::gpu::PrepareCommandResources(1,&command);
        queue->ExecuteCommandLists(1,&command);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(resources));
        ComPtr<ID3D12Fence> done;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));Check(queue->Signal(done.Get(),1));
        for(UINT i=0;i<500 && done->GetCompletedValue()<1;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));Require(done->GetCompletedValue()==1);
        const auto expectedBefore=nativeBefore.Read(),actualBefore=privateBefore.Read(),expectedAfter=nativeAfter.Read(),actualAfter=privateAfter.Read();
        Require(expectedBefore==actualBefore && expectedAfter==actualAfter && actualBefore.size()==colors+2);
        for(size_t i=0;i<actualBefore.size();++i)Require(actualBefore[i]!=actualAfter[i]);
        Require(std::all_of(actualBefore.back().begin(),actualBefore.back().end(),[](auto value){return value==37;}) &&
            std::all_of(actualAfter.back().begin(),actualAfter.back().end(),[](auto value){return value==173;}));
        cvr::gpu::ResetCommandResources(list.Get());cvr::gpu::CollectCommandResources();
        std::printf("PASS checkpoint %u MRT: exact private pre/post color, depth and stencil; native content preserved\n",colors);
    }
    for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T bytes{};Check(messages->GetMessage(i,nullptr,&bytes));std::vector<uint8_t> storage(bytes);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());Check(messages->GetMessage(i,message,&bytes));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);Require(false);}}
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
