#include "Render/StructuredSnapshotView.hpp"
#include "Render/CommandResources.hpp"
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace view=cvr::stereo::structured_snapshot_view;
namespace multilayer=cvr::stereo::multilayer_selectors;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Check(HRESULT value){if(FAILED(value))throw std::runtime_error("D3D12 fixture call failed");}
static void Math(){
    D3D12_SHADER_RESOURCE_VIEW_DESC a{},b;std::memset(&b,0xA5,sizeof(b));
    for(auto* desc:{&a,&b}){desc->Format=DXGI_FORMAT_UNKNOWN;desc->ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
        desc->Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc->Buffer.FirstElement=17;desc->Buffer.NumElements=8192;desc->Buffer.StructureByteStride=128;desc->Buffer.Flags=D3D12_BUFFER_SRV_FLAG_NONE;}
    Require(std::memcmp(&a,&b,sizeof(a)) && view::detail::SameBufferView(a,b),"Structure padding incorrectly changed SRV identity");
    for(UINT field=0;field<7;++field){auto changed=b;
        if(field==0)changed.Format=DXGI_FORMAT_R32_UINT;if(field==1)changed.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        if(field==2)changed.Shader4ComponentMapping++;if(field==3)changed.Buffer.FirstElement++;
        if(field==4)changed.Buffer.NumElements--;if(field==5)changed.Buffer.StructureByteStride=64;if(field==6)changed.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
        Require(!view::detail::SameBufferView(a,changed),"Changed SRV field borrowed a valid original view");
    }
    view::Mapping mapping;
    Require(view::detail::Reserve(0,3412,2048,view::MaxBytes,mapping) && mapping.targetOffset==436736 &&
        !mapping.privateFirstElement && mapping.nextFree==438784 && mapping.viewEnd==1048576,"First original-index mapping differs");
    uint64_t cursor=mapping.nextFree,width=mapping.viewEnd;
    for(UINT version=1;version<84;++version){
        const auto previous=cursor;Require(view::detail::Reserve(cursor,3412,2048,view::MaxBytes,mapping),"Bounded version was not reserved");
        Require(mapping.targetOffset>=previous,"Immutable version copies overlap");cursor=mapping.nextFree;width=std::max(width,mapping.viewEnd);
        for(UINT record=3412;record<3428;++record)Require((mapping.privateFirstElement+record)*128==mapping.targetOffset+uint64_t(record-3412)*128,
            "Original high record index no longer maps to its owned row");
    }
    Require(mapping.targetOffset==606720 && mapping.privateFirstElement==1328 && width==1218560,"84-version view domain bound differs");
    Require(view::detail::Reserve(0,7000,128,view::MaxBytes,mapping),"High first record rejected");cursor=mapping.nextFree;
    Require(view::detail::Reserve(cursor,1,128,view::MaxBytes,mapping) && mapping.targetOffset==cursor && mapping.privateFirstElement==7000,
        "Varying first record overlapped earlier copy data");
    Require(!view::detail::Reserve(16,3412,2048,view::MaxBytes,mapping) && !mapping.targetOffset,"Non-stride cursor acquired a mapping");
    Require(!view::detail::Reserve(0,8192,128,view::MaxBytes,mapping),"Record outside original8192 domain admitted");
    Require(!view::detail::Reserve(0,8191,256,view::MaxBytes,mapping),"Record span beyond original domain admitted");
    Require(!view::detail::Reserve(0,3412,4224,view::MaxBytes,mapping),"Unbounded copy span admitted");
    Require(!view::detail::Reserve(438784,3412,2048,1048576,mapping),"Copy fits but full SRV exceeds backing was admitted");
    Require(!view::detail::Reserve(UINT64_MAX-127,3412,2048,view::MaxBytes,mapping),"Destination arithmetic overflow admitted");
    Require(!view::detail::Reserve(0,3412,0,view::MaxBytes,mapping),"Empty uncaptured source became an owned version");
    view::Plan empty;view::Failure failure;Require(!view::BuildPlan({},empty,failure) && !empty.VersionCount(),"Empty plan acquired version ownership");
}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&resource)));return resource;
}
static void Execute(ID3D12CommandQueue* queue,ID3D12GraphicsCommandList* list,ID3D12Fence* fence,UINT64 value){
    Check(list->Close());ID3D12CommandList* raw=list;auto flight=cvr::gpu::PrepareCommandResources(1,&raw);
    queue->ExecuteCommandLists(1,&raw);cvr::gpu::SubmitCommandResources(queue,std::move(flight));Check(queue->Signal(fence,value));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event unavailable");
    const auto signal=fence->SetEventOnCompletion(value,event);if(FAILED(signal)){CloseHandle(event);Check(signal);}
    const auto waited=WaitForSingleObject(event,5000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"GPU fixture timed out");
}
static multilayer::StructuredView OriginalView(){
    multilayer::StructuredView original;original.key={std::string(multilayer::RootSha256),5,5,0,23,5};
    original.resourceWidth=view::DomainBytes;original.numElements=UINT(view::Elements);original.structureByteStride=128;
    original.dimension=1;original.componentMapping=5768;original.explicitDescription=true;return original;
}
static void Gpu(bool native=false,bool hardware=false){
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> adapter;
    if(hardware)Check(factory->EnumAdapters(0,&adapter));else Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> messages;device.As(&messages);
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC queueDesc{};Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> sourceAllocator,allocator;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&sourceAllocator)));
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> source,list;
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,sourceAllocator.Get(),nullptr,IID_PPV_ARGS(&source)));
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    std::array<std::array<uint8_t,228>,2> b4{};
    for(auto& bytes:b4){const float base=3412;const uint32_t mask=65535;std::memcpy(bytes.data()+128,&base,4);std::memcpy(bytes.data()+224,&mask,4);}
    std::array<uint8_t,4608> data{};
    for(UINT version=0;version<2;++version){
        for(UINT record=0;record<16;++record){const uint32_t value=(version+1)*1000+record+1,selector=100+version*100+record;
            std::memcpy(data.data()+version*2048+record*128,&value,4);std::memcpy(data.data()+version*2048+record*128+100,&selector,4);}
        std::memcpy(data.data()+4096+version*256,b4[version].data(),228);
    }
    auto upload=Buffer(device.Get(),data.size(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped{};Check(upload->Map(0,nullptr,&mapped));std::memcpy(mapped,data.data(),data.size());upload->Unmap(0,nullptr);
    auto dense=Buffer(device.Get(),native?1050624:4096,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto b4Gpu=Buffer(device.Get(),512,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto b4Readback=Buffer(device.Get(),512,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    if(native){
        auto original=Buffer(device.Get(),view::DomainBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        uint64_t cursor{};
        for(UINT version=0;version<2;++version){
            source->CopyBufferRegion(original.Get(),436736,upload.Get(),version*2048,2048);
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition={original.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};
            source->ResourceBarrier(1,&barrier);view::Mapping mapping;
            Require(view::detail::Reserve(cursor,3412,2048,1050624,mapping),"Original-index capture reservation rejected");
            source->CopyBufferRegion(dense.Get(),mapping.targetOffset,original.Get(),436736,mapping.copyBytes);cursor=mapping.nextFree;
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);source->ResourceBarrier(1,&barrier);
        }
        Require(cvr::gpu::KeepCommandResources(source.Get(),{original.Get()}),"Mutable original not retained");
    }else source->CopyBufferRegion(dense.Get(),0,upload.Get(),0,4096);
    source->CopyBufferRegion(b4Gpu.Get(),0,upload.Get(),4096,512);
    D3D12_RESOURCE_BARRIER sourceBarriers[2]{};
    for(UINT i=0;i<2;++i){sourceBarriers[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        sourceBarriers[i].Transition={i?b4Gpu.Get():dense.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,
            native && !i?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_SOURCE};}
    source->ResourceBarrier(2,sourceBarriers);source->CopyBufferRegion(b4Readback.Get(),0,b4Gpu.Get(),0,512);
    Require(cvr::gpu::KeepCommandResources(source.Get(),{dense.Get(),b4Gpu.Get(),b4Readback.Get(),upload.Get(),sourceAllocator.Get()}),"Source owners not retained");
    ComPtr<ID3D12Fence> sourceFence,fence,pendingFence;
    Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&sourceFence)));Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&pendingFence)));Execute(queue.Get(),source.Get(),sourceFence.Get(),1);
    Check(b4Readback->Map(0,nullptr,&mapped));for(UINT version=0;version<2;++version)Require(!std::memcmp(static_cast<uint8_t*>(mapped)+version*256,b4[version].data(),228),
        "Fixture private b4 did not match its actual GPU bytes");b4Readback->Unmap(0,nullptr);
    std::array<view::Input,2> inputs;
    for(UINT version=0;version<2;++version){auto& input=inputs[version];input.scope={123,reinterpret_cast<uintptr_t>(source.Get()),7,version+1,native?0:version};
        input.privateB4Scope=input.scope;input.originalView=OriginalView();input.privateB4Bytes=b4[version];input.privateB4GpuMatchVerified=input.allRecordPathsBounded=true;
        std::string error;Require(multilayer::PlanRead({std::string(multilayer::RootSha256),std::string(multilayer::VertexSha256),std::string(multilayer::PixelSha256)},
            {{std::string(multilayer::RootSha256),5,4,0,19,0},b4[version]},input.originalView,input.readPlan,error),"Original bounded plan failed");
        // This fixture owns the completed private data, submission and scope;
        // it does not manufacture or borrow a native original-read epoch.
        const auto offset=version*2048+(native?436736:0);
        input.dense={dense,dense,436736,offset,2048};input.copy={dense,input.scope.list,7,version+1,436736,offset,2048};
        input.completion={input.scope,queue,sourceFence,1,true,true};
    }
    view::Failure failure;view::Plan plan;
    Require(view::BuildPlan(inputs,plan,failure) && plan.VersionCount()==2 && plan.CopiedBytes()==4096 && plan.RequiredBytes()==1050624,"Owned version plan failed");
    for(UINT which=0;which<6;++which){auto bad=inputs;
        switch(which){case 0:bad[1].scope=bad[0].scope;break;case 1:bad[0].privateB4Scope.eye=1;break;
            case 2:bad[0].privateB4GpuMatchVerified=false;break;case 3:bad[0].allRecordPathsBounded=false;break;
            case 4:bad[0].copy.privateOffset+=128;break;default:bad[0].completion.finalized=false;break;}
        view::Plan rejected;Require(!view::BuildPlan(bad,rejected,failure) && !rejected.VersionCount(),"Torn input proof admitted an overlay");}
    auto incomplete=inputs;incomplete[0].completion.fence=pendingFence;view::Plan waiting;
    Require(view::BuildPlan(incomplete,waiting,failure),"Pending source plan should defer completion until record");view::Overlay waitOverlay;Check(waitOverlay.Create(device.Get(),waiting));
    view::DestinationRecording recording{reinterpret_cast<uintptr_t>(list.Get()),11,allocator,queue,true};
    Require(!waitOverlay.Record(list.Get(),recording,failure) && failure==view::Failure::Completion,"Uncompleted source fence allowed a GPU transfer");
    view::Overlay overlay;view::PrivateView privateView;
    if(native){
        view::Overlay uncompleted;Require(!uncompleted.AdoptSources(device.Get(),incomplete,failure),"Uncompleted native copy became consumable");
        for(UINT field=0;field<4;++field){auto bad=inputs;
            if(field==0){bad[1].scope.eye=1;bad[1].privateB4Scope=bad[1].completion.scope=bad[1].scope;}
            if(field==1)bad[1].dense.privateOffset=bad[1].copy.privateOffset=436736;
            if(field==2)bad[0].dense.privateOffset=bad[0].copy.privateOffset=0;
            if(field==3)bad[0].completion.finalized=false;
            view::Overlay rejected;Require(!rejected.AdoptSources(device.Get(),bad,failure),"Torn native copy accepted for adoption");
        }
        Require(overlay.AdoptSources(device.Get(),inputs,failure) && !overlay.AdoptSources(device.Get(),inputs,failure),"Native views did not adopt exactly once");
        Require(overlay.RequiredBytes()==1050624 && overlay.CopiedBytes()==4096,"Native view extent differs");
        Require(!overlay.Record(list.Get(),recording,failure),"Adopted source recorded a redundant copy");
    }else{
        Check(overlay.Create(device.Get(),plan));Require(!overlay.GetView(0,privateView),"Unrecorded overlay exposed a shader view");
        auto uncertified=recording;uncertified.certifiedCurrentRecording=false;
        Require(!overlay.Record(list.Get(),uncertified,failure),"Uncertified private recording acquired copy permission");
        Require(overlay.Record(list.Get(),recording,failure) && !overlay.Record(list.Get(),recording,failure),"Overlay copies did not record exactly once");
    }
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    ComPtr<ID3D12DescriptorHeap> heap;Check(device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&heap)));const auto stride=device->GetDescriptorHandleIncrementSize(heapDesc.Type);
    for(UINT version=0;version<2;++version){Require(overlay.GetView(version,privateView) && view::ValidatePrivateView(privateView) &&
        (native?!view::ValidatePrivateViewForRecording(privateView,list.Get(),recording):view::ValidatePrivateViewForRecording(privateView,list.Get(),recording)),"Qualified source/version association failed");
        if(native)Require(privateView.resource==dense && privateView.copyList==inputs[version].scope.list &&
            privateView.targetOffset==inputs[version].dense.privateOffset,"Adopted view allocated/copied instead of sharing source storage");
        device->CreateShaderResourceView(privateView.resource.Get(),&privateView.srv,{heap->GetCPUDescriptorHandleForHeapStart().ptr+version*stride});
        auto torn=privateView;++torn.srv.Buffer.FirstElement;Require(!view::ValidatePrivateView(torn),"Torn private SRV addressing accepted");
        torn=privateView;++torn.scope.occurrence;Require(!view::ValidatePrivateView(torn),"Another source occurrence borrowed the private view");
        auto foreign=recording;++foreign.generation;Require(!view::ValidatePrivateViewForRecording(privateView,list.Get(),foreign),"Foreign/reset generation consumed an unfinished overlay");}
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameter.DescriptorTable={1,&range};parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{1,&parameter,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> serialized,error;Check(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error));ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root)));
    constexpr char shader[]=R"(
struct Rec { uint4 v[8]; };
StructuredBuffer<Rec> records : register(t0);
float4 VS(uint id:SV_VertexID):SV_Position { float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1); }
uint4 PS(float4 p:SV_Position):SV_Target { uint n,s;records.GetDimensions(n,s);uint r=3412+(uint)p.x;return uint4(records[r].v[0].x,n,s,records[r].v[6].y); }
)";
    ComPtr<ID3DBlob> vs,ps;Check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&error));
    Check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&error));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=root.Get();pipeline.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pipeline.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pipeline.SampleMask=UINT_MAX;pipeline.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pipeline.RasterizerState.DepthClipEnable=TRUE;
    for(auto& blend:pipeline.BlendState.RenderTarget){blend.SrcBlend=D3D12_BLEND_ONE;blend.DestBlend=D3D12_BLEND_ZERO;blend.BlendOp=D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
        blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;}
    pipeline.DepthStencilState.DepthEnable=FALSE;pipeline.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    pipeline.DepthStencilState.StencilEnable=FALSE;pipeline.DepthStencilState.StencilReadMask=pipeline.DepthStencilState.StencilWriteMask=D3D12_DEFAULT_STENCIL_READ_MASK;
    pipeline.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};pipeline.DepthStencilState.BackFace=pipeline.DepthStencilState.FrontFace;
    pipeline.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pipeline.NumRenderTargets=1;pipeline.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_UINT;pipeline.SampleDesc.Count=1;
    ComPtr<ID3D12PipelineState> pso;Check(device->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(&pso)));
    D3D12_RESOURCE_DESC targetDesc{};targetDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;targetDesc.Width=16;targetDesc.Height=2;
    targetDesc.DepthOrArraySize=targetDesc.MipLevels=targetDesc.SampleDesc.Count=1;targetDesc.Format=DXGI_FORMAT_R32G32B32A32_UINT;targetDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES defaultHeap{};defaultHeap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> target;
    Check(device->CreateCommittedResource(&defaultHeap,D3D12_HEAP_FLAG_NONE,&targetDesc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target)));
    heapDesc={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};ComPtr<ID3D12DescriptorHeap> rtv;Check(device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&rtv)));
    device->CreateRenderTargetView(target.Get(),nullptr,rtv->GetCPUDescriptorHandleForHeapStart());const auto handle=rtv->GetCPUDescriptorHandleForHeapStart();
    const float zero[4]{};list->ClearRenderTargetView(handle,zero,0,nullptr);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);
    list->SetPipelineState(pso.Get());list->SetGraphicsRootSignature(root.Get());ID3D12DescriptorHeap* heaps[]{heap.Get()};list->SetDescriptorHeaps(1,heaps);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for(UINT version=0;version<2;++version){D3D12_VIEWPORT viewport{0,float(version),16,1,0,1};D3D12_RECT scissor{0,LONG(version),16,LONG(version+1)};
        list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);list->SetGraphicsRootDescriptorTable(0,{heap->GetGPUDescriptorHandleForHeapStart().ptr+version*stride});list->DrawInstanced(3,1,0,0);}
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};UINT rows{};UINT64 rowBytes{},total{};device->GetCopyableFootprints(&targetDesc,0,1,0,&layout,&rows,&rowBytes,&total);
    auto readback=Buffer(device.Get(),total,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={target.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=target.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=layout;
    list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    Require(cvr::gpu::KeepCommandResources(list.Get(),{heap.Get(),rtv.Get(),root.Get(),pso.Get(),target.Get(),readback.Get()}),"Consumer owners not retained");
    Require(overlay.ValidateDestinationSubmission(recording,queue.Get())!=native,"Private submission association differs");Execute(queue.Get(),list.Get(),fence.Get(),1);
    Check(readback->Map(0,nullptr,&mapped));
    for(UINT version=0;version<2;++version){const auto* output=reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(mapped)+layout.Offset+version*layout.Footprint.RowPitch);
        for(UINT record=0;record<16;++record)Require(output[record*4]==(version+1)*1000+record+1 && output[record*4+1]==8192 && output[record*4+2]==128 &&
            output[record*4+3]==100+version*100+record,"Original high index read zero/wrong version or changed structured domain");}
    readback->Unmap(0,nullptr);
    if(messages)for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T size{};messages->GetMessage(i,nullptr,&size);std::vector<uint8_t> bytes(size);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Check(messages->GetMessage(i,message,&size));Require(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR &&
            message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,"D3D12 validation reported an invalid private read");}
    cvr::gpu::CollectCommandResources();
}
int main(int argc,char** argv)try{
    Require(argc==1 || (argc==2 && (!std::strcmp(argv[1],"--gpu") || !std::strcmp(argv[1],"--native") || !std::strcmp(argv[1],"--native-hardware"))),"Expected GPU fixture mode");
    Math();if(argc==2)Gpu(std::strcmp(argv[1],"--gpu")!=0,!std::strcmp(argv[1],"--native-hardware"));
    std::puts("structured_snapshot_view_test: OK");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"structured_snapshot_view_test: %s\n",error.what());return 1;}
