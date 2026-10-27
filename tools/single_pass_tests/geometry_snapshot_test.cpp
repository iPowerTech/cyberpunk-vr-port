#include "Render/GeometrySnapshot.hpp"
#include "Render/CommandResources.hpp"
#include "Render/NativeGeometrySnapshots.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <stdexcept>
using namespace cvr::stereo;
namespace cvr::stereo::native_probe {bool ObserveBindings(){return true;}}
using Microsoft::WRL::ComPtr;
static void Require(bool v,const char* text){if(!v)throw std::runtime_error(text);}
static void Check(HRESULT hr){if(FAILED(hr)){std::fprintf(stderr,"HRESULT %08X\n",UINT(hr));throw std::runtime_error("D3D12 failure");}}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,UINT bytes,D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> result;Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&result)));return result;
}
static geometry::BufferLease Lease(ID3D12Resource* r){const auto gpu=r->GetGPUVirtualAddress(),bytes=r->GetDesc().Width;return geometry::AcquireBuffer({reinterpret_cast<uintptr_t>(r),gpu,bytes},gpu,bytes);}
static void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,0,from,to};list->ResourceBarrier(1,&b);}
int main() try {
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> adapter;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> info;Check(device.As(&info));
    auto native=Buffer(device.Get(),1024,D3D12_HEAP_TYPE_DEFAULT),upload=Buffer(device.Get(),2048,D3D12_HEAP_TYPE_UPLOAD),instances=Buffer(device.Get(),1024,D3D12_HEAP_TYPE_UPLOAD);
    void* mapped{};Check(upload->Map(0,nullptr,&mapped));for(UINT i=0;i<1024;++i){static_cast<uint8_t*>(mapped)[i]=uint8_t(i*13);static_cast<uint8_t*>(mapped)[1024+i]=uint8_t(i*17+1);}upload->Unmap(0,nullptr);
    Check(instances->Map(0,nullptr,&mapped));std::memset(mapped,0xFF,1024);std::array<uint8_t,96> expected{};for(UINT i=0;i<96;++i)expected[i]=uint8_t(i*3);
    std::memcpy(static_cast<uint8_t*>(mapped)+3*48,expected.data(),96);std::memcpy(static_cast<uint8_t*>(mapped)+9*48,expected.data(),96);instances->Unmap(0,nullptr);
    ComPtr<ID3D12CommandAllocator> allocator,newAllocator;ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&newAllocator)));
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    list->CopyBufferRegion(native.Get(),0,upload.Get(),0,1024);const auto ia=D3D12_RESOURCE_STATES(D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER|D3D12_RESOURCE_STATE_INDEX_BUFFER);Transition(list.Get(),native.Get(),D3D12_RESOURCE_STATE_COPY_DEST,ia);
    geometry::DrawResources source;source.index=Lease(native.Get());source.vertices[0]=source.index;source.vertices[7]=Lease(instances.Get());source.instances.assign(96,0xEE);
    auto& draw=source.draw;draw.frame=1;draw.side=1;draw.pipeline=1;draw.root=2;draw.indices=3;draw.instances=2;draw.firstInstance=3;draw.flags=191;draw.inputSlotMask=129;
    draw.index={native->GetGPUVirtualAddress()+64,24,DXGI_FORMAT_R16_UINT};draw.vertices[0]={native->GetGPUVirtualAddress()+256,256,16};draw.vertices[7]={instances->GetGPUVirtualAddress(),1024,48};draw.viewport={0,0,128,64,0,1};draw.scissor={0,0,128,64};
    std::array<geometry::ReadState,17> states{};states[0]=states[1]={UINT(ia),2,0};states[8]={D3D12_RESOURCE_STATE_GENERIC_READ,1,5};
    source.draw.list=reinterpret_cast<uintptr_t>(list.Get());source.draw.sequence=1;
    CyberpunkVR_GeometryFreezeEnable=1;geometry_snapshots::Begin(true);geometry_snapshots::Capture(source,states,true);
    GeometrySnapshot left,right,different,rejected;auto bad=states;bad[0].status=5;
    Require(FAILED(rejected.Capture(list.Get(),source,bad,true)),"Unknown source state accepted");Require(FAILED(rejected.Capture(list.Get(),source,states,false)),"Unproved instance rebasing accepted");
    Check(left.Capture(list.Get(),source,states,true));Require(left.Bytes()==384,"Snapshot did not trim the instance subrange");Require(left.Read()==S_FALSE,"Readback ready before submission");
    Transition(list.Get(),native.Get(),ia,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyBufferRegion(native.Get(),0,upload.Get(),1024,1024);Transition(list.Get(),native.Get(),D3D12_RESOURCE_STATE_COPY_DEST,ia);
    source.draw.side=0;source.draw.firstInstance=9;Check(different.Capture(list.Get(),source,states,true));
    Transition(list.Get(),native.Get(),ia,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyBufferRegion(native.Get(),0,upload.Get(),0,1024);Transition(list.Get(),native.Get(),D3D12_RESOURCE_STATE_COPY_DEST,ia);
    Check(right.Capture(list.Get(),source,states,true));
    source.draw.sequence=2;geometry_snapshots::Capture(source,states,true);
    Transition(list.Get(),native.Get(),ia,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyBufferRegion(native.Get(),0,upload.Get(),1024,1024);Transition(list.Get(),native.Get(),D3D12_RESOURCE_STATE_COPY_DEST,ia);
    Check(list->Close());ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12Fence> gate,done;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));
    Require(!left.Seal(done.Get(),1),"A replayable source recording was sealed");
    Check(queue->Wait(gate.Get(),1));ID3D12CommandList* raw=list.Get();auto flight=cvr::gpu::PrepareCommandResources(1,&raw);auto frozenSubmission=geometry_snapshots::BeforeSubmit(1,&raw);
    Require(frozenSubmission.entries.size()==2,"Native snapshot submit did not retain this recording");
    queue->ExecuteCommandLists(1,&raw);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(flight));geometry_snapshots::Submitted(queue.Get(),std::move(frozenSubmission));Check(queue->Signal(done.Get(),1));
    Check(list->Reset(newAllocator.Get(),nullptr));cvr::gpu::ResetCommandResources(list.Get());Check(list->Close());
    Require(geometry_snapshots::BeforeSubmit(1,&raw).entries.empty(),"A reset list was mistaken for a snapshot replay");geometry_snapshots::Poll();
    for(auto* snapshot:{&left,&right,&different}){Require(snapshot->Seal(done.Get(),1),"Sealed source submission rejected");Require(snapshot->Read()==S_FALSE,"Pending GPU copy reported ready");}
    Check(gate->Signal(1));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event failed");Check(done->SetEventOnCompletion(1,event));const auto wait=WaitForSingleObject(event,5000);CloseHandle(event);Require(wait==WAIT_OBJECT_0,"Snapshot timeout");
    for(auto* snapshot:{&left,&right,&different})Check(snapshot->Read());
    geometry_snapshots::Poll();GeometryCommandPair collected;
    Require(!CyberpunkVR_GeometryFreezePending && !CyberpunkVR_GeometryFreezeFailure && geometry_snapshots::CopyPair(1,2,collected),"Native snapshot completion/pairing failed");
    geometry::DrawResources frozen;Require(left.CopyDraw(frozen) && frozen.instances==std::vector<uint8_t>(expected.begin(),expected.end()),"GPU instance data replaced by stale CPU snapshot");
    Require(frozen.draw.firstInstance==0 && frozen.draw.vertices[7].SizeInBytes==96 && frozen.index.resource!=native,"Native storage still used by frozen draw");
    GeometryCommandPair pair;Require(!GeometrySnapshot::Pair(left,different,pair),"Different GPU geometry bytes merged");Require(GeometrySnapshot::Pair(left,right,pair),"Matching GPU snapshots rejected after source overwrite");
    StereoGeometryCommands prepared;Check(prepared.Create(device.Get(),{&pair,1}));Require(prepared.InstanceBytes()==96,"Frozen geometry did not reach prepared draw path");
    for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size{};info->GetMessage(i,nullptr,&size);std::vector<uint8_t> bytes(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Check(info->GetMessage(i,m,&size));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",m->pDescription);throw std::runtime_error("D3D12 validation failed");}}
    // A duplicated command list in one ExecuteCommandLists batch is a replay,
    // even though there is only one API submission callback.
    Check(newAllocator->Reset());Check(list->Reset(newAllocator.Get(),nullptr));cvr::gpu::ResetCommandResources(list.Get());
    geometry_snapshots::Begin(true);source.draw.sequence=3;source.draw.side=1;
    Transition(list.Get(),native.Get(),D3D12_RESOURCE_STATE_COMMON,ia);geometry_snapshots::Capture(source,states,true);Check(list->Close());
    ID3D12CommandList* duplicates[]{list.Get(),list.Get()};Require(geometry_snapshots::BeforeSubmit(2,duplicates).entries.empty(),"Duplicate source recording was accepted");
    const auto* rows=reinterpret_cast<const geometry_snapshots::Record*>(CyberpunkVR_GeometryFreezeRecords);
    Require(CyberpunkVR_GeometryFreezeCount==1 && rows[0].status==5 && !CyberpunkVR_GeometryFreezePending,"Replay was not rejected");
    cvr::gpu::ResetCommandResources(list.Get());CyberpunkVR_GeometryFreezeEnable=0;geometry_snapshots::Begin(false);cvr::gpu::CollectCommandResources();
    std::puts("PASS GPU-position geometry copies, range compaction, source overwrite, asynchronous completion, recording seal and exact two-eye pairing");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
