#include "Render/SourceBindingAssembly.hpp"
#include "Render/StereoGeometryPass.hpp"
#include "Render/NativeIndirectPolicy.hpp"
#include "Render/CommandResources.hpp"
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

namespace cvr::stereo::native_probe {bool ObserveBindings(){return true;}}

using Microsoft::WRL::ComPtr;
using namespace cvr::stereo;
namespace assembly=cvr::stereo::source_binding_assembly;
namespace audit=cvr::stereo::descriptors;
static void Require(bool value,const char* message,const std::source_location at=std::source_location::current()){
    if(!value)throw std::runtime_error(std::string(message)+" at line "+std::to_string(at.line()));
}
static void Check(HRESULT value,const std::source_location at=std::source_location::current()){
    if(FAILED(value))throw std::runtime_error("HRESULT "+std::to_string(uint32_t(value))+" at line "+std::to_string(at.line()));
}
class Owner final:public IUnknown {
    std::atomic<ULONG> refs_{1};std::atomic<UINT>& destroyed_;std::vector<ComPtr<IUnknown>> children_;
public:
    Owner(std::atomic<UINT>& destroyed,std::initializer_list<IUnknown*> children):destroyed_(destroyed){for(auto* child:children)children_.emplace_back(child);}
    ~Owner(){++destroyed_;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;
        if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs_;if(!n)delete this;return n;}
};
static ComPtr<IUnknown> Lease(std::atomic<UINT>& count,std::initializer_list<IUnknown*> children){
    ComPtr<IUnknown> owner;owner.Attach(new Owner(count,children));return owner;
}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,const void* data=nullptr){
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&resource)));
    if(data){void* mapped{};const D3D12_RANGE none{};Check(resource->Map(0,&none,&mapped));Require(mapped!=nullptr,"UPLOAD map failed");
        std::memcpy(mapped,data,SIZE_T(bytes));const D3D12_RANGE written{0,SIZE_T(bytes)};resource->Unmap(0,&written);}
    return resource;
}
static geometry::BufferLease BufferLease(ID3D12Resource* resource){geometry::BufferLease result;result.resource=resource;
    result.gpu=resource->GetGPUVirtualAddress();result.bytes=resource->GetDesc().Width;
    D3D12_HEAP_PROPERTIES heap{};D3D12_HEAP_FLAGS flags{};Check(resource->GetHeapProperties(&heap,&flags));result.heapType=heap.Type;return result;
}
static void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&barrier);
}
static ComPtr<ID3DBlob> Shader(const char* source,const char* entry,const char* target){ComPtr<ID3DBlob> code,error;
    const auto hr=D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,entry,target,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error);
    if(FAILED(hr) && error)std::fprintf(stderr,"%s\n",static_cast<const char*>(error->GetBufferPointer()));Check(hr);return code;
}
struct Gpu {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12InfoQueue> info;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
    HANDLE event{};UINT64 value{};
    Gpu(){ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
        ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG,IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter1> adapter;Check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));
        DXGI_ADAPTER_DESC1 desc{};Check(adapter->GetDesc1(&desc));Require(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE),"High-performance hardware adapter required");
        Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));Check(device.As(&info));
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
        Check(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)));Check(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event failed");
    }
    ~Gpu(){if(event)CloseHandle(event);}
    void Execute(){Check(list->Close());ID3D12CommandList* lists[]{list.Get()};auto resources=cvr::gpu::PrepareCommandResources(1,lists);
        queue->ExecuteCommandLists(1,lists);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(resources));Check(queue->Signal(fence.Get(),++value));
        Check(fence->SetEventOnCompletion(value,event));Require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"GPU completion timeout");
        Require(fence->GetCompletedValue()>=value && fence->GetCompletedValue()!=UINT64_MAX,"Actual fence did not prove GPU completion");
        cvr::gpu::CollectCommandResources();cvr::gpu::ResetCommandResources(list.Get());
    }
    void Begin(){Check(allocator->Reset());Check(list->Reset(allocator.Get(),nullptr));cvr::gpu::ResetCommandResources(list.Get());}
    void CleanMessages(){for(UINT64 n=0;n<info->GetNumStoredMessages();++n){SIZE_T bytes{};Check(info->GetMessage(n,nullptr,&bytes));std::vector<uint8_t> storage(bytes);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());Check(info->GetMessage(n,message,&bytes));
            if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("D3D12 consumer validation error");}}
    }
};
struct Targets {
    std::array<ComPtr<ID3D12Resource>,3> colors,readbacks;ComPtr<ID3D12DescriptorHeap> rtvs;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE,3> views{};std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,3> layouts{};
    explicit Targets(ID3D12Device* device){D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,3,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
        Check(device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&rtvs)));const auto stride=device->GetDescriptorHandleIncrementSize(heapDesc.Type);
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=desc.Height=4;
        desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R32G32B32A32_UINT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        for(UINT n=0;n<colors.size();++n){Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&colors[n])));
            views[n]={rtvs->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(n)*stride};device->CreateRenderTargetView(colors[n].Get(),nullptr,views[n]);
            UINT64 bytes{};device->GetCopyableFootprints(&desc,0,1,0,&layouts[n],nullptr,nullptr,&bytes);
            readbacks[n]=Buffer(device,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);}
    }
    void Copy(ID3D12GraphicsCommandList* list){for(UINT n=0;n<colors.size();++n){Transition(list,colors[n].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=readbacks[n].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=layouts[n];
            from.pResource=colors[n].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
            Transition(list,colors[n].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);}
    }
    void CheckPixels(UINT target,UINT value,UINT marker){void* mapped{};const auto& layout=layouts[target];
        const D3D12_RANGE range{0,SIZE_T(readbacks[target]->GetDesc().Width)};Check(readbacks[target]->Map(0,&range,&mapped));Require(mapped!=nullptr,"Readback map failed");
        bool valid=true;for(UINT y=0;y<4;++y)for(UINT x=0;x<4;++x){std::array<UINT,4> actual{};
            std::memcpy(actual.data(),static_cast<const uint8_t*>(mapped)+layout.Offset+SIZE_T(y)*layout.Footprint.RowPitch+x*16,16);
            valid&=actual==std::array<UINT,4>{value,marker,43,47};}
        const D3D12_RANGE none{};readbacks[target]->Unmap(0,&none);Require(valid,"Exact integer render output differed");
    }
};
static bool BindTarget(void* user,ID3D12GraphicsCommandList* list,UINT source){auto& targets=*static_cast<Targets*>(user);if(source>=2)return false;
    Require(cvr::gpu::KeepCommandResources(list,{targets.rtvs.Get(),targets.colors[source].Get(),targets.readbacks[source].Get()}),"Target retention failed");
    const FLOAT clear[4]{};list->ClearRenderTargetView(targets.views[source],clear,0,nullptr);list->OMSetRenderTargets(1,&targets.views[source],FALSE,nullptr);return true;
}
int main()try{
    // This fixture supplies its own resources, scopes and completed-copy contracts.
    // It does not assert captured native-source admission or a full-scene route.
    Gpu gpu;Targets targets(gpu.device.Get());
    constexpr const char* source=R"(
cbuffer Data : register(b0) { uint4 values; }
struct Output { float4 position : SV_Position; nointerpolation uint marker : TEXCOORD0; };
Output VS(uint vertex : SV_VertexID, uint marker : INSTANCE) {
    Output o; float2 p=float2((vertex << 1)&2,vertex&2);o.position=float4(p*float2(2,-2)+float2(-1,1),0,1);o.marker=marker;return o;
}
uint4 PS(Output input) : SV_Target { return uint4(values.x,input.marker,values.z,values.w); }
)";
    auto vs=Shader(source,"VS","vs_5_0"),ps=Shader(source,"PS","ps_5_0");ComPtr<ID3D12ShaderReflection> reflected;
    Check(D3DReflect(ps->GetBufferPointer(),ps->GetBufferSize(),IID_PPV_ARGS(&reflected)));D3D12_SHADER_BUFFER_DESC bufferDesc{};
    Check(reflected->GetConstantBufferByName("Data")->GetDesc(&bufferDesc));Require(bufferDesc.Size==16,"PS b0 reflection was not exactly16 logical bytes");
    D3D12_DESCRIPTOR_RANGE1 range{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,0,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};D3D12_ROOT_PARAMETER1 parameter{};
    parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameter.DescriptorTable={1,&range};parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC1 rootDesc{1,&parameter,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned{};versioned.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;versioned.Desc_1_1=rootDesc;
    ComPtr<ID3DBlob> serialized,errors;Check(D3D12SerializeVersionedRootSignature(&versioned,&serialized,&errors));ComPtr<ID3D12RootSignature> root;
    Check(gpu.device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root)));
    const D3D12_INPUT_ELEMENT_DESC element{"INSTANCE",0,DXGI_FORMAT_R32_UINT,7,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};pipelineDesc.pRootSignature=root.Get();pipelineDesc.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
    pipelineDesc.PS={ps->GetBufferPointer(),ps->GetBufferSize()};pipelineDesc.InputLayout={&element,1};pipelineDesc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineDesc.SampleMask=UINT_MAX;pipelineDesc.SampleDesc.Count=1;pipelineDesc.NumRenderTargets=1;pipelineDesc.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_UINT;
    pipelineDesc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pipelineDesc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pipelineDesc.RasterizerState.DepthClipEnable=TRUE;
    auto& blend=pipelineDesc.BlendState.RenderTarget[0];blend.RenderTargetWriteMask=15;blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;
    blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;
    pipelineDesc.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;pipelineDesc.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
    pipelineDesc.DepthStencilState.FrontFace=pipelineDesc.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    ComPtr<ID3D12PipelineState> pipeline;Check(gpu.device->CreateGraphicsPipelineState(&pipelineDesc,IID_PPV_ARGS(&pipeline)));
    std::array<uint8_t,256> originalData{};const std::array<UINT,4> originalValues{9,41,43,47};std::memcpy(originalData.data(),originalValues.data(),16);
    auto original=Buffer(gpu.device.Get(),256,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,originalData.data());
    D3D12_DESCRIPTOR_HEAP_DESC tableHeapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};ComPtr<ID3D12DescriptorHeap> originalHeap,originalGpuHeap;
    Check(gpu.device->CreateDescriptorHeap(&tableHeapDesc,IID_PPV_ARGS(&originalHeap)));const D3D12_CONSTANT_BUFFER_VIEW_DESC originalView{original->GetGPUVirtualAddress(),256};
    gpu.device->CreateConstantBufferView(&originalView,originalHeap->GetCPUDescriptorHandleForHeapStart());
    tableHeapDesc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;Check(gpu.device->CreateDescriptorHeap(&tableHeapDesc,IID_PPV_ARGS(&originalGpuHeap)));
    gpu.device->CopyDescriptorsSimple(1,originalGpuHeap->GetCPUDescriptorHandleForHeapStart(),originalHeap->GetCPUDescriptorHandleForHeapStart(),tableHeapDesc.Type);
    const std::array<uint16_t,3> indexData{0,1,2};auto index=Buffer(gpu.device.Get(),sizeof(indexData),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,indexData.data());
    std::array<uint8_t,16*48> instanceData{};for(UINT n=0;n<16;++n){const UINT marker=900+n;std::memcpy(instanceData.data()+n*48,&marker,4);}
    auto instances=Buffer(gpu.device.Get(),instanceData.size(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,instanceData.data());
    const D3D12_DRAW_INDEXED_ARGUMENTS indirectData{3,1,0,0,11};auto arguments=Buffer(gpu.device.Get(),sizeof(indirectData),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,&indirectData);
    const D3D12_INDIRECT_ARGUMENT_DESC indirectArgument{D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED};
    const D3D12_COMMAND_SIGNATURE_DESC signatureDesc{sizeof(indirectData),1,&indirectArgument,0};ComPtr<ID3D12CommandSignature> signature;
    Check(gpu.device->CreateCommandSignature(&signatureDesc,nullptr,IID_PPV_ARGS(&signature)));indirect_policy::Created(signature.Get(),signatureDesc);
    Require(indirect_policy::DrawKind(signature.Get())==1,"Actual created DRAW_INDEXED signature was not classified");
    std::array<ComPtr<ID3D12Resource>,2> privateBuffers;for(auto& buffer:privateBuffers)buffer=Buffer(gpu.device.Get(),256,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    std::atomic<UINT> originalOwners{},copyOwners{},privateOwners{};audit::OwnedSourceTable finite;
    finite.parameter=0;finite.count=1;finite.cpuHeap=originalHeap;finite.owner=Lease(originalOwners,{original.Get(),originalHeap.Get(),originalGpuHeap.Get()});finite.identity=1;finite.version=1;
    finite.sourceGpu=originalGpuHeap->GetGPUDescriptorHandleForHeapStart().ptr;finite.sourceHeap=reinterpret_cast<uintptr_t>(originalGpuHeap.Get());
    const families::ResourceUse use{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_PIXEL,0,0,1,16};
    for(UINT round=0;round<2;++round){if(round)gpu.Begin();
        std::array<ComPtr<ID3D12Resource>,2> staging;
        const std::array<UINT,2> values{round?29u:17u,29u};
        for(UINT n=0;n<2;++n){std::array<uint8_t,16> data{};const std::array<UINT,4> words{values[n],41,43,47};std::memcpy(data.data(),words.data(),16);
            staging[n]=Buffer(gpu.device.Get(),16,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,data.data());
            if(round)Transition(gpu.list.Get(),privateBuffers[n].Get(),D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,D3D12_RESOURCE_STATE_COPY_DEST);
            gpu.list->CopyBufferRegion(privateBuffers[n].Get(),0,staging[n].Get(),0,16);
            Transition(gpu.list.Get(),privateBuffers[n].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            Require(cvr::gpu::KeepCommandResources(gpu.list.Get(),{staging[n].Get(),privateBuffers[n].Get()}),"Private copy lifetime failed");}
        const auto copiedList=reinterpret_cast<uintptr_t>(gpu.list.Get());gpu.Execute();gpu.Begin();
        std::array<source_cbv_snapshot::PrivateCbv,2> cbvs;std::array<source_cbv_snapshot::PreparedCopies,2> copies;
        std::array<assembly::Plan,2> plans;std::array<GeometrySourcePassDraw,2> draws;
        assembly::Failure failure;auto cache=std::make_unique<assembly::CpuTableCache>();std::vector<std::vector<SharedBankTable>> tables;
        for(UINT n=0;n<2;++n){const assembly::Scope scope{reinterpret_cast<uintptr_t>(root.Get()),copiedList,round+1,n+1,n+1,n};
            auto& cbv=cbvs[n];cbv.parameter=cbv.offset=0;cbv.reflectedBytes=cbv.copiedBytes=16;cbv.paddedBytes=256;
            cbv.resource=privateBuffers[n];cbv.owner=Lease(privateOwners,{privateBuffers[n].Get()});cbv.view={privateBuffers[n]->GetGPUVirtualAddress(),256};
            copies[n].owner=Lease(copyOwners,{staging[n].Get(),privateBuffers[n].Get()});copies[n].list=scope.list;copies[n].generation=scope.generation;
            copies[n].occurrence=scope.occurrence;copies[n].count=1;copies[n].copiedBytes=16;
            const assembly::FiniteTable table{scope,&finite};const assembly::PrivateCbv owned{scope,&cbv,&copies[n],true};assembly::Input input;
            input.scope=scope;input.root=&rootDesc;input.shaderUses={&use,1};input.finite={&table,1};input.privateCbvs={&owned,1};
            Require(assembly::Build(input,plans[n],failure) && plans[n].Ready(),"Completed private fixture CBV was not assembly-ready");}
        const std::array<const assembly::Plan*,2> planPointers{&plans[0],&plans[1]};Check(assembly::AssemblePass(gpu.device.Get(),planPointers,*cache,tables,failure));
        Require(tables.size()==2 && tables[0][0].cpuHeap!=tables[1][0].cpuHeap,"Different exact private CBV views were merged");
        for(UINT n=0;n<2;++n){auto& sourceDraw=draws[n];auto& geometry=sourceDraw.geometry.geometry;auto& draw=geometry.draw;
            geometry.index=BufferLease(index.Get());geometry.vertices[7]=BufferLease(instances.Get());geometry.state.original=pipeline;
            geometry.state.topology=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;geometry.state.rate.known=true;geometry.state.rate.value=D3D12_SHADING_RATE_1X1;
            draw.frame=round+1;draw.side=n;draw.pipeline=reinterpret_cast<uintptr_t>(pipeline.Get());draw.root=reinterpret_cast<uintptr_t>(root.Get());
            draw.flags=176;draw.inputSlotMask=128;draw.indices=3;draw.instances=1;draw.firstInstance=7;
            draw.index={index->GetGPUVirtualAddress(),sizeof(indexData),DXGI_FORMAT_R16_UINT};draw.vertices[7]={instances->GetGPUVirtualAddress(),UINT(instanceData.size()),48};
            draw.rtCount=1;draw.viewport={0,0,4,4,0,1};draw.scissor={0,0,4,4};
            sourceDraw.binding={root,{static_cast<const uint8_t*>(serialized->GetBufferPointer()),serialized->GetBufferSize()},tables[n]};
            sourceDraw.contract.targets.NumRenderTargets=1;sourceDraw.contract.targets.RTFormats[0]=DXGI_FORMAT_R32G32B32A32_UINT;
            sourceDraw.contract.samples.Count=1;sourceDraw.contract.topology=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;sourceDraw.contract.fixedRasterState=true;
            if(n)sourceDraw.geometry.indirect=PureIndirectSource{signature,1,arguments,0,nullptr,0};}
        auto pass=std::make_unique<StereoGeometryPass>();Check(pass->CreateSources(gpu.device.Get(),draws));
        Require(pass->SourceCount()==2 && !pass->InstanceBytes(),"Mixed consumer changed/rebased source instances");
        Require(pass->RecordSource(gpu.list.Get(),0,BindTarget,&targets) && pass->RecordSource(gpu.list.Get(),1,BindTarget,&targets) &&
            pass->SourceRecordedCount()==2,"Mixed direct/indirect source emission failed");
        SharedBankTable originalTable;originalTable.parameter=0;originalTable.count=1;originalTable.cpuHeap=originalHeap;originalTable.leases={original,finite.owner};
        const SourceBankDraw originalBinding{root,{static_cast<const uint8_t*>(serialized->GetBufferPointer()),serialized->GetBufferSize()},{&originalTable,1}};
        StereoBindingBank control;Check(control.CreateSourcePass(gpu.device.Get(),{&originalBinding,1}));
        Require(control.BindSource(gpu.list.Get()),"Original descriptor control bind failed");gpu.list->SetPipelineState(pipeline.Get());
        const FLOAT clear[4]{};gpu.list->ClearRenderTargetView(targets.views[2],clear,0,nullptr);gpu.list->OMSetRenderTargets(1,&targets.views[2],FALSE,nullptr);
        gpu.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);gpu.list->IASetIndexBuffer(&draws[0].geometry.geometry.draw.index);
        gpu.list->IASetVertexBuffers(7,1,&draws[0].geometry.geometry.draw.vertices[7]);gpu.list->DrawIndexedInstanced(3,1,0,0,7);
        Require(cvr::gpu::KeepCommandResources(gpu.list.Get(),{pipeline.Get(),root.Get(),index.Get(),instances.Get(),arguments.Get(),signature.Get(),
            targets.rtvs.Get(),targets.colors[2].Get(),targets.readbacks[2].Get()}),"Explicit fixture consumer leases failed");targets.Copy(gpu.list.Get());
        const auto copiesBefore=copyOwners.load(),privateBefore=privateOwners.load();
        pass.reset();draws={};plans={};tables.clear();cache.reset();cbvs={};copies={};staging={};
        Require(copyOwners==copiesBefore && privateOwners==privateBefore,"Recorded pass dropped fixture copy/allocation owners before submission");
        gpu.Execute();Require(copyOwners==copiesBefore+2 && privateOwners==privateBefore+2,"Actual fence/reset did not retire fixture copy/allocation owners");
        targets.CheckPixels(0,values[0],907);targets.CheckPixels(1,29,911);targets.CheckPixels(2,9,907);
        void* mapped{};const D3D12_RANGE read{0,256};Check(original->Map(0,&read,&mapped));const bool unchanged=std::memcmp(mapped,originalData.data(),originalData.size())==0;
        const D3D12_RANGE none{};original->Unmap(0,&none);Require(unchanged && originalView.BufferLocation==original->GetGPUVirtualAddress() && originalView.SizeInBytes==256,
            "Private-only mutation changed the original CBV resource/view");gpu.CleanMessages();}
    std::puts("source_assembly_geometry_test: PASS hardware GPU Build/AssemblePass/CreateSources/RecordSource, exact private17/29 vs original9, slot7 firstInstance7/11 and completed owning copies; no native-source admission or full-scene claim");
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"source_assembly_geometry_test: %s\n",error.what());return 1;}
