#include "Render/DepthCommitGate.hpp"
#include "Render/StereoTargetArray.hpp"
#include "Render/StereoSceneState.hpp"
#include "Render/CommandResources.hpp"
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <stdexcept>
#include <cstring>
using namespace cvr::stereo;
using Microsoft::WRL::ComPtr;
namespace cvr::stereo::native_probe {bool Enabled(){return true;}bool ObserveBindings(){return true;}}
extern "C" {std::atomic<uint32_t> CyberpunkVR_NativeStereoProbeState{1};}
void Check(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D12 failed");}
void Require(bool v){if(!v)throw std::runtime_error("depth gate assertion failed");}
ComPtr<ID3D12Resource> Buffer(ID3D12Device* d,D3D12_HEAP_TYPE type,const void* data=nullptr,UINT bytes=16){
    D3D12_HEAP_PROPERTIES h{};h.Type=type;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;Check(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&desc,
        type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)));
    if(data){void* p{};Check(r->Map(0,nullptr,&p));std::memcpy(p,data,bytes);r->Unmap(0,nullptr);}return r;
}
int main()try{
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    Check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> messages;Check(device.As(&messages));
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameter.DescriptorTable={1,&range};
    const D3D12_ROOT_SIGNATURE_DESC rd{1,&parameter,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> rs,error,code;
    Check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&rs,&error));ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0,rs->GetBufferPointer(),rs->GetBufferSize(),IID_PPV_ARGS(&root)));
    scene_state::RootCreated(root.Get(),rs->GetBufferPointer(),rs->GetBufferSize());
    const char shader[]="[numthreads(1,1,1)] void CS(){}";
    Check(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"CS","cs_5_0",0,0,&code,&error));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={code->GetBufferPointer(),code->GetBufferSize()};ComPtr<ID3D12PipelineState> pso;
    Check(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    ComPtr<ID3D12DescriptorHeap> heap,dsv;D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));hd={D3D12_DESCRIPTOR_HEAP_TYPE_DSV,2,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
    Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsv)));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    const uint64_t values[]{123,456},zero[]{0,0};auto input=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,values),zeros=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,zero);
    const uint64_t changedValues[]{123,456ull|(1ull<<56)};auto changed=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,changedValues);
    for(UINT test=0;test<24;++test){
        D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=67;td.Height=39;td.DepthOrArraySize=td.MipLevels=1;
        td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G8X24_TYPELESS;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;StereoTargetArray initial;Check(initial.Initialize(device.Get(),td,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear));
        auto handle=dsv->GetCPUDescriptorHandleForHeapStart();const auto step=device->GetDescriptorHandleIncrementSize(hd.Type);
        for(UINT eye=0;eye<2;++eye){D3D12_DEPTH_STENCIL_VIEW_DESC v{};v.Format=clear.Format;v.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            v.Texture2DArray.ArraySize=1;v.Texture2DArray.FirstArraySlice=eye;device->CreateDepthStencilView(initial.Resource(),&v,{handle.ptr+eye*step});}
        ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),pso.Get(),IID_PPV_ARGS(&list)));
        scene_state::Reset(list.Get(),true);list->SetGraphicsRootSignature(root.Get());scene_state::Root(list.Get(),root.Get());
        ID3D12DescriptorHeap* bound=heap.Get();list->SetDescriptorHeaps(1,&bound);scene_state::Heaps(list.Get(),1,&bound);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);scene_state::Topology(list.Get(),D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for(UINT eye=0;eye<2;++eye)list->ClearDepthStencilView({handle.ptr+eye*step},D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,.3f,17,0,nullptr);
        const D3D12_RECT edge{66,38,67,39};
        if(test==1 || test==2)list->ClearDepthStencilView({handle.ptr+step},test==1?D3D12_CLEAR_FLAG_DEPTH:D3D12_CLEAR_FLAG_STENCIL,.4f,18,1,&edge);
        DepthCommitGate gate;Check(gate.Create(device.Get(),initial.Resource()));native_probe::DrawRecord draw{};draw.pipeline=reinterpret_cast<uintptr_t>(pso.Get());
        DepthCommitGate::BufferProof proof{input.Get(),test==6?changed.Get():input.Get(),16};
        if(test>=5){
            auto invalid=proof;invalid.bytes=20;Require(!gate.Prepare(list.Get(),draw,&invalid));
            invalid=proof;invalid.peer=nullptr;Require(!gate.Prepare(list.Get(),draw,&invalid));
            invalid=proof;invalid.bytes=3;Require(!gate.Prepare(list.Get(),draw,&invalid));
        }
        std::vector<DepthCommitGate::BufferProof> proofs;
        ComPtr<ID3D12Resource> rowsA,rowsB;
        if(test>=7){
            proofs={proof,{input.Get(),input.Get(),16}};
            if(test==8)proofs[0].peer=changed.Get();
            if(test==9)proofs[1].peer=changed.Get();
            if(test>=10 && test<20){
                const UINT widths[]{1,3,4,5,257};const auto width=widths[(test-10)/2];
                std::array<uint8_t,1024> a{},b{};b.fill(0x5a);
                for(UINT row=0;row<2;++row)for(UINT x=0;x<width;++x)a[row*512+x]=b[row*512+x]=uint8_t(17+x*3);
                if(test%2)b[512+width-1]^=0x80;
                rowsA=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,a.data(),UINT(a.size()));
                rowsB=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,b.data(),UINT(b.size()));
                proofs[1]={rowsA.Get(),rowsB.Get(),1024,width,512,2};
            }
            if(test>=20 && test<22){
                std::array<uint8_t,80> a{},b{};a.fill(0xa5);b.fill(0x5a);
                std::memcpy(a.data()+16,values,16);std::memcpy(b.data()+32,values,16);if(test==21)b[47]^=0x80;
                rowsA=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,a.data(),UINT(a.size()));
                rowsB=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,b.data(),UINT(b.size()));
                proofs[1]={rowsA.Get(),rowsB.Get(),16,0,0,0,16,32};
            }
            if(test>=22){
                proofs.assign(DepthCommitGate::MaxBufferProofs,proof);
                if(test==23)proofs.back().peer=changed.Get();
            }
            auto invalid=proofs;invalid[1].peer=nullptr;Require(!gate.Prepare(list.Get(),draw,invalid));
            invalid={proof};invalid[0].mainOffset=8;Require(!gate.Prepare(list.Get(),draw,invalid));
            invalid={proof};invalid[0].peerOffset=16;Require(!gate.Prepare(list.Get(),draw,invalid));
            for(const auto layout:std::array<std::array<UINT,3>,7>{{{5,4,2},{5,6,2},{1,4,65536},{17,20,1},{1,4,0},{0,4,2},{5,UINT_MAX-3,2}}}){
                invalid={proof,{input.Get(),input.Get(),16,layout[0],layout[1],layout[2]}};
                Require(!gate.Prepare(list.Get(),draw,invalid));
            }
            invalid.assign(DepthCommitGate::MaxBufferProofs+1,proof);Require(!gate.Prepare(list.Get(),draw,invalid));
            Require(gate.Prepare(list.Get(),draw,proofs));
        }else Require(gate.Prepare(list.Get(),draw,test>=5?&proof:nullptr));
        Require(!gate.Prepare(list.Get(),draw));
        auto destination=Buffer(device.Get(),D3D12_HEAP_TYPE_DEFAULT),readback=Buffer(device.Get(),D3D12_HEAP_TYPE_READBACK);
        list->CopyBufferRegion(destination.Get(),0,zeros.Get(),0,16);
        Require(gate.Bind(list.Get(),true));list->CopyBufferRegion(destination.Get(),0,input.Get(),0,8);
        Require(gate.Bind(list.Get(),false));list->CopyBufferRegion(destination.Get(),8,input.Get(),8,8);
        list->SetPredication(nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={destination.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);
        list->CopyBufferRegion(readback.Get(),0,destination.Get(),0,16);Require(gate.Readback(list.Get()));
        if(test==4){gate.Freeze();Require(!gate.Decide(true));}else Require(gate.Decide(test!=3));
        gate.Freeze();Check(list->Close());ID3D12CommandList* command=list.Get();auto keep=cvr::gpu::PrepareCommandResources(1,&command);
        queue->ExecuteCommandLists(1,&command);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(keep));Check(queue->Signal(fence.Get(),test+1));
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr);Check(fence->SetEventOnCompletion(test+1,event));
        const auto waited=WaitForSingleObject(event,5000);CloseHandle(event);Require(waited==WAIT_OBJECT_0);
        const bool accepted=test==0 || test==5 || test==7 || (test>=10 && test%2==0);
        uint64_t decision{};Check(gate.Read(decision));Require(decision==uint64_t(accepted));
        void* mapped{};Check(readback->Map(0,nullptr,&mapped));uint64_t actual[2]{};std::memcpy(actual,mapped,16);const D3D12_RANGE none{};readback->Unmap(0,&none);
        Require(actual[0]==(accepted?123u:0u) && actual[1]==(accepted?0u:456u));
        cvr::gpu::ResetCommandResources(list.Get());cvr::gpu::CollectCommandResources();
    }
    for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T bytes{};messages->GetMessage(i,nullptr,&bytes);std::vector<uint8_t> data(bytes);
        auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());Check(messages->GetMessage(i,m,&bytes));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",m->pDescription);Require(false);}}
    std::puts("PASS depth/stencil plus multiple exact buffer proofs, padded rows and byte tails, mismatch in either resource, rejected layouts, frozen admission and complementary predication");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
