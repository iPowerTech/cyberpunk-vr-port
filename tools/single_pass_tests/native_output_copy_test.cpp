#include "Render/NativeOutputCopy.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <array>
#include <climits>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
using cvr::stereo::NativeOutputCopy;
static void Require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
static void Check(HRESULT hr,const char* text){if(FAILED(hr)){std::fprintf(stderr,"%s: %08X\n",text,unsigned(hr));throw std::runtime_error(text);}}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,UINT64 bytes,D3D12_HEAP_TYPE heapType,D3D12_RESOURCE_STATES state){
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=heapType;ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&result)),"Buffer");return result;
}
static void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after,UINT subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES){
    if(before==after)return;D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,subresource,before,after};list->ResourceBarrier(1,&barrier);
}
static void ReadPlane(ID3D12GraphicsCommandList* list,ID3D12Resource* from,UINT plane,ID3D12Resource* readback,
                      const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint){
    D3D12_TEXTURE_COPY_LOCATION source{},destination{};source.pResource=from;source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex=plane;destination.pResource=readback;destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint=footprint;list->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
}
static void Validate(ID3D12Device* device){
    ComPtr<ID3D12InfoQueue> messages;Check(device->QueryInterface(IID_PPV_ARGS(&messages)),"InfoQueue");
    for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T bytes{};messages->GetMessage(i,nullptr,&bytes);
        std::vector<char> data(bytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());messages->GetMessage(i,message,&bytes);
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("D3D12 validation error");}
    }
}
struct CountingList {
    void** vtable{};std::array<void*,56> methods{};ID3D12Device* device{};UINT commands{};
    D3D12_COMMAND_LIST_TYPE type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    explicit CountingList(ID3D12Device* owner):device(owner){vtable=methods.data();
        methods[7]=reinterpret_cast<void*>(&Device);methods[8]=reinterpret_cast<void*>(&Type);
        methods[16]=reinterpret_cast<void*>(&Copy);methods[26]=reinterpret_cast<void*>(&Barriers);methods[55]=reinterpret_cast<void*>(&Predicate);
    }
    ID3D12GraphicsCommandList* List(){return reinterpret_cast<ID3D12GraphicsCommandList*>(this);}
    static HRESULT STDMETHODCALLTYPE Device(CountingList* self,REFIID id,void** result){return self->device->QueryInterface(id,result);}
    static D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE Type(CountingList* self){return self->type;}
    static void STDMETHODCALLTYPE Copy(CountingList* self,const D3D12_TEXTURE_COPY_LOCATION*,UINT,UINT,UINT,const D3D12_TEXTURE_COPY_LOCATION*,const D3D12_BOX*){++self->commands;}
    static void STDMETHODCALLTYPE Barriers(CountingList* self,UINT,const D3D12_RESOURCE_BARRIER*){++self->commands;}
    static void STDMETHODCALLTYPE Predicate(CountingList* self,ID3D12Resource*,UINT64,D3D12_PREDICATION_OP){++self->commands;}
};
struct Fixture {
    NativeOutputCopy::Arrays arrays;
    NativeOutputCopy::Targets targets;
    std::array<D3D12_RESOURCE_DESC,4> descs{};
    ComPtr<ID3D12DescriptorHeap> rtvs,dsvs;
    std::array<std::array<D3D12_CPU_DESCRIPTOR_HANDLE,4>,2> arrayViews{},targetViews{};
    UINT width{},height{};
    static D3D12_RESOURCE_STATES Writable(UINT slot){return slot==3?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET;}
    Fixture(ID3D12Device* device,UINT size):width(size),height(size/2+5){
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.NumDescriptors=12;heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&rtvs)),"RTV heap");
        heap.NumDescriptors=4;heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;Check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&dsvs)),"DSV heap");
        const std::array<DXGI_FORMAT,4> formats{DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32G8X24_TYPELESS};
        D3D12_HEAP_PROPERTIES memory{};memory.Type=D3D12_HEAP_TYPE_DEFAULT;
        const auto view=[&](ID3D12Resource* resource,UINT slot,UINT layer,bool array,UINT index){
            const bool depth=slot==3;auto handle=(depth?dsvs:rtvs)->GetCPUDescriptorHandleForHeapStart();
            handle.ptr+=SIZE_T(index)*device->GetDescriptorHandleIncrementSize(depth?D3D12_DESCRIPTOR_HEAP_TYPE_DSV:D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            if(depth){D3D12_DEPTH_STENCIL_VIEW_DESC desc{};desc.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
                desc.ViewDimension=array?D3D12_DSV_DIMENSION_TEXTURE2DARRAY:D3D12_DSV_DIMENSION_TEXTURE2D;
                if(array){desc.Texture2DArray.FirstArraySlice=layer;desc.Texture2DArray.ArraySize=1;}
                device->CreateDepthStencilView(resource,&desc,handle);
            }else{D3D12_RENDER_TARGET_VIEW_DESC desc{};desc.Format=formats[slot];
                desc.ViewDimension=array?D3D12_RTV_DIMENSION_TEXTURE2DARRAY:D3D12_RTV_DIMENSION_TEXTURE2D;
                if(array){desc.Texture2DArray.FirstArraySlice=layer;desc.Texture2DArray.ArraySize=1;}
                device->CreateRenderTargetView(resource,&desc,handle);}
            return handle;
        };
        for(UINT slot=0;slot<4;++slot){auto& desc=descs[slot];desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width=width;desc.Height=height;desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=formats[slot];
            desc.Flags=slot==3?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL:D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_CLEAR_VALUE clear{};clear.Format=slot==3?DXGI_FORMAT_D32_FLOAT_S8X24_UINT:desc.Format;
            auto arrayDesc=desc;arrayDesc.DepthOrArraySize=2;
            Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&arrayDesc,Writable(slot),&clear,IID_PPV_ARGS(&arrays[slot])),"Array");
            for(UINT side=0;side<2;++side){auto& target=targets[side][slot];target.creation.kind=1;target.planeCount=slot==3?2:1;
                Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&desc,Writable(slot),&clear,IID_PPV_ARGS(&target.creation.resource)),"Target");
                const auto read=D3D12_RESOURCE_STATES(D3D12_RESOURCE_STATE_DEPTH_READ|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                target.finalStates=slot==3?(side?std::array{D3D12_RESOURCE_STATE_DEPTH_WRITE,read}:std::array{read,D3D12_RESOURCE_STATE_DEPTH_WRITE}):
                    std::array{slot==2 && !side?D3D12_RESOURCE_STATE_COPY_SOURCE:((side+slot)%2?D3D12_RESOURCE_STATE_RENDER_TARGET:D3D12_RESOURCE_STATES(192)),D3D12_RESOURCE_STATE_COMMON};
                arrayViews[side][slot]=view(arrays[slot].Get(),slot,side,true,slot==3?side:slot*2+side);
                targetViews[side][slot]=view(target.creation.resource.Get(),slot,0,false,slot==3?2+side:6+side*3+slot);
            }
        }
    }
    void Clear(ID3D12GraphicsCommandList* list){
        for(UINT side=0;side<2;++side)for(UINT slot=0;slot<4;++slot){
            if(slot==3){const auto flags=D3D12_CLEAR_FLAGS(D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL);
                list->ClearDepthStencilView(arrayViews[side][slot],flags,.25f+.5f*side,UINT8(40+60*side),0,nullptr);
                list->ClearDepthStencilView(targetViews[side][slot],flags,.10f+.1f*side,UINT8(11+4*side),0,nullptr);
            }else{const FLOAT image[4]{.1f+.6f*side,.2f+.08f*slot,.3f,1};
                const FLOAT baseline[4]{.7f-.4f*side,.8f-.05f*slot,.15f,.5f};
                list->ClearRenderTargetView(arrayViews[side][slot],image,0,nullptr);
                list->ClearRenderTargetView(targetViews[side][slot],baseline,0,nullptr);}
        }
    }
};
static void Rejections(ID3D12Device* device,const Fixture& fixture,ID3D12Resource* predicate){
    CountingList list(device);
    const auto reject=[&](const NativeOutputCopy::Arrays& arrays,const NativeOutputCopy::Targets& targets,ID3D12Resource* p,UINT64 offset){
        NativeOutputCopy copy;Require(FAILED(copy.Create(device,arrays,targets,p,offset)),"Invalid copy inputs accepted");
        Require(!copy.Record(list.List()) && !copy.Recorded() && !copy.Readback() && copy.Planes().empty() && !copy.Bytes(),"Rejected copy acquired output or recorded");
        Require(!list.commands,"Rejected copy emitted commands");
    };
    auto arrays=fixture.arrays;arrays[3].Reset();reject(arrays,fixture.targets,predicate,0);
    arrays=fixture.arrays;arrays[1]=arrays[0];reject(arrays,fixture.targets,predicate,0);
    auto targets=fixture.targets;targets[1][0]=targets[0][0];reject(fixture.arrays,targets,predicate,0);
    for(UINT planes:{0u,2u,UINT_MAX}){targets=fixture.targets;targets[0][0].planeCount=planes;reject(fixture.arrays,targets,predicate,0);}
    targets=fixture.targets;targets[1][3].planeCount=1;reject(fixture.arrays,targets,predicate,0);
    for(auto state:{D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATES(196),D3D12_RESOURCE_STATES(UINT_MAX)}){
        targets=fixture.targets;targets[0][0].finalStates[0]=state;reject(fixture.arrays,targets,predicate,0);}
    targets=fixture.targets;targets[1][3].finalStates[1]=D3D12_RESOURCE_STATE_RENDER_TARGET;reject(fixture.arrays,targets,predicate,0);
    reject(fixture.arrays,fixture.targets,predicate,1);reject(fixture.arrays,fixture.targets,predicate,16);
    reject(fixture.arrays,fixture.targets,fixture.arrays[0].Get(),0);reject(fixture.arrays,fixture.targets,nullptr,0);
    auto shape=fixture.descs[0];++shape.Width;D3D12_HEAP_PROPERTIES memory{};memory.Type=D3D12_HEAP_TYPE_DEFAULT;
    targets=fixture.targets;
    Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&shape,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,
        IID_PPV_ARGS(targets[1][0].creation.resource.ReleaseAndGetAddressOf())),"Mismatched target");
    reject(fixture.arrays,targets,predicate,0);
    shape=fixture.descs[0];const auto allocation=device->GetResourceAllocationInfo(0,1,&shape);
    D3D12_HEAP_DESC heap{};heap.SizeInBytes=allocation.SizeInBytes;heap.Alignment=allocation.Alignment;heap.Properties=memory;
    heap.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;ComPtr<ID3D12Heap> shared;
    Check(device->CreateHeap(&heap,IID_PPV_ARGS(&shared)),"Aliased target fixture heap");targets=fixture.targets;
    for(UINT slot=0;slot<2;++slot){auto& creation=targets[0][slot].creation;creation={};creation.kind=2;creation.heap=shared;
        creation.allocationBytes=allocation.SizeInBytes;
        Check(device->CreatePlacedResource(shared.Get(),0,&shape,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,
            IID_PPV_ARGS(&creation.resource)),"Aliased target fixture");}
    reject(fixture.arrays,targets,predicate,0);
    NativeOutputCopy copy;Check(copy.Create(device,fixture.arrays,fixture.targets,predicate,0),"Copy for list rejection");
    list.type=D3D12_COMMAND_LIST_TYPE_COPY;Require(!copy.Record(list.List()) && !list.commands,"COPY list emitted output commands");
    Require(!copy.Record(nullptr) && !list.commands,"Null list emitted output commands");
}
int main(int argc,char** argv)try{
    const bool hardware=argc>1 && std::strcmp(argv[1],"--hardware")==0;
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"Debug layer");debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Factory");ComPtr<IDXGIAdapter1> adapter;
    Check(hardware?factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)):
        factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"Adapter");
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"Device");
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC queueDesc{};
    Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)),"Queue");ComPtr<ID3D12Fence> fence;
    Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Fence");UINT64 serial{},checked{};
    for(UINT width:{67u,129u}){
        Fixture fixture(device.Get(),width);auto predicate=Buffer(device.Get(),16,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        auto upload=Buffer(device.Get(),16,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        void* address{};const D3D12_RANGE noRead{};Check(upload->Map(0,&noRead,&address),"Predicate upload map");
        const UINT64 values[2]{0,1};std::memcpy(address,values,sizeof(values));upload->Unmap(0,nullptr);
        Rejections(device.Get(),fixture,predicate.Get());
        for(UINT execute=0;execute<2;++execute){
            NativeOutputCopy copy;Check(copy.Create(device.Get(),fixture.arrays,fixture.targets,predicate.Get(),execute*8),"Create output copy");
            Require(copy.Planes().size()==10 && !copy.Recorded(),"Output shape");
            auto expected=Buffer(device.Get(),copy.Bytes(),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
            auto baseline=Buffer(device.Get(),copy.Bytes(),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
            auto after=Buffer(device.Get(),copy.Bytes(),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
            ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Allocator");
            ComPtr<ID3D12GraphicsCommandList> list;
            Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"List");
            if(!execute){list->CopyBufferRegion(predicate.Get(),0,upload.Get(),0,16);
                Transition(list.Get(),predicate.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PREDICATION);}
            fixture.Clear(list.Get());
            for(UINT slot=0;slot<4;++slot)Transition(list.Get(),fixture.arrays[slot].Get(),Fixture::Writable(slot),D3D12_RESOURCE_STATE_COPY_SOURCE);
            constexpr UINT sourceEye[2]{1,0};
            for(const auto& plane:copy.Planes()){const UINT slot=plane.slot==4?3:plane.slot;const auto& target=fixture.targets[plane.side][slot];
                Transition(list.Get(),target.creation.resource.Get(),Fixture::Writable(slot),D3D12_RESOURCE_STATE_COPY_SOURCE,plane.plane);
                ReadPlane(list.Get(),target.creation.resource.Get(),plane.plane,baseline.Get(),plane.footprint);
                ReadPlane(list.Get(),fixture.arrays[slot].Get(),sourceEye[plane.side]+plane.plane*2,expected.Get(),plane.footprint);
                Transition(list.Get(),target.creation.resource.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,target.finalStates[plane.plane],plane.plane);
            }
            // A preexisting false predicate must not suppress the helper's barriers or readback.
            list->SetPredication(predicate.Get(),0,D3D12_PREDICATION_OP_EQUAL_ZERO);
            Require(copy.Record(list.Get()) && copy.Recorded(),"Record output copy");
            Require(!copy.Record(list.Get()),"Output copy recorded twice");
            for(const auto& plane:copy.Planes()){const UINT slot=plane.slot==4?3:plane.slot;const auto& target=fixture.targets[plane.side][slot];
                Transition(list.Get(),target.creation.resource.Get(),target.finalStates[plane.plane],D3D12_RESOURCE_STATE_COPY_SOURCE,plane.plane);
                ReadPlane(list.Get(),target.creation.resource.Get(),plane.plane,after.Get(),plane.footprint);
                Transition(list.Get(),target.creation.resource.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,Fixture::Writable(slot),plane.plane);
            }
            for(UINT slot=0;slot<4;++slot)Transition(list.Get(),fixture.arrays[slot].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,Fixture::Writable(slot));
            Check(list->Close(),"Close");ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);
            Check(queue->Signal(fence.Get(),++serial),"Signal");HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event");
            Check(fence->SetEventOnCompletion(serial,event),"Set fence event");const auto waited=WaitForSingleObject(event,10000);CloseHandle(event);
            Require(waited==WAIT_OBJECT_0,"GPU fence timeout");
            void *reference{},*old{},*actual{},*restored{};const D3D12_RANGE range{0,SIZE_T(copy.Bytes())};
            Check(expected->Map(0,&range,&reference),"Expected map");Check(baseline->Map(0,&range,&old),"Baseline map");
            Check(copy.Readback()->Map(0,&range,&actual),"Output map");Check(after->Map(0,&range,&restored),"Restored map");
            for(const auto& plane:copy.Planes()){UINT64 distinguishable{};
                for(UINT y=0;y<plane.rows;++y)for(UINT x=0;x<width;++x)for(UINT b=0;b<plane.bytes;++b){
                    const auto offset=plane.footprint.Offset+UINT64(y)*plane.footprint.Footprint.RowPitch+UINT64(x)*plane.stride+plane.component+b;
                    const auto image=static_cast<const uint8_t*>(reference)[offset],prior=static_cast<const uint8_t*>(old)[offset];
                    distinguishable+=image!=prior;const auto wanted=execute?image:prior;
                    Require(static_cast<const uint8_t*>(actual)[offset]==wanted,"Predicated output differs");
                    Require(static_cast<const uint8_t*>(restored)[offset]==wanted,"Final-state readback differs");++checked;
                }
                Require(distinguishable>0,"Every color/depth/stencil plane must distinguish copy from baseline");
            }
            expected->Unmap(0,&noRead);baseline->Unmap(0,&noRead);copy.Readback()->Unmap(0,&noRead);after->Unmap(0,&noRead);
            Validate(device.Get());
        }
    }
    std::printf("PASS %s native output copy: 10 planes, both predicate values, mixed exact states, %llu component bytes\n",hardware?"hardware":"WARP",checked);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
