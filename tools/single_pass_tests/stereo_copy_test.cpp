#include "Render/StereoTargetArray.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <array>
#include <vector>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr,const char* text) {if(FAILED(hr)){std::fprintf(stderr,"%s: %08X\n",text,unsigned(hr));throw std::runtime_error(text);}}
static void Require(bool value,const char* text) {if(!value)throw std::runtime_error(text);}
static ComPtr<ID3D12Resource> Readback(ID3D12Device* device,UINT64 size) {
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;
    d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&result)),"Readback");return result;
}
static void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b,
    UINT subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
    if(a==b)return;D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition={r,subresource,a,b};list->ResourceBarrier(1,&x);
}
// Exercise the same COM slots as the helper, without recording rejected work.
struct CountingList {
    void** vtable{};std::array<void*,27> methods{};ID3D12Device* device{};UINT commands{};
    explicit CountingList(ID3D12Device* owner):device(owner){
        vtable=methods.data();methods[7]=reinterpret_cast<void*>(&Device);methods[8]=reinterpret_cast<void*>(&Type);
        methods[16]=reinterpret_cast<void*>(&Copy);methods[26]=reinterpret_cast<void*>(&Barriers);
    }
    ID3D12GraphicsCommandList* List(){return reinterpret_cast<ID3D12GraphicsCommandList*>(this);}
    static HRESULT STDMETHODCALLTYPE Device(CountingList* self,REFIID id,void** out){return self->device->QueryInterface(id,out);}
    static D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE Type(CountingList*){return D3D12_COMMAND_LIST_TYPE_DIRECT;}
    static void STDMETHODCALLTYPE Copy(CountingList* self,const D3D12_TEXTURE_COPY_LOCATION*,UINT,UINT,UINT,
        const D3D12_TEXTURE_COPY_LOCATION*,const D3D12_BOX*){++self->commands;}
    static void STDMETHODCALLTYPE Barriers(CountingList* self,UINT,const D3D12_RESOURCE_BARRIER*){++self->commands;}
};
static void ToReadback(ID3D12GraphicsCommandList* list,ID3D12Resource* src,UINT subresource,
    ID3D12Resource* dst,const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint) {
    D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=src;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;a.SubresourceIndex=subresource;
    b.pResource=dst;b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=footprint;list->CopyTextureRegion(&b,0,0,0,&a,nullptr);
}
static void Validate(ID3D12Device* device) {
    ComPtr<ID3D12InfoQueue> info;Check(device->QueryInterface(IID_PPV_ARGS(&info)),"InfoQueue");
    for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
        SIZE_T size{};info->GetMessage(i,nullptr,&size);std::vector<char> data(size);
        auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());info->GetMessage(i,m,&size);
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",m->pDescription);throw std::runtime_error("D3D12 validation error");}
    }
}
int main(int argc,char** argv) try {
    const bool hardware=argc>1 && std::strcmp(argv[1],"--hardware")==0;
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"Debug layer");debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Factory");
    ComPtr<IDXGIAdapter1> adapter;
    Check(hardware?factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)):
        factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"Adapter");
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"Device");
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"Queue");
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Fence");
    UINT64 serial{},comparisons{},bytesCompared{};
    const DXGI_FORMAT formats[]={DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R32G8X24_TYPELESS};
    for(auto format:formats)for(UINT width:{67u,129u}) {
        const UINT height=width/2+5;const bool depth=format==DXGI_FORMAT_R32G8X24_TYPELESS;
        const auto state=depth?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;
        desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=format;
        desc.Flags=depth?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL:D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{};clear.Format=depth?DXGI_FORMAT_D32_FLOAT_S8X24_UINT:format;
        cvr::stereo::StereoTargetArray pair;Check(pair.Initialize(device.Get(),desc,state,&clear),"Stereo array");
        Require(pair.PlaneCount()==(depth?2u:1u),"Unexpected format planes");
        ComPtr<ID3D12Resource> native;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_SOURCE,&clear,IID_PPV_ARGS(&native)),"Native 2D destination");
        const std::array<D3D12_RESOURCE_STATES,3> invalidStates{state,state,state};
        const auto validStates=std::span(invalidStates).first(pair.PlaneCount());CountingList rejected(device.Get());
        for(UINT count=0;count<=3;++count)if(count!=pair.PlaneCount()){
            const auto invalid=std::span(invalidStates).first(count);
            Require(pair.CopyEyeLayer(rejected.List(),0,state,native.Get(),invalid,validStates)==E_INVALIDARG,"Invalid before-plane count accepted");
            Require(pair.CopyEyeLayer(rejected.List(),0,state,native.Get(),validStates,invalid)==E_INVALIDARG,"Invalid after-plane count accepted");
            Require(rejected.commands==0,"Rejected plane count recorded commands");
        }
        ComPtr<ID3D12Resource> predicates,predicateUpload;
        D3D12_RESOURCE_DESC predicateDesc{};predicateDesc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;predicateDesc.Width=16;
        predicateDesc.Height=predicateDesc.DepthOrArraySize=predicateDesc.MipLevels=predicateDesc.SampleDesc.Count=1;
        predicateDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&predicateDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&predicates)),"GPU predicates");
        D3D12_HEAP_PROPERTIES uploadHeap{};uploadHeap.Type=D3D12_HEAP_TYPE_UPLOAD;
        Check(device->CreateCommittedResource(&uploadHeap,D3D12_HEAP_FLAG_NONE,&predicateDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&predicateUpload)),"Predicate upload");
        void* predicateData{};const D3D12_RANGE noRead{};Check(predicateUpload->Map(0,&noRead,&predicateData),"Predicate map");
        const UINT64 predicateValues[2]{0,1};std::memcpy(predicateData,predicateValues,sizeof(predicateValues));predicateUpload->Unmap(0,nullptr);
        const auto arrayDesc=pair.Resource()->GetDesc();const UINT subresources=pair.PlaneCount()*2;
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(subresources);std::vector<UINT> rows(subresources);std::vector<UINT64> rowBytes(subresources);UINT64 bytes{};
        device->GetCopyableFootprints(&arrayDesc,0,subresources,0,footprints.data(),rows.data(),rowBytes.data(),&bytes);
        auto reference=Readback(device.Get(),bytes),actual=Readback(device.Get(),bytes),imported=Readback(device.Get(),bytes);
        ComPtr<ID3D12DescriptorHeap> views;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=2;hd.Type=depth?D3D12_DESCRIPTOR_HEAP_TYPE_DSV:D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&views)),"View heap");
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE,2> handles{};
        for(UINT eye=0;eye<2;++eye) {
            auto handle=views->GetCPUDescriptorHandleForHeapStart();handle.ptr+=eye*device->GetDescriptorHandleIncrementSize(hd.Type);handles[eye]=handle;
            if(depth){D3D12_DEPTH_STENCIL_VIEW_DESC v{};v.Format=clear.Format;v.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY;v.Texture2DArray.FirstArraySlice=eye;v.Texture2DArray.ArraySize=1;device->CreateDepthStencilView(pair.Resource(),&v,handle);}
            else {D3D12_RENDER_TARGET_VIEW_DESC v{};v.Format=format;v.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2DARRAY;v.Texture2DArray.FirstArraySlice=eye;v.Texture2DArray.ArraySize=1;device->CreateRenderTargetView(pair.Resource(),&v,handle);}
        }
        // Reuse the same destinations to catch state-restoration errors.
        for(UINT cycle=0;cycle<8;++cycle) {
            const bool layered=cycle>=2,perPlane=cycle>=4,predicateRejected=cycle==4 || cycle==6;
            ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Allocator");
            ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"Commands");
            if(!cycle){
                list->CopyBufferRegion(predicates.Get(),0,predicateUpload.Get(),0,16);
                Barrier(list.Get(),predicates.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PREDICATION);
            }
            const D3D12_RECT region{7,3,LONG(width-9),LONG(height-4)};
            for(UINT eye=0;eye<2;++eye) {
                if(depth){const auto flags=D3D12_CLEAR_FLAGS(D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL);
                    list->ClearDepthStencilView(handles[eye],flags,.2f+.5f*eye,UINT8(17+63*eye+cycle),0,nullptr);
                    list->ClearDepthStencilView(handles[eye],flags,.3f+.4f*eye,UINT8(33+49*eye+cycle),1,&region);}
                else {const float color[4]{.1f+.6f*eye,.25f+.1f*cycle,.5f,.75f};
                    const float inner[4]{.5f,.1f+.7f*eye,.2f+.1f*cycle,1};
                    list->ClearRenderTargetView(handles[eye],color,0,nullptr);list->ClearRenderTargetView(handles[eye],inner,1,&region);}
            }
            Barrier(list.Get(),pair.Resource(),state,D3D12_RESOURCE_STATE_COPY_SOURCE);
            for(UINT sub=0;sub<subresources;++sub)ToReadback(list.Get(),pair.Resource(),sub,reference.Get(),footprints[sub]);
            Barrier(list.Get(),pair.Resource(),D3D12_RESOURCE_STATE_COPY_SOURCE,state);
            std::array<D3D12_RESOURCE_STATES,2> layerStates{state,state};
            if(layered){
                Require(pair.TransitionEye(list.Get(),2,state,state)==E_INVALIDARG,"Invalid layer transition accepted");
                Require(pair.ImportEyeLayer(list.Get(),0,state,nullptr,state)==E_INVALIDARG,"Null import accepted");
                Require(pair.CopyEyeLayer(list.Get(),0,state,pair.Resource(),state,state)==E_INVALIDARG,"Self export accepted");
                Check(pair.TransitionEye(list.Get(),1,state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),"Other eye read state");
                layerStates[1]=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            }
            Require(pair.CopyEye(list.Get(),2,state,native.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE)==E_INVALIDARG,"Invalid eye accepted");
            for(UINT eye=0;eye<2;++eye) {
                if(layered){
                    if(eye){
                        Check(pair.TransitionEye(list.Get(),0,layerStates[0],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),"First eye read state");
                        Check(pair.TransitionEye(list.Get(),1,layerStates[1],state),"Second eye write state");
                        layerStates={D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,state};
                    }
                    if(perPlane){
                        // The opposite eye makes a skipped copy distinguishable on every plane.
                        Check(pair.CopyEyeLayer(list.Get(),1-eye,layerStates[1-eye],native.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE),"Seed native predicate baseline");
                        Check(pair.TransitionEye(list.Get(),eye,layerStates[eye],D3D12_RESOURCE_STATE_COPY_SOURCE),"Array export source state");
                        layerStates[eye]=D3D12_RESOURCE_STATE_COPY_SOURCE;
                        const auto depthRead=D3D12_RESOURCE_STATES(D3D12_RESOURCE_STATE_DEPTH_READ|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                        const std::array<D3D12_RESOURCE_STATES,2> nativeStates=depth?
                            (eye?std::array{D3D12_RESOURCE_STATE_DEPTH_WRITE,depthRead}:std::array{depthRead,D3D12_RESOURCE_STATE_DEPTH_WRITE}):
                            std::array{eye?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COMMON};
                        auto nativeAfter=nativeStates;
                        if(cycle>=6){
                            if(depth)nativeAfter={nativeStates[1],nativeStates[0]};
                            else nativeAfter[0]=eye?D3D12_RESOURCE_STATE_RENDER_TARGET:D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                            for(UINT plane=0;plane<pair.PlaneCount();++plane)
                                Require(nativeAfter[plane]!=nativeStates[plane],"Distinct output-state fixture must change every plane");
                        }
                        const auto planeStates=std::span(nativeStates).first(pair.PlaneCount());
                        const auto afterStates=std::span(nativeAfter).first(pair.PlaneCount());
                        for(UINT plane=0;plane<pair.PlaneCount();++plane)Barrier(list.Get(),native.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,nativeStates[plane],plane);
                        list->SetPredication(predicates.Get(),predicateRejected?0:8,D3D12_PREDICATION_OP_EQUAL_ZERO);
                        Check(pair.CopyEyeLayer(list.Get(),eye,layerStates[eye],native.Get(),planeStates,afterStates),"Predicated per-plane native copy");
                        list->SetPredication(nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
                        // Require the requested output states even when the copy was skipped.
                        for(UINT plane=0;plane<pair.PlaneCount();++plane)Barrier(list.Get(),native.Get(),nativeAfter[plane],D3D12_RESOURCE_STATE_COPY_SOURCE,plane);
                    }else Check(pair.CopyEyeLayer(list.Get(),eye,layerStates[eye],native.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE),"Copy isolated layer");
                }else Check(pair.CopyEye(list.Get(),eye,state,native.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE),"Copy eye into native 2D target");
                for(UINT plane=0;plane<pair.PlaneCount();++plane)ToReadback(list.Get(),native.Get(),plane,actual.Get(),footprints[eye+plane*2]);
            }
            for(UINT invalid:{0u,4u,UINT_MAX})Require(pair.ImportEyes(list.Get(),invalid,state,native.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE)==E_INVALIDARG,"Invalid import mask accepted");
            Require(pair.ImportEye(list.Get(),2,state,native.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE)==E_INVALIDARG,"Invalid import eye accepted");
            if(layered){
                Check(pair.ImportEyeLayer(list.Get(),0,layerStates[0],native.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE),"Import isolated layer");
                for(UINT eye=0;eye<2;++eye)Check(pair.TransitionEye(list.Get(),eye,layerStates[eye],D3D12_RESOURCE_STATE_COPY_SOURCE),"Read back isolated layer");
            }else {
                Check(pair.ImportEyes(list.Get(),cycle?3:1,state,native.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE),"Import selected array layers");
                Barrier(list.Get(),pair.Resource(),state,D3D12_RESOURCE_STATE_COPY_SOURCE);
            }
            for(UINT sub=0;sub<subresources;++sub)ToReadback(list.Get(),pair.Resource(),sub,imported.Get(),footprints[sub]);
            Barrier(list.Get(),pair.Resource(),D3D12_RESOURCE_STATE_COPY_SOURCE,state);
            Check(list->Close(),"Close");ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);Check(queue->Signal(fence.Get(),++serial),"Signal");
            HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Event");Check(fence->SetEventOnCompletion(serial,event),"Fence completion");
            const DWORD waited=WaitForSingleObject(event,5000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"GPU timeout");
            void *a{},*b{},*c{};D3D12_RANGE range{0,SIZE_T(bytes)};Check(reference->Map(0,&range,&a),"Reference map");Check(actual->Map(0,&range,&b),"Actual map");
            Check(imported->Map(0,&range,&c),"Imported map");
            UINT64 different{},eyeDifference{};
            for(UINT sub=0;sub<subresources;++sub)for(UINT row=0;row<rows[sub];++row) {
                const auto offset=footprints[sub].Offset+row*footprints[sub].Footprint.RowPitch;
                const UINT copiedSub=predicateRejected?(sub^1u):sub;
                const auto copiedOffset=footprints[copiedSub].Offset+row*footprints[copiedSub].Footprint.RowPitch;
                const auto* lhs=static_cast<const unsigned char*>(a)+copiedOffset;const auto* rhs=static_cast<const unsigned char*>(b)+offset;
                for(UINT64 byte=0;byte<rowBytes[sub];++byte){different+=lhs[byte]!=rhs[byte];++bytesCompared;}
                const UINT importedSub=predicateRejected?sub:(sub|1u);
                const auto source=footprints[importedSub].Offset+row*footprints[importedSub].Footprint.RowPitch;
                const auto* expected=static_cast<const unsigned char*>(a)+source;const auto* cloned=static_cast<const unsigned char*>(c)+offset;
                for(UINT64 byte=0;byte<rowBytes[sub];++byte){different+=expected[byte]!=cloned[byte];++bytesCompared;}
            }
            for(UINT row=0;row<rows[0];++row)for(UINT64 byte=0;byte<rowBytes[0];++byte) {
                const auto left=footprints[0].Offset+row*footprints[0].Footprint.RowPitch+byte;
                const auto right=footprints[1].Offset+row*footprints[1].Footprint.RowPitch+byte;
                eyeDifference+=static_cast<const unsigned char*>(a)[left]!=static_cast<const unsigned char*>(a)[right];
            }
            D3D12_RANGE none{};reference->Unmap(0,&none);actual->Unmap(0,&none);imported->Unmap(0,&none);
            Require(eyeDifference>0,"Fixture eyes must differ");Require(different==0,"Copied eye/plane data differs");
            Validate(device.Get());comparisons+=subresources;
        }
    }
    std::printf("PASS %s: %llu eye/plane comparisons, %llu bytes, color/MV/depth/stencil, mixed eye/plane states, zero/one predication, rejected plane counts, padded rows and target reuse; no D3D12 validation errors\n",hardware?"hardware":"WARP",comparisons,bytesCompared);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
