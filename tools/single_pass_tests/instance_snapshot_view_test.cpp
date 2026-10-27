#include "Render/InstanceSnapshotView.hpp"
#include "Render/CommandResources.hpp"
#include "Render/InstanceGpuProbe.hpp"
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace cvr::stereo::native_probe {bool ObserveBindings(){return true;}}
using Microsoft::WRL::ComPtr;
namespace view=cvr::stereo::instance_snapshot_view;
namespace snapshot=cvr::stereo::shader_snapshot;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Check(HRESULT value){if(FAILED(value))throw std::runtime_error("D3D12 fixture HRESULT "+std::to_string(UINT(value)));}
static void Math(){
    const D3D12_VERTEX_BUFFER_VIEW original{0x100000,6*1024*1024,48};view::Mapping mapping;
    Require(view::detail::Reserve(0,original,8913,1,view::MaxBytes,mapping) && mapping.sourceOffset==427824 &&
        mapping.targetOffset==427824 && !mapping.viewOffset && mapping.copyBytes==48 && mapping.viewEnd==original.SizeInBytes,
        "High original firstInstance mapping differs");
    auto cursor=mapping.nextFree,width=mapping.viewEnd;
    for(UINT index=1;index<84;++index){const auto previous=cursor;
        Require(view::detail::Reserve(cursor,original,8913,1,view::MaxBytes,mapping) && mapping.targetOffset>=previous &&
            mapping.viewOffset+8913*48==mapping.targetOffset,"Selected immutable instance versions overlap or rebase coordinates");
        cursor=mapping.nextFree;width=(std::max)(width,mapping.viewEnd);
    }
    Require(width==original.SizeInBytes+83*48 && mapping.copyBytes==48,"Instance packing allocated a whole domain for each copy");
    const D3D12_VERTEX_BUFFER_VIEW wider{0x100000,2*1024*1024,64};
    Require(view::detail::Reserve(cursor,wider,1,2,view::MaxBytes,mapping) && mapping.targetOffset%64==0 &&
        mapping.targetOffset>=cursor && mapping.viewOffset+64==mapping.targetOffset,"Mixed strides did not preserve alignment and coordinates");
    Require(!view::detail::Reserve(427872,original,8913,1,original.SizeInBytes,mapping) && !mapping.copyBytes,
        "Selected bytes fit but full bound view exceeds backing was admitted");
    Require(!view::detail::Reserve(UINT64_MAX,original,0,1,view::MaxBytes,mapping),"Overflowing cursor admitted");
    Require(!view::detail::Reserve(0,original,131072,1,view::MaxBytes,mapping),"Instance outside original view admitted");
    Require(!view::detail::Reserve(0,original,131071,2,view::MaxBytes,mapping),"Instance span beyond original view admitted");
    Require(!view::detail::Reserve(0,original,0,0,view::MaxBytes,mapping),"Empty draw acquired a mapping");
    std::vector<snapshot::Input> layout{{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"INSTANCE",0,DXGI_FORMAT_R32G32B32A32_FLOAT,7,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE",1,DXGI_FORMAT_R32G32B32A32_FLOAT,7,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE",2,DXGI_FORMAT_R32G32B32A32_FLOAT,7,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1}};
    Require(view::detail::Layout(layout,48),"Actual slot7 append-aligned layout rejected");
    for(UINT which=0;which<7;++which){auto bad=layout;
        switch(which){case 0:bad[1].step=2;break;case 1:bad[1].classification=D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;break;
        case 2:bad[1].slot=6;break;case 3:bad[2].offset=40;break;case 4:bad[1].format=DXGI_FORMAT_BC7_UNORM;break;
        case 5:bad[1].semantic.clear();break;default:bad[0].step=1;break;}
        Require(!view::detail::Layout(bad,48),"Unsupported source instance input layout admitted");
    }
    layout[2].offset=0;layout[3].offset=16;
    Require(view::detail::Layout(layout,48),"Legitimate overlapping input elements rejected");
    layout[1].format=DXGI_FORMAT_R8_UINT;layout[2].format=DXGI_FORMAT_R32_UINT;layout[2].offset=D3D12_APPEND_ALIGNED_ELEMENT;
    layout[3].format=DXGI_FORMAT_R8G8B8A8_UINT;layout[3].offset=D3D12_APPEND_ALIGNED_ELEMENT;
    Require(view::detail::Layout(layout,48),"Small-format APPEND alignment rejected");
    view::Plan empty;view::Failure failure;Require(!view::BuildPlan({},empty,failure) && !empty.VersionCount(),"Empty plan acquired ownership");
}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,uint64_t bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&result)));return result;
}
static void Write(ID3D12Resource* resource,std::span<const uint8_t> bytes){
    void* mapped{};const D3D12_RANGE none{};Check(resource->Map(0,&none,&mapped));Require(mapped!=nullptr,"UPLOAD map returned null");
    std::memcpy(mapped,bytes.data(),bytes.size());const D3D12_RANGE written{0,bytes.size()};resource->Unmap(0,&written);
}
static void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&barrier);
}
static std::shared_ptr<std::vector<uint8_t>> InstanceBytes(UINT stride,UINT version){
    auto bytes=std::make_shared<std::vector<uint8_t>>(stride*2);
    for(UINT instance=0;instance<2;++instance){const std::array<float,16> fields{
        float(1+instance+version*1000),float(2+instance),float(3+instance),float(4+instance),
        instance?0.5f:-0.5f,0,0,0,10,20,30,40,100,200,300,400};
        std::memcpy(bytes->data()+instance*stride,fields.data(),stride);
    }
    return bytes;
}
static void Wait(ID3D12Fence* fence,uint64_t value){
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event unavailable");
    const auto signal=fence->SetEventOnCompletion(value,event);if(FAILED(signal)){CloseHandle(event);Check(signal);}
    const auto waited=WaitForSingleObject(event,10000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"GPU instance fixture timed out");
}
static void Gpu(ID3D12Device* device,UINT stride){
    ComPtr<ID3D12InfoQueue> messages;device->QueryInterface(IID_PPV_ARGS(&messages));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC queueDesc{};Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};rootDesc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> rootBytes,error;Check(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&rootBytes,&error));
    ComPtr<ID3D12RootSignature> root;Check(device->CreateRootSignature(0,rootBytes->GetBufferPointer(),rootBytes->GetBufferSize(),IID_PPV_ARGS(&root)));
    snapshot::RootCreated(root.Get(),rootBytes->GetBufferPointer(),rootBytes->GetBufferSize());
    constexpr char shader[]=R"(
struct Input { float2 position:POSITION; float4 color:INSTANCE0; float4 offset:INSTANCE1; float4 extra:INSTANCE2;
#if WIDE
float4 tail:INSTANCE3;
#endif
};
struct Output { float4 position:SV_Position; nointerpolation float4 color:COLOR; };
Output VS(Input input,uint instance:SV_InstanceID) { Output o; o.position=float4(input.position+input.offset.xy,0,1);
o.color=input.color+input.extra;
#if WIDE
o.color+=input.tail;
#endif
o.color.a+=instance*16; return o; }
float4 PS(Output input):SV_Target { return input.color; }
)";
    const D3D_SHADER_MACRO defines[]={{"WIDE",stride==64?"1":"0"},{nullptr,nullptr}};ComPtr<ID3DBlob> vs,ps;
    Check(D3DCompile(shader,sizeof(shader)-1,nullptr,defines,nullptr,"VS","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&error));
    Check(D3DCompile(shader,sizeof(shader)-1,nullptr,defines,nullptr,"PS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&error));
    const D3D12_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"INSTANCE",0,DXGI_FORMAT_R32G32B32A32_FLOAT,7,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE",1,DXGI_FORMAT_R32G32B32A32_FLOAT,7,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE",2,DXGI_FORMAT_R32G32B32A32_FLOAT,7,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE",3,DXGI_FORMAT_R32G32B32A32_FLOAT,7,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=root.Get();pipeline.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pipeline.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pipeline.InputLayout={elements,stride==64?5u:4u};pipeline.SampleMask=UINT_MAX;
    pipeline.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pipeline.RasterizerState.DepthClipEnable=TRUE;
    for(auto& blend:pipeline.BlendState.RenderTarget){blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;
        blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;}
    pipeline.DepthStencilState.DepthEnable=FALSE;pipeline.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    pipeline.DepthStencilState.StencilEnable=FALSE;pipeline.DepthStencilState.StencilReadMask=pipeline.DepthStencilState.StencilWriteMask=255;
    pipeline.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};pipeline.DepthStencilState.BackFace=pipeline.DepthStencilState.FrontFace;
    pipeline.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pipeline.NumRenderTargets=1;pipeline.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;pipeline.SampleDesc.Count=1;
    ComPtr<ID3D12PipelineState> pso;Check(device->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(&pso)));
    snapshot::GraphicsCreated(pso.Get(),root.Get(),pipeline.VS,pipeline.PS,pipeline.InputLayout);const auto actual=snapshot::Capture(pso.Get());
    Require(actual && actual->input.size()==pipeline.InputLayout.NumElements,"Actual PSO creation input-layout receipt missing");
    const UINT domain=stride==48?6*1024*1024:2*1024*1024,first=7;const D3D12_DRAW_INDEXED_ARGUMENTS draw{3,2,2,-4,first};
    auto original=Buffer(device,domain,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto a=InstanceBytes(stride,0),b=InstanceBytes(stride,1);const auto expectedA=*a,expectedB=*b;
    auto upload=Buffer(device,a->size()+b->size(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    std::vector<uint8_t> copied=*a;copied.insert(copied.end(),b->begin(),b->end());Write(upload.Get(),copied);
    std::array<view::Input,2> inputs;
    for(UINT version=0;version<2;++version){auto& input=inputs[version];input.scope={reinterpret_cast<uintptr_t>(root.Get()),100,5,version+1,version};
        input.pipeline=pso;input.shader=actual;input.original={original->GetGPUVirtualAddress(),domain,stride};input.draw=draw;input.bytes=version?b:a;}
    view::Plan plan;view::Failure failure;
    Require(view::BuildPlan(inputs,plan,failure) && plan.VersionCount()==2 && plan.CopiedBytes()==stride*4 &&
        plan.RequiredBytes()==domain+stride*2,"Sparse owning instance plan differs");
    for(UINT which=0;which<5;++which){auto bad=inputs;
        switch(which){case 0:bad[1].scope=bad[0].scope;break;case 1:bad[0].shader=std::make_shared<snapshot::Snapshot>(*actual);break;
        case 2:bad[0].draw.InstanceCount=3;break;case 3:bad[0].original.SizeInBytes=domain-1;break;default:bad[0].draw.StartInstanceLocation=UINT_MAX;break;}
        view::Plan rejected;Require(!view::BuildPlan(bad,rejected,failure) && !rejected.VersionCount(),"Torn input/PSO/instance domain admitted");}
    std::fill(a->begin(),a->end(),0);std::fill(b->begin(),b->end(),0);
    auto overlay=std::make_unique<view::Overlay>();Check(overlay->Create(device,plan));view::PrivateView notRecorded;
    Require(!overlay->GetView(0,notRecorded),"Unrecorded owning buffer exposed a usable view");
    view::DestinationRecording recording{reinterpret_cast<uintptr_t>(list.Get()),11,allocator,queue,true};auto invalid=recording;invalid.certifiedCurrentRecording=false;
    Require(!overlay->Record(list.Get(),invalid,failure) && overlay->Record(list.Get(),recording,failure) && !overlay->Record(list.Get(),recording,failure),"Instance overlay did not record exactly once");
    std::array<view::PrivateView,2> views;
    for(UINT version=0;version<2;++version){Require(overlay->GetView(version,views[version]) && view::ValidatePrivateView(views[version]) &&
        view::ValidatePrivateViewForRecording(views[version],list.Get(),recording),"Genuine instance/version association failed");
        for(UINT which=0;which<4;++which){auto bad=views[version];switch(which){case 0:bad.vertex.BufferLocation+=stride;break;case 1:++bad.draw.StartInstanceLocation;break;
            case 2:bad.version^=1;break;default:++bad.scope.occurrence;break;}Require(!view::ValidatePrivateView(bad),"Torn or aliased instance version admitted");}
        auto foreign=recording;++foreign.generation;Require(!view::ValidatePrivateViewForRecording(views[version],list.Get(),foreign),"Reset destination generation admitted");}
    const std::array<float,6> positions{-0.4f,-0.8f,-0.4f,0.8f,0.4f,0};const std::array<uint32_t,5> indices{99,98,4,5,6};
    auto vertices=Buffer(device,sizeof(positions),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto index=Buffer(device,sizeof(indices),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    Write(vertices.Get(),{reinterpret_cast<const uint8_t*>(positions.data()),sizeof(positions)});Write(index.Get(),{reinterpret_cast<const uint8_t*>(indices.data()),sizeof(indices)});
    auto nativeArena=std::make_unique<view::GpuCaptureArena>();
    cvr::stereo::native_recording::Lease nativeRecording{root,17,123,reinterpret_cast<uintptr_t>(list.Get()),456};
    Check(nativeArena->Create(device,nativeRecording,0));
    view::GpuCaptureArena invalidArena;auto invalidNative=nativeRecording;invalidNative.token.Reset();
    Require(FAILED(invalidArena.Create(device,invalidNative,0)),"Native copy arena accepted a missing recording owner");
    auto nativePermission=Buffer(device,8,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    const std::array<uint8_t,8> disabled{};Write(nativePermission.Get(),disabled);
    struct CopyContext {ID3D12Resource* source;ID3D12Resource* permission;UINT calls{};} copyContext{original.Get(),nativePermission.Get()};
    const auto captureCopy=[](void* context,ID3D12GraphicsCommandList* commands,const view::Input& input,ID3D12Resource* destination,uint64_t offset){
        auto& copy=*static_cast<CopyContext*>(context);++copy.calls;
        // Fixture admission only. Production rejoins the actual native draw and
        // allocation in NativeInstanceObserver before reaching this command.
        cvr::stereo::instance_gpu_probe::Record(commands,copy.source,uint64_t(input.draw.StartInstanceLocation)*input.original.StrideInBytes,
            destination,offset,uint64_t(input.draw.InstanceCount)*input.original.StrideInBytes,copy.permission,0);return true;};
    std::array<view::PrivateView,2> nativeViews;
    const auto captureNative=[&](UINT version){auto input=inputs[version];input.bytes.reset();input.scope={input.scope.root,nativeRecording.list,nativeRecording.generation,version+1,0};
        auto wrong=input;wrong.bytes=a;view::PrivateView rejected;
        Require(!nativeArena->Capture(list.Get(),wrong,captureCopy,&copyContext,rejected),"Native arena accepted CPU upload bytes");
        wrong=input;++wrong.scope.generation;
        Require(!nativeArena->Capture(list.Get(),wrong,captureCopy,&copyContext,rejected),"Native arena accepted a different recording generation");
        Require(nativeArena->Capture(list.Get(),input,captureCopy,&copyContext,nativeViews[version]),"Original-position GPU instance copy rejected");
        Require(!nativeArena->Capture(list.Get(),input,captureCopy,&copyContext,rejected),"Duplicate native occurrence copied twice");
        Require(!view::ValidateNativeView(nativeViews[version]),"Unfinalized native arena exposed a readable view");};
    D3D12_RESOURCE_DESC targetDesc{};targetDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;targetDesc.Width=32;targetDesc.Height=56;
    targetDesc.DepthOrArraySize=targetDesc.MipLevels=targetDesc.SampleDesc.Count=1;targetDesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;targetDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> target;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&targetDesc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target)));
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};ComPtr<ID3D12DescriptorHeap> rtv;
    Check(device->CreateDescriptorHeap(&rtvDesc,IID_PPV_ARGS(&rtv)));const auto handle=rtv->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(target.Get(),nullptr,handle);
    const float zero[4]{};list->ClearRenderTargetView(handle,zero,0,nullptr);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);
    list->SetGraphicsRootSignature(root.Get());list->SetPipelineState(pso.Get());list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VERTEX_BUFFER_VIEW vertex{vertices->GetGPUVirtualAddress(),sizeof(positions),8};const D3D12_INDEX_BUFFER_VIEW indexView{index->GetGPUVirtualAddress(),sizeof(indices),DXGI_FORMAT_R32_UINT};
    list->IASetVertexBuffers(0,1,&vertex);list->IASetIndexBuffer(&indexView);
    constexpr auto readable=D3D12_RESOURCE_STATES(D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER|D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(original.Get(),uint64_t(first)*stride,upload.Get(),0,stride*2);Barrier(list.Get(),original.Get(),D3D12_RESOURCE_STATE_COPY_DEST,readable);
    auto emit=[&](UINT row,const D3D12_VERTEX_BUFFER_VIEW& instances,const D3D12_DRAW_INDEXED_ARGUMENTS& args){
        const D3D12_VIEWPORT viewport{0,float(row*8),32,8,0,1};const D3D12_RECT scissor{0,LONG(row*8),32,LONG((row+1)*8)};
        list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);list->IASetVertexBuffers(7,1,&instances);
        list->DrawIndexedInstanced(args.IndexCountPerInstance,args.InstanceCount,args.StartIndexLocation,args.BaseVertexLocation,args.StartInstanceLocation);};
    captureNative(0);emit(0,inputs[0].original,draw);emit(1,views[0].vertex,views[0].draw);
    Barrier(list.Get(),original.Get(),readable,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyBufferRegion(original.Get(),uint64_t(first)*stride,upload.Get(),stride*2,stride*2);
    Barrier(list.Get(),original.Get(),D3D12_RESOURCE_STATE_COPY_DEST,readable);captureNative(1);
    emit(2,inputs[0].original,draw);emit(3,views[0].vertex,views[0].draw);emit(4,views[1].vertex,views[1].draw);
    Require(nativeArena->Finalize(list.Get()) && !nativeArena->Finalize(list.Get()),"Native instance arena did not finalize exactly once");
    Require(copyContext.calls==2 && nativeArena->VersionCount()==2 && nativeArena->CopiedBytes()==stride*4 && nativeArena->RequiredBytes()==domain+stride*2,
        "Original-position GPU copy packing differs");
    for(UINT version=0;version<2;++version){const auto& actualView=nativeViews[version];
        Require(view::ValidateNativeViewForRecording(actualView,list.Get(),nativeRecording),"Same-recording GPU consumer rejected without a CPU fence");
        auto changedRecording=nativeRecording;++changedRecording.generation;
        Require(!view::ValidateNativeViewForRecording(actualView,list.Get(),changedRecording),"Native generation mismatch admitted");
        for(UINT field=0;field<5;++field){auto bad=actualView;
            switch(field){case 0:++bad.draw.StartInstanceLocation;break;case 1:bad.vertex.BufferLocation+=stride;break;case 2:++bad.scope.occurrence;break;
                case 3:bad.version^=1;break;default:++bad.mapping.targetOffset;break;}
            Require(!view::ValidateNativeView(bad),"Native copy metadata changed without invalidation");}
        list->SetPredication(nativePermission.Get(),0,D3D12_PREDICATION_OP_EQUAL_ZERO);
        emit(5+version,actualView.vertex,actualView.draw);list->SetPredication(nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows{};uint64_t rowBytes{},total{};device->GetCopyableFootprints(&targetDesc,0,1,0,&footprint,&rows,&rowBytes,&total);
    auto readback=Buffer(device,total,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    auto instanceReadback=Buffer(device,plan.CopiedBytes(),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    for(UINT version=0;version<2;++version)list->CopyBufferRegion(instanceReadback.Get(),uint64_t(version)*stride*2,views[version].resource.Get(),views[version].mapping.targetOffset,stride*2);
    Barrier(list.Get(),target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=target.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    Require(cvr::gpu::KeepCommandResources(list.Get(),{vertices.Get(),index.Get(),original.Get(),upload.Get(),root.Get(),pso.Get(),target.Get(),rtv.Get(),readback.Get(),instanceReadback.Get(),nativePermission.Get()}),"Fixture GPU resources not retained");
    ComPtr<ID3D12Fence> block,fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&block)));Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    struct Unblock {ID3D12Fence* fence;~Unblock(){fence->Signal(1);}} unblock{block.Get()};
    Check(queue->Wait(block.Get(),1));Require(overlay->ValidateDestinationSubmission(recording,queue.Get()),"Exact destination submission rejected");
    const uint64_t allowed=1;Write(nativePermission.Get(),{reinterpret_cast<const uint8_t*>(&allowed),sizeof(allowed)});
    Check(list->Close());ID3D12CommandList* commands[]={list.Get()};auto flight=cvr::gpu::PrepareCommandResources(1,commands);
    queue->ExecuteCommandLists(1,commands);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(flight));Check(queue->Signal(fence.Get(),1));
    view::DestinationCompletion completion{recording,fence,1,true};Require(!view::ValidatePrivateViewAfterCompletion(views[0],completion),"Blocked GPU submission claimed completed instance copies");
    view::NativeCompletion nativeCompletion{nativeRecording,queue,fence,1,true};
    Require(view::ValidateNativeViewAfterSubmission(nativeViews[0],nativeCompletion),"Submitted native copy required a completed GPU fence");
    Require(!view::ValidateNativeViewAfterCompletion(nativeViews[0],nativeCompletion),"Blocked native submission claimed completion");
    ComPtr<ID3D12CommandQueue> copyQueue;auto copyQueueDesc=queueDesc;copyQueueDesc.Type=D3D12_COMMAND_LIST_TYPE_COPY;
    Check(device->CreateCommandQueue(&copyQueueDesc,IID_PPV_ARGS(&copyQueue)));
    for(UINT field=0;field<12;++field){auto bad=nativeCompletion;
        switch(field){case 0:bad.submissionCertified=false;break;case 1:bad.recording.token.Reset();break;
            case 2:bad.recording.token=allocator;break;case 3:++bad.recording.list;break;case 4:++bad.recording.generation;break;
            case 5:++bad.recording.context;break;case 6:++bad.recording.registry;break;case 7:bad.queue.Reset();break;
            case 8:bad.queue=copyQueue;break;case 9:bad.fence.Reset();break;case 10:bad.value=0;break;default:bad.value=UINT64_MAX;break;}
        Require(!view::ValidateNativeViewAfterSubmission(nativeViews[0],bad),"Torn native submission receipt admitted");}
    {auto wrongOwner=nativeViews[0];wrongOwner.owner=views[0].owner;
        Require(!view::ValidateNativeViewAfterSubmission(wrongOwner,nativeCompletion),"CPU-upload owner became a submitted native copy");}
    nativeArena.reset();
    IUnknown* owner=views[0].owner.Get();ID3D12Resource* privateResource=views[0].resource.Get();auto completedView=views[0];
    completedView.owner.Reset();completedView.resource.Reset();completedView.pipeline.Reset();completedView.shader.reset();
    views={};inputs={};plan={};overlay.reset();a.reset();b.reset();cvr::gpu::ResetCommandResources(list.Get());
    Require(!view::ValidateNativeViewForRecording(nativeViews[0],list.Get(),nativeRecording),"Recycled source retained current-recording permission");
    Require(view::ValidateNativeViewAfterSubmission(nativeViews[0],nativeCompletion) &&
        !view::ValidateNativeViewAfterCompletion(nativeViews[0],nativeCompletion),"Recycled submitted source lost its pending copy ownership");
    ComPtr<IUnknown> retained;Check(owner->QueryInterface(IID_PPV_ARGS(&retained)));Require(retained.Get()==owner,"Pending GPU submission lost its owning instance storage");
    completedView.owner=retained;completedView.resource=privateResource;completedView.pipeline=pso;completedView.shader=actual;retained.Reset();
    Check(block->Signal(1));Wait(fence.Get(),1);
    Require(view::ValidateNativeViewAfterCompletion(nativeViews[0],nativeCompletion),"Completed original GPU copy rejected");
    auto badNativeCompletion=nativeCompletion;badNativeCompletion.submissionCertified=false;
    Require(!view::ValidateNativeViewAfterCompletion(nativeViews[0],badNativeCompletion),"Uncertified original submission admitted");
    badNativeCompletion=nativeCompletion;++badNativeCompletion.recording.generation;
    Require(!view::ValidateNativeViewAfterCompletion(nativeViews[0],badNativeCompletion),"Unrelated original generation admitted");
    Require(view::ValidatePrivateViewAfterCompletion(completedView,completion),"Completed exact destination did not allow a later private consumer");
    auto uncertifiedCompletion=completion;uncertifiedCompletion.submissionCertified=false;
    Require(!view::ValidatePrivateViewAfterCompletion(completedView,uncertifiedCompletion),"Completed unrelated-label fence inferred an actual submission contract");
    auto foreignCompletion=completion;++foreignCompletion.recording.generation;
    Require(!view::ValidatePrivateViewAfterCompletion(completedView,foreignCompletion),"A completed fence legitimized a different destination generation");
    completedView={};
    void* mapped{};Check(instanceReadback->Map(0,nullptr,&mapped));Require(!std::memcmp(mapped,expectedA.data(),expectedA.size()) &&
        !std::memcmp(static_cast<uint8_t*>(mapped)+expectedA.size(),expectedB.data(),expectedB.size()),"CPU alias mutation changed private GPU instance versions");instanceReadback->Unmap(0,nullptr);
    Check(readback->Map(0,nullptr,&mapped));const auto* pixels=static_cast<const uint8_t*>(mapped)+footprint.Offset;bool changed{};
    for(UINT y=0;y<8;++y){const auto* originalA=pixels+uint64_t(y)*footprint.Footprint.RowPitch;
        const auto* privateA=pixels+uint64_t(y+8)*footprint.Footprint.RowPitch;
        const auto* originalB=pixels+uint64_t(y+16)*footprint.Footprint.RowPitch;
        const auto* retainedA=pixels+uint64_t(y+24)*footprint.Footprint.RowPitch;
        const auto* privateB=pixels+uint64_t(y+32)*footprint.Footprint.RowPitch;
        const auto* nativeA=pixels+uint64_t(y+40)*footprint.Footprint.RowPitch;
        const auto* nativeB=pixels+uint64_t(y+48)*footprint.Footprint.RowPitch;
        Require(!std::memcmp(originalA,privateA,size_t(rowBytes)) && !std::memcmp(originalA,retainedA,size_t(rowBytes)) &&
            !std::memcmp(originalB,privateB,size_t(rowBytes)) && !std::memcmp(originalA,nativeA,size_t(rowBytes)) && !std::memcmp(originalB,nativeB,size_t(rowBytes)),
            "POSITION/INSTANCE shader fetch changed original draw operands or version identity");
        changed|=std::memcmp(originalA,originalB,size_t(rowBytes))!=0;
    }
    Require(changed,"Source mutation did not change the rendered original instance input");
    for(const UINT x:{8u,24u}){float color{};std::memcpy(&color,pixels+4*footprint.Footprint.RowPitch+x*16,4);
        Require(color==float((x==8?11:12)+(stride==64?100:0)),"Instance draw was blank or fetched the wrong original firstInstance");}
    readback->Unmap(0,nullptr);cvr::gpu::CollectCommandResources();
    if(messages)for(UINT64 indexMessage=0;indexMessage<messages->GetNumStoredMessages();++indexMessage){SIZE_T bytes{};
        Check(messages->GetMessage(indexMessage,nullptr,&bytes));std::vector<uint8_t> data(bytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        Check(messages->GetMessage(indexMessage,message,&bytes));if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("GPU instance validation error");}}
    std::printf("Instance stride%u:2 sparse immutable versions,%u copied bytes,unchanged firstIndex/baseVertex/firstInstance,source mutation isolation and blocked-fence lifetime\n",stride,stride*4);
}
int main(int argc,char** argv)try{
    Math();if(argc==1){std::puts("instance_snapshot_view_test: math OK");return 0;}
    Require(argc==2 && (!std::strcmp(argv[1],"--gpu") || !std::strcmp(argv[1],"--hardware")),"Expected --gpu or --hardware");
    const bool hardware=!std::strcmp(argv[1],"--hardware");ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    if(hardware){for(UINT index=0;SUCCEEDED(factory->EnumAdapterByGpuPreference(index,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));++index){
        DXGI_ADAPTER_DESC1 desc{};Check(adapter->GetDesc1(&desc));if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))break;adapter.Reset();}if(!adapter)return 77;}
    else Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));Gpu(device.Get(),48);Gpu(device.Get(),64);
    std::printf("instance_snapshot_view_test: %s OK\n",hardware?"hardware":"WARP");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"instance_snapshot_view_test: %s\n",error.what());return 1;}
