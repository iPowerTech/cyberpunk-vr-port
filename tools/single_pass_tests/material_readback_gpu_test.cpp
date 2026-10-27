#include "Render/MaterialReadbackState.hpp"
#include "Render/CommandResources.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <intrin.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
using cvr::stereo::material_read::ReadbackState;
using Stage=ReadbackState::Stage;
using Permit=ReadbackState::PermitUpdate;
namespace {
constexpr UINT Bytes=292;
using Payload=std::array<uint8_t,Bytes>;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Check(HRESULT result){if(FAILED(result)){std::fprintf(stderr,"HRESULT %08X\n",UINT(result));throw std::runtime_error("D3D12 failure");}}
Payload Pattern(uint8_t seed){Payload result{};for(UINT i=0;i<Bytes;++i)result[i]=uint8_t(seed+i*13);return result;}
ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,UINT bytes,D3D12_HEAP_TYPE type){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
        type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,IID_PPV_ARGS(&resource)));return resource;
}
struct Gpu {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12InfoQueue> info;UINT64 serial{};
    Gpu(){
        ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> adapter;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));Check(device.As(&info));
        D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)));
        Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    }
    UINT64 Signal(){Check(queue->Signal(fence.Get(),++serial));return serial;}
    bool Complete(UINT64 value)const{
        const auto completed=fence->GetCompletedValue();Require(completed!=UINT64_MAX,"Device removed");return completed>=value;
    }
    void Wait(UINT64 value){
        if(!Complete(value)){
            const auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event creation failed");
            const auto result=fence->SetEventOnCompletion(value,event);
            const auto waited=SUCCEEDED(result)?WaitForSingleObject(event,5000):WAIT_FAILED;
            CloseHandle(event);Check(result);Require(waited==WAIT_OBJECT_0,"GPU completion timeout");
        }
        cvr::gpu::CollectCommandResources();
    }
    void CheckMessages(){
        for(UINT64 i=0;i<info->GetNumStoredMessages();++i){
            SIZE_T bytes{};Check(info->GetMessage(i,nullptr,&bytes));std::vector<uint8_t> storage(bytes);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());Check(info->GetMessage(i,message,&bytes));
            if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){
                std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("D3D12 validation failed");
            }
        }
    }
};
struct Gate {
    ComPtr<ID3D12Fence> fence;
    explicit Gate(Gpu& gpu){
        Check(gpu.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        Check(gpu.queue->Wait(fence.Get(),1));
    }
    ~Gate(){if(fence)fence->Signal(1);}
    void Open(){Check(fence->Signal(1));}
};
struct Row {
    ComPtr<ID3D12Resource> source,readback,permit;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    ReadbackState state;void* mapped{};
    Row(Gpu& gpu,const Payload& initial){
        source=Buffer(gpu.device.Get(),Bytes,D3D12_HEAP_TYPE_UPLOAD);
        readback=Buffer(gpu.device.Get(),Bytes,D3D12_HEAP_TYPE_READBACK);
        permit=Buffer(gpu.device.Get(),8,D3D12_HEAP_TYPE_UPLOAD);
        Check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        Check(gpu.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        WriteSource(initial);list->CopyBufferRegion(readback.Get(),0,source.Get(),0,Bytes);Check(list->Close());
        ID3D12CommandList* raw=list.Get();gpu.queue->ExecuteCommandLists(1,&raw);gpu.Wait(gpu.Signal());
        Check(allocator->Reset());Check(list->Reset(allocator.Get(),nullptr));
        const D3D12_RANGE none{};Check(permit->Map(0,&none,&mapped));Require(mapped!=nullptr,"Permit map was null");
        Disable();
        Require(cvr::gpu::KeepCommandResources(list.Get(),{source.Get(),readback.Get(),permit.Get()}),"Resource retention failed");
        list->SetPredication(permit.Get(),0,D3D12_PREDICATION_OP_EQUAL_ZERO);
        list->CopyBufferRegion(readback.Get(),0,source.Get(),0,Bytes);
        list->SetPredication(nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);Check(list->Close());
    }
    ~Row(){if(mapped){const D3D12_RANGE written{0,8};permit->Unmap(0,&written);}}
    void WriteSource(const Payload& bytes){
        void* data{};const D3D12_RANGE none{};Check(source->Map(0,&none,&data));Require(data!=nullptr,"Source map was null");
        std::memcpy(data,bytes.data(),bytes.size());const D3D12_RANGE written{0,Bytes};source->Unmap(0,&written);_mm_sfence();
    }
    void Disable(){*static_cast<volatile uint64_t*>(mapped)=0;_mm_sfence();}
    uint64_t PermitValue()const{return *static_cast<const volatile uint64_t*>(mapped);}
    UINT64 Submit(Gpu& gpu,bool valid){
        ID3D12CommandList* raw=list.Get();auto resources=cvr::gpu::PrepareCommandResources(1,&raw);
        Require(!resources.batches.empty(),"Recording lost retained resources");
        const auto update=state.BeforeSubmit(1,valid,cvr::gpu::HasCommandResource(list.Get(),permit.Get()));
        if(update!=Permit::None){*static_cast<volatile uint64_t*>(mapped)=update==Permit::Enable?1:0;_mm_sfence();}
        Require(!state.CanSeal(true),"Submission callback still pending at seal");
        gpu.queue->ExecuteCommandLists(1,&raw);cvr::gpu::SubmitCommandResources(gpu.queue.Get(),std::move(resources));
        const auto value=gpu.Signal();state.Submitted();return value;
    }
    Payload Read()const{
        Payload result{};void* data{};const D3D12_RANGE range{0,Bytes};Check(readback->Map(0,&range,&data));
        Require(data!=nullptr,"Readback map was null");std::memcpy(result.data(),data,Bytes);
        const D3D12_RANGE none{};readback->Unmap(0,&none);return result;
    }
};
void ValidAndReplay(Gpu& gpu){
    const auto initial=Pattern(7),expected=Pattern(83),changed=Pattern(151);
    Row row(gpu,initial);row.WriteSource(expected);Gate gate(gpu);
    const auto submitted=row.Submit(gpu,true);
    Require(row.PermitValue()==1 && !gpu.Complete(submitted),"Copy was not held in flight");
    Require(!row.state.Seal(gpu.Complete(submitted)),"Unfinished copy sealed");
    gate.Open();gpu.Wait(submitted);
    Require(row.state.Seal(gpu.Complete(submitted)),"Completed valid copy did not seal");row.Disable();
    Require(row.Read()==expected,"Valid copy bytes differ");
    Require(cvr::gpu::HasCommandResource(row.list.Get(),row.permit.Get()),"Seal detached replay ownership");
    row.WriteSource(changed);gpu.Wait(row.Submit(gpu,true));
    Require(row.state.ReplayObserved() && row.PermitValue()==0,"Sealed recording replay rearmed permit");
    Require(row.Read()==expected,"Replay overwrote sealed readback");
}
void RejectedMetadata(Gpu& gpu){
    const auto initial=Pattern(19);Row row(gpu,initial);row.WriteSource(Pattern(127));
    gpu.Wait(row.Submit(gpu,false));
    Require(row.state.State()==Stage::Rejected && row.PermitValue()==0 && !row.state.Seal(true),"Invalid metadata admitted copy");
    Require(row.Read()==initial,"Disabled copy changed readback");
}
void LateInvalidation(Gpu& gpu){
    const auto expected=Pattern(61);Row row(gpu,Pattern(3));row.WriteSource(expected);Gate gate(gpu);
    const auto submitted=row.Submit(gpu,true);row.state.Invalidated();
    Require(!gpu.Complete(submitted) && row.PermitValue()==1,"Late invalidation modified in-flight permit");
    Require(!row.state.DisarmRejected(gpu.Complete(submitted)),"Unfinished rejected copy disarmed");
    gate.Open();gpu.Wait(submitted);
    Require(row.state.DisarmRejected(gpu.Complete(submitted)),"Completed rejected copy did not disarm");row.Disable();
    Require(row.state.State()==Stage::Rejected && !row.state.CanSeal(true),"Rejected payload became publishable");
    Require(row.Read()==expected,"Original permitted copy did not execute");
    row.WriteSource(Pattern(209));gpu.Wait(row.Submit(gpu,true));
    Require(row.PermitValue()==0 && row.Read()==expected,"Rejected recording replay overwrote readback");
}
}
int main()try{
    Gpu gpu;ValidAndReplay(gpu);RejectedMetadata(gpu);LateInvalidation(gpu);
    cvr::gpu::CollectCommandResources();gpu.CheckMessages();
    std::puts("PASS GPU material predicates, exact readback, rejection, delayed completion and sealed replay");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
