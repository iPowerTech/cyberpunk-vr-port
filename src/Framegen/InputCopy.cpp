#include "Framegen/InputCopy.hpp"
#include "Render/RenderProbeScope.hpp"
#include <d3dcompiler.h>
#include <array>

extern void Log(const char*,...);
namespace cvr::framegen {
namespace {
using Microsoft::WRL::ComPtr;
uint32_t MotionPixelBytes(DXGI_FORMAT format) {
    switch(format) {
        case DXGI_FORMAT_R16G16_FLOAT:return 4;
        case DXGI_FORMAT_R32G32_FLOAT:
        // RT's RGBA16F texture still contains 2D motion in XY.
        // FidelityFX reads .xy, so preserve the native format and channels.
        case DXGI_FORMAT_R16G16B16A16_FLOAT:return 8;
        default:return 0;
    }
}
void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b,UINT subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
    if(a==b)return;D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    v.Transition={resource,subresource,a,b};list->ResourceBarrier(1,&v);
}
// The widened motion: the depth's size, writable by the widening pass, resting readable like every copied input.
bool WideTarget(ID3D12GraphicsCommandList* list,Inputs& out,DXGI_FORMAT format) {
    if(out.motion) {
        const auto d=out.motion->GetDesc();
        if(d.Width==out.width && d.Height==out.height && d.Format==format && (d.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))return true;
    }
    ComPtr<ID3D12Device> device;if(FAILED(list->GetDevice(IID_PPV_ARGS(&device))))return false;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=out.width;d.Height=out.height;
    d.DepthOrArraySize=1;d.MipLevels=1;d.Format=format;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> created;
    if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&created))))return false;
    created->SetName(L"CVR_Framegen_Motion");out.motion=created;return true;
}
}
bool CopyInput(ID3D12GraphicsCommandList* list,const InputTag& tag,Inputs& out) {
    if(!list || !tag.resource || tag.type>1)return false;
    const auto desc=tag.resource->GetDesc();
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count!=1 || desc.DepthOrArraySize!=1)return false;
    const uint32_t width=tag.width ? tag.width : uint32_t(desc.Width),height=tag.height ? tag.height : desc.Height;
    if(!width || !height || width>16384 || height>16384 || uint64_t(tag.x)+width>desc.Width || uint64_t(tag.y)+height>desc.Height)return false;
    DXGI_FORMAT format=desc.Format;
    if(tag.type==0) {
        switch(format) {
            case DXGI_FORMAT_R32_TYPELESS:case DXGI_FORMAT_D32_FLOAT:case DXGI_FORMAT_R32_FLOAT:
            case DXGI_FORMAT_R32G8X24_TYPELESS:case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:format=DXGI_FORMAT_R32_FLOAT;break;
            default:return false;
        }
    } else if(!MotionPixelBytes(format))return false;
    // MOTION SMALLER THAN THE DEPTH. TAA's velocity is half the render size in both directions (with DLSS off:
    // 1140x916 per eye against a 2280x1832 depth), and FSR 3 takes motion at the depth's size only. It is copied as
    // it is into motionSource and widened into `motion` before a consumer reads it (WidenMotion).
    const bool widen=tag.type==1 && out.hasDepth && width<=out.width && height<=out.height && (width<out.width || height<out.height);
    auto& target=tag.type==0 ? out.depth : widen ? out.motionSource : out.motion;
    if(target) {
        ComPtr<ID3D12Device> sourceDevice,targetDevice;
        if(FAILED(list->GetDevice(IID_PPV_ARGS(&sourceDevice))) || FAILED(target->GetDevice(IID_PPV_ARGS(&targetDevice))) ||
            sourceDevice.Get()!=targetDevice.Get())return false;
    }
    const bool recreate=!target || target->GetDesc().Width!=width || target->GetDesc().Height!=height || target->GetDesc().Format!=format;
    if(recreate) {
        ComPtr<ID3D12Device> device;if(FAILED(list->GetDevice(IID_PPV_ARGS(&device))))return false;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=width;d.Height=height;
        d.DepthOrArraySize=1;d.MipLevels=1;d.Format=format;d.SampleDesc.Count=1;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> created;
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&created))))return false;
        target=created;target->SetName(tag.type==0 ? L"CVR_Framegen_Depth" : widen ? L"CVR_Framegen_MotionSource" : L"CVR_Framegen_Motion");
        if(!out.readyFence && FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&out.readyFence))))return false;
    } else Barrier(list,target.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    const auto state=D3D12_RESOURCE_STATES(tag.state);
    Barrier(list,tag.resource,state,D3D12_RESOURCE_STATE_COPY_SOURCE,0);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=tag.resource;dst.pResource=target.Get();
    src.Type=dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_BOX box{tag.x,tag.y,0,tag.x+width,tag.y+height,1};
    list->CopyTextureRegion(&dst,0,0,0,&src,&box);
    Barrier(list,tag.resource,D3D12_RESOURCE_STATE_COPY_SOURCE,state,0);
    Barrier(list,target.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if(tag.type==0) {out.width=width;out.height=height;out.hasDepth=true;}
    else if(widen) {
        if(!WideTarget(list,out,format))return false;
        out.motionWidth=out.width;out.motionHeight=out.height;out.hasMotion=true;out.widen=true;
    }
    else {out.motionWidth=width;out.motionHeight=height;out.hasMotion=true;out.widen=false;}
    const uint32_t motionBytes=out.motion ? MotionPixelBytes(out.motion->GetDesc().Format) : 0;
    out.bytes=uint64_t(out.width)*out.height*4+uint64_t(out.motionWidth)*out.motionHeight*motionBytes+
        (out.widen && out.motionSource ? out.motionSource->GetDesc().Width*out.motionSource->GetDesc().Height*motionBytes : 0);
    return true;
}
namespace {
// WIDENING. Each pixel at the depth's size takes the source texel it falls in: no blend across an edge, where two
// surfaces move differently. The values stay as they are: the engine's vectors are UV offsets (Fsr3.cpp), which do
// not change with the texture's size. MOTION is the format's channel count (two, or four for RGBA16F).
constexpr char kWidenHLSL[]=R"(
Texture2D<MOTION> source:register(t0);
RWTexture2D<MOTION> target:register(u0);
cbuffer Sizes:register(b0) {uint2 targetSize;uint2 sourceSize;};
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=targetSize))return;
    const uint2 at=min(uint2((float2(id.xy)+.5)*float2(sourceSize)/float2(targetSize)),sourceSize-1);
    target[id.xy]=source.Load(int3(at,0));
})";
struct Widener {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso[2];
    ComPtr<ID3D12Fence> fence;uint64_t value{};
    struct Slot {ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12DescriptorHeap> heap;uint64_t value{};};
    std::array<Slot,8> slots;unsigned next{};
    bool tried{},ready{};
    // Built once, on first use; a failure is logged and widening stays off (FSR 3 then refuses that motion as before).
    bool Ready(ID3D12Device* d) {
        if(tried)return ready && device.Get()==d;
        tried=true;device=d;
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
        ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1};
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,ranges};
        params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,4};
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=params;
        ComPtr<ID3DBlob> blob,error;
        if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error)) ||
            FAILED(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)))) {
            Log("[framegen] motion widening unavailable: root signature\n");return false;
        }
        for(int i=0;i<2;++i) {
            const D3D_SHADER_MACRO macros[]={{"MOTION",i ? "float4" : "float2"},{nullptr,nullptr}};
            ComPtr<ID3DBlob> code;
            if(FAILED(D3DCompile(kWidenHLSL,sizeof(kWidenHLSL)-1,nullptr,macros,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error))) {
                Log("[framegen] motion widening unavailable: %s\n",error ? static_cast<const char*>(error->GetBufferPointer()) : "compile");return false;
            }
            D3D12_COMPUTE_PIPELINE_STATE_DESC state{};state.pRootSignature=root.Get();state.CS={code->GetBufferPointer(),code->GetBufferSize()};
            if(FAILED(d->CreateComputePipelineState(&state,IID_PPV_ARGS(&pso[i])))) {Log("[framegen] motion widening unavailable: pipeline\n");return false;}
        }
        if(FAILED(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))) {Log("[framegen] motion widening unavailable: fence\n");return false;}
        ready=true;return true;
    }
};
Widener widener;   // used under ImageSubmissionMutex only
}
bool WidenMotion(ID3D12CommandQueue* queue,Inputs& in) {
    if(!queue || !in.motionSource || !in.motion)return false;
    const auto source=in.motionSource->GetDesc(),target=in.motion->GetDesc();
    ComPtr<ID3D12Device> device;
    if(FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || !widener.Ready(device.Get()))return false;
    auto& slot=widener.slots[widener.next%widener.slots.size()];
    if(slot.value && widener.fence->GetCompletedValue()<slot.value)return false;   // eight still on the GPU: this frame goes without
    cvr::stereo::probe::Scope internal;
    if(!slot.list) {
        Widener::Slot made;
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.NumDescriptors=2;heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&made.allocator))) ||
            FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,made.allocator.Get(),nullptr,IID_PPV_ARGS(&made.list))) ||
            FAILED(made.list->Close()) || FAILED(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&made.heap))))return false;
        made.list->SetName(L"CVR_Framegen_MotionWiden");slot=std::move(made);
    }
    const int variant=target.Format==DXGI_FORMAT_R16G16B16A16_FLOAT ? 1 : 0;
    if(FAILED(slot.allocator->Reset()) || FAILED(slot.list->Reset(slot.allocator.Get(),widener.pso[variant].Get())))return false;
    const auto step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu=slot.heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=source.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    device->CreateShaderResourceView(in.motionSource.Get(),&srv,cpu);cpu.ptr+=step;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=target.Format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(in.motion.Get(),nullptr,&uav,cpu);
    auto* list=slot.list.Get();
    ID3D12DescriptorHeap* heaps[]={slot.heap.Get()};list->SetDescriptorHeaps(1,heaps);
    list->SetComputeRootSignature(widener.root.Get());
    list->SetComputeRootDescriptorTable(0,slot.heap->GetGPUDescriptorHandleForHeapStart());
    const uint32_t sizes[4]={uint32_t(target.Width),target.Height,uint32_t(source.Width),source.Height};
    list->SetComputeRoot32BitConstants(1,4,sizes,0);
    Barrier(list,in.motion.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->Dispatch((uint32_t(target.Width)+7)/8,(target.Height+7)/8,1);
    Barrier(list,in.motion.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if(FAILED(list->Close()))return false;
    ID3D12CommandList* lists[]={list};queue->ExecuteCommandLists(1,lists);
    slot.value=++widener.value;++widener.next;
    queue->Signal(widener.fence.Get(),slot.value);
    return true;
}
}
