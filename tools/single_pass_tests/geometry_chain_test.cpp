#include "Render/StereoGeometryChain.hpp"
#include "Render/CommandResources.hpp"
#include "Render/NativeIndirectPolicy.hpp"
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <vector>

using namespace cvr::stereo;
using Microsoft::WRL::ComPtr;
namespace cvr::stereo::native_probe {bool Enabled(){return false;}bool ObserveBindings(){return true;}}
extern "C" {std::atomic<uint32_t> CyberpunkVR_NativeStereoProbeState{1};}
static void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static void Check(HRESULT hr,std::source_location at=std::source_location::current()){
    if(FAILED(hr)){std::fprintf(stderr,"HRESULT %08X at %u\n",UINT(hr),at.line());throw std::runtime_error("D3D12 failure");}}
static void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&barrier);
}
int main(int argc,char** argv)try{
    const bool hardware=argc==2 && std::strcmp(argv[1],"--hardware")==0;
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    Check(hardware?factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)):
        factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> messages;Check(device.As(&messages));
    D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameter.Constants={0,0,4};
    const D3D12_ROOT_SIGNATURE_DESC rootDesc{1,&parameter,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> blob,error;Check(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));
    ComPtr<ID3D12RootSignature> root;Check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    constexpr char shader[]=R"(
cbuffer C:register(b0){float Shift;float Depth;float Phase;float Eye;};
float4 VS(uint id:SV_VertexID):SV_Position {
    float2 p=id==0?float2(-.8,-.7):id==1?float2(0,.8):float2(.7,-.7);
    return float4(p+float2(Shift,0),Depth,1);
}
struct Three {float4 a:SV_Target0;float4 b:SV_Target1;float4 c:SV_Target2;};
struct Four {float4 a:SV_Target0;float4 b:SV_Target1;float4 c:SV_Target2;float2 d:SV_Target3;};
Three PS3(){Three o;o.a=float4(.15+Eye*.4,Phase*.15,.3,1);o.b=float4(.6,.2+Eye*.3,.1+Phase*.1,1);o.c=float4(.3,Phase*.2,.2+Eye*.5,1);return o;}
Four PS4(){Three t=PS3();Four o;o.a=t.a;o.b=t.b;o.c=t.c;o.d=float2(Shift,Depth);return o;}
)";
    ComPtr<ID3DBlob> vs,ps3,ps4;
    Check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&error));
    Check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PS3","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps3,&error));
    Check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PS4","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps4,&error));
    constexpr std::array<DXGI_FORMAT,5> formats{DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_D32_FLOAT_S8X24_UINT};
    std::array<ComPtr<ID3D12PipelineState>,3> pipelines;
    for(UINT i=0;i<3;++i){D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
        const auto ps=i==1?ps3:ps4;if(i)pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};pd.NumRenderTargets=i?i+2:0;
        for(UINT slot=0;slot<pd.NumRenderTargets;++slot)pd.RTVFormats[slot]=formats[slot];pd.DSVFormat=formats[4];pd.SampleDesc.Count=1;pd.SampleMask=UINT_MAX;
        pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode=D3D12_CULL_MODE_BACK;pd.RasterizerState.DepthClipEnable=TRUE;
        for(auto& blend:pd.BlendState.RenderTarget){blend.RenderTargetWriteMask=15;blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;
            blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;}
        pd.DepthStencilState.DepthEnable=TRUE;pd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
        pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_GREATER_EQUAL;pd.DepthStencilState.StencilEnable=TRUE;
        pd.DepthStencilState.StencilReadMask=pd.DepthStencilState.StencilWriteMask=255;
        pd.DepthStencilState.FrontFace=pd.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_REPLACE,D3D12_COMPARISON_FUNC_ALWAYS};
        Check(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pipelines[i])));
    }
    ComPtr<ID3D12DescriptorHeap> rtvs,dsvs;D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,16,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
    Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs)));hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=4;
    Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsvs)));
    const auto rtStep=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV),dsStep=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    auto rtv=[&](UINT target,UINT slot){return D3D12_CPU_DESCRIPTOR_HANDLE{rtvs->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(target*4+slot)*rtStep};};
    auto dsv=[&](UINT target){return D3D12_CPU_DESCRIPTOR_HANDLE{dsvs->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(target)*dsStep};};
    constexpr UINT Width=67,Height=39;
    std::array<std::array<ComPtr<ID3D12Resource>,5>,4> targets;
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    for(UINT target=0;target<4;++target)for(UINT slot=0;slot<5;++slot){
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=Width;desc.Height=Height;
        desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=formats[slot];
        desc.Flags=slot==4?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL:D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,slot==4?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&targets[target][slot])));
        if(slot==4){device->CreateDepthStencilView(targets[target][slot].Get(),nullptr,dsv(target));scene_state::DsvCreated(targets[target][slot].Get(),nullptr,dsv(target));}
        else {device->CreateRenderTargetView(targets[target][slot].Get(),nullptr,rtv(target,slot));scene_state::RtvCreated(targets[target][slot].Get(),nullptr,rtv(target,slot));}
    }
    auto chain=std::make_unique<StereoGeometryChain>();Check(chain->Create(device.Get(),Width,Height));
    Require(chain->Create(device.Get(),Width,Height)==E_INVALIDARG,"Duplicate create accepted");
    std::array<ComPtr<ID3D12CommandAllocator>,9> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>,9> lists;
    for(UINT i=0;i<9;++i){Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocators[i])));
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocators[i].Get(),nullptr,IID_PPV_ARGS(&lists[i])));}
    D3D12_INDIRECT_ARGUMENT_DESC argument{};argument.Type=D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    const D3D12_COMMAND_SIGNATURE_DESC signatureDesc{32,1,&argument,0};ComPtr<ID3D12CommandSignature> signature;
    Check(device->CreateCommandSignature(&signatureDesc,nullptr,IID_PPV_ARGS(&signature)));indirect_policy::Created(signature.Get(),signatureDesc);
    Require(indirect_policy::DrawIndexedOnly(signature.Get()) && indirect_policy::DrawIndexedStride(signature.Get())==32,"Indirect signature proof missing");
    D3D12_INDIRECT_ARGUMENT_DESC vertexArgument{};vertexArgument.Type=D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    const D3D12_COMMAND_SIGNATURE_DESC vertexSignatureDesc{32,1,&vertexArgument,0};ComPtr<ID3D12CommandSignature> vertexSignature;
    Check(device->CreateCommandSignature(&vertexSignatureDesc,nullptr,IID_PPV_ARGS(&vertexSignature)));indirect_policy::Created(vertexSignature.Get(),vertexSignatureDesc);
    Require(indirect_policy::DrawKind(vertexSignature.Get())==2 && indirect_policy::PureDrawStride(vertexSignature.Get())==32 &&
        !indirect_policy::DrawIndexedOnly(vertexSignature.Get()),"Non-indexed signature proof missing");
    auto upload=[&](const void* data,UINT bytes){D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
        desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;ComPtr<ID3D12Resource> result;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&result)));
        void* mapped{};Check(result->Map(0,nullptr,&mapped));std::memcpy(mapped,data,bytes);result->Unmap(0,nullptr);return result;};
    const uint16_t indexData[]{0,1,2};const std::array<UINT,8> drawArguments{3,1,0,0,0,0xdeadbeef,0xdeadbeef,0xdeadbeef};
    auto indexBuffer=upload(indexData,sizeof(indexData)),argumentBuffer=upload(drawArguments.data(),sizeof(drawArguments));
    const D3D12_INDEX_BUFFER_VIEW indexView{indexBuffer->GetGPUVirtualAddress(),sizeof(indexData),DXGI_FORMAT_R16_UINT};
    for(UINT eye=0;eye<2;++eye)for(UINT stage=0;stage<4;++stage){
        auto* list=lists[eye*4+stage].Get();const UINT colors=stage==0?0:stage==2?4:3;const auto depth=dsv(eye);
        list->SetGraphicsRootSignature(root.Get());list->SetPipelineState(pipelines[!colors?0:colors==3?1:2].Get());
        const D3D12_VIEWPORT viewport{0,0,float(Width),float(Height),0,1};const D3D12_RECT scissor{0,0,Width,Height};
        list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const float constants[]{-.12f+eye*.2f+stage*.02f,.2f+stage*.18f,float(stage),float(eye)};
        list->SetGraphicsRoot32BitConstants(0,4,constants,0);list->OMSetStencilRef(17+eye*31+stage);
        list->IASetIndexBuffer(&indexView);
        const auto emit=[&]{if(stage>=2)list->ExecuteIndirect(stage==2?signature.Get():vertexSignature.Get(),1,argumentBuffer.Get(),0,nullptr,0);else list->DrawInstanced(3,1,0,0);};
        if(!stage)for(UINT target:{eye,eye+2}){
            const float color[]{.04f+.03f*eye,.1f,.2f,.5f};for(UINT slot=0;slot<4;++slot)list->ClearRenderTargetView(rtv(target,slot),color,0,nullptr);
            list->ClearDepthStencilView(dsv(target),D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,0,UINT8(eye+3),0,nullptr);
        }
        const auto reference=rtv(eye,0);list->OMSetRenderTargets(colors,colors?&reference:nullptr,TRUE,&depth);emit();
        native_probe::DrawRecord draw{};draw.rtCount=colors;draw.rtContiguous=TRUE;draw.targets[0]=rtv(eye+2,0).ptr;draw.depth=dsv(eye+2).ptr;draw.viewport=viewport;
        scene_state::GeometryTargets capture;Require(scene_state::CaptureGeometryTargets(draw,capture),"Capture targets failed");
        const auto before=chain->Counts();auto invalid=capture;invalid.views[4].viewFlags=1;
        Require(chain->PrepareEye(list,eye,invalid)==E_INVALIDARG && chain->Counts()==before,"Read-only native state admitted");
        Check(chain->PrepareEye(list,eye,capture));Check(chain->PrepareEye(list,eye,capture));
        if(!stage)Require(!chain->Bind(list,1u<<eye,4),"Uninitialized color targets bound");
        Require(!chain->Bind(list,0,colors) && !chain->Bind(list,1u<<eye,2),"Invalid target mask admitted");
        Require(chain->Bind(list,1u<<eye,colors),"Layer bind failed");emit();
        Require(chain->RestoreNative(list,eye,colors),"Native OM restore failed");
        const auto stats=chain->Counts();Require(stats[eye]==(stage==0?1u:stage==1?4u:5u),"Target was reimported between passes");
        if(stage==3){Require(chain->ExportEye(list,eye),"Export failed");
            Require(!chain->ExportEye(list,eye) && !chain->Bind(list,1u<<eye,3) && !chain->Bind(list,3,3),"Sealed eye reopened");
            Require(chain->PrepareEye(list,eye,capture)==E_INVALIDARG,"Sealed eye imported again");}
        Check(list->Close());
    }
    Require(chain->Counts()==std::array<uint64_t,7>{5,5,4,4,0,5,5},"Unexpected import/bind/export counts");
    struct Copy {ID3D12Resource* resource{};UINT sub{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};UINT rows{};UINT64 rowBytes{};};
    {StereoGeometryChain isolated;Check(isolated.Create(device.Get(),Width,Height));auto* commands=lists.back().Get();
        Require(isolated.ClearPrivateEye(commands,0,3,0,0) && isolated.ClearPrivateEye(commands,1,3,1,0) && isolated.Bind(commands,3,3),"Private target initialization failed");
        Require(isolated.SeedPrivateDepth(commands,0,targets[0][4].Get()) && isolated.SeedPrivateDepth(commands,1,targets[1][4].Get()),
            "Owned depth/stencil checkpoint did not initialize private layers");
        Require(!isolated.ClearPrivateEye(commands,0,3,0,0) && !isolated.RestoreNative(commands,0,3) && !isolated.ExportEye(commands,0),
            "Isolated targets acquired a native attachment or were initialized twice");}
    std::array<scene_state::GeometryTargets,2> baselines;
    for(UINT eye=0;eye<2;++eye){native_probe::DrawRecord draw{};draw.rtCount=3;draw.rtContiguous=TRUE;
        draw.targets[0]=rtv(eye,0).ptr;draw.depth=dsv(eye).ptr;draw.viewport={0,0,float(Width),float(Height),0,1};
        Require(scene_state::CaptureGeometryTargets(draw,baselines[eye]),"Full baseline target capture failed");}
    std::array<std::unique_ptr<StereoGeometryChain>,3> seeded;
    ComPtr<ID3D12QueryHeap> pipelineQueries,occlusionQueries;
    D3D12_QUERY_HEAP_DESC queryDesc{D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS,3,0};
    Check(device->CreateQueryHeap(&queryDesc,IID_PPV_ARGS(&pipelineQueries)));queryDesc.Type=D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    Check(device->CreateQueryHeap(&queryDesc,IID_PPV_ARGS(&occlusionQueries)));
    const uint64_t deny{};auto rejectedPredicate=upload(&deny,sizeof(deny));auto* seededList=lists.back().Get();
    for(UINT mode=0;mode<seeded.size();++mode){auto& output=seeded[mode];output=std::make_unique<StereoGeometryChain>();Check(output->Create(device.Get(),Width,Height));
        for(UINT eye=0;eye<2;++eye){const auto counts=output->Counts();auto invalid=baselines[eye];invalid.colors=0;
            Require(!output->SeedPrivateEye(seededList,eye,invalid) && output->Counts()==counts,"Depth-only baseline silently supplied zero colors");
            invalid=baselines[eye];invalid.resources[1].Reset();
            Require(!output->SeedPrivateEye(seededList,eye,invalid) && output->Counts()==counts,"Incomplete baseline recorded a partial seed");
            invalid=baselines[eye];invalid.views[4].viewFlags=D3D12_DSV_FLAG_READ_ONLY_STENCIL;
            Require(!output->SeedPrivateEye(seededList,eye,invalid) && output->Counts()==counts,"Read-only stencil baseline was admitted");
            Require(output->SeedPrivateEye(seededList,eye,baselines[eye]),"Full color/depth/stencil seed failed");
            Require(!output->SeedPrivateEye(seededList,eye,baselines[eye]) && !output->RestoreNative(seededList,eye,3) &&
                !output->ExportEye(seededList,eye),"Private baseline gained native attachment permissions");}
        seededList->SetGraphicsRootSignature(root.Get());seededList->SetPipelineState(pipelines[1].Get());
        seededList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const D3D12_VIEWPORT viewport{0,0,float(Width),float(Height),0,1};const D3D12_RECT scissor{0,0,Width,Height};
        seededList->RSSetViewports(1,&viewport);seededList->RSSetScissorRects(1,&scissor);
        seededList->BeginQuery(pipelineQueries.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS,mode);
        seededList->BeginQuery(occlusionQueries.Get(),D3D12_QUERY_TYPE_OCCLUSION,mode);
        seededList->SetPredication(mode==2?rejectedPredicate.Get():nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
        for(UINT eye=0;eye<2;++eye){Require(output->Bind(seededList,1u<<eye,3),"Seeded private target bind failed");
            const float constants[]{.05f+eye*.1f,.96f,4.f,float(eye)};seededList->SetGraphicsRoot32BitConstants(0,4,constants,0);
            seededList->OMSetStencilRef(180+eye);seededList->DrawInstanced(3,1,0,0);}
        seededList->SetPredication(nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
        seededList->EndQuery(occlusionQueries.Get(),D3D12_QUERY_TYPE_OCCLUSION,mode);
        seededList->EndQuery(pipelineQueries.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS,mode);
    }
    std::vector<Copy> copies;UINT64 total{};
    for(UINT eye=0;eye<2;++eye)for(UINT slot=0;slot<5;++slot)for(UINT plane=0;plane<(slot==4?2u:1u);++plane)for(UINT mode=0;mode<2;++mode){
        const auto target=eye+mode*2;const auto desc=targets[target][slot]->GetDesc();Copy copy{targets[target][slot].Get(),plane};UINT64 bytes{};
        device->GetCopyableFootprints(&desc,plane,1,total,&copy.layout,&copy.rows,&copy.rowBytes,&bytes);total+=bytes;copies.push_back(copy);
    }
    struct BaselineCopy {UINT eye{},slot{},plane{};std::array<Copy,4> versions;};std::vector<BaselineCopy> baselineCopies;
    for(UINT eye=0;eye<2;++eye)for(UINT slot:{0u,1u,2u,4u})for(UINT plane=0;plane<(slot==4?2u:1u);++plane){BaselineCopy comparison{eye,slot,plane};
        for(UINT mode=0;mode<comparison.versions.size();++mode){auto& copy=comparison.versions[mode];
            copy.resource=mode?seeded[mode-1]->Resource(slot):targets[eye][slot].Get();copy.sub=mode?eye+plane*2:plane;
            const auto desc=copy.resource->GetDesc();UINT64 bytes{};device->GetCopyableFootprints(&desc,copy.sub,1,total,&copy.layout,&copy.rows,&copy.rowBytes,&bytes);
            total+=bytes;Require(copy.rows==Height && copy.rowBytes%Width==0,"Baseline footprint shape changed");
            if(mode)Require(copy.rows==comparison.versions[0].rows && copy.rowBytes==comparison.versions[0].rowBytes,"Baseline footprint layouts disagree");}
        baselineCopies.push_back(comparison);
    }
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;
    buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hp.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> readback;
    Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    constexpr UINT QueryStride=sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS)+sizeof(uint64_t);
    static_assert(QueryStride==12*sizeof(uint64_t));auto queryBuffer=buffer;queryBuffer.Width=3*QueryStride;ComPtr<ID3D12Resource> queryReadback;
    Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&queryBuffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&queryReadback)));
    auto* last=lists.back().Get();
    for(UINT target=0;target<4;++target)for(UINT slot=0;slot<5;++slot)Transition(last,targets[target][slot].Get(),slot==4?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    for(const auto& output:seeded)for(UINT slot:{0u,1u,2u,4u})Transition(last,output->Resource(slot),
        slot==4?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    for(const auto& copy:copies){D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=copy.resource;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=copy.sub;
        to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=copy.layout;last->CopyTextureRegion(&to,0,0,0,&from,nullptr);}
    for(const auto& comparison:baselineCopies)for(const auto& copy:comparison.versions){D3D12_TEXTURE_COPY_LOCATION from{},to{};
        from.pResource=copy.resource;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=copy.sub;
        to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=copy.layout;last->CopyTextureRegion(&to,0,0,0,&from,nullptr);}
    for(UINT mode=0;mode<seeded.size();++mode){last->ResolveQueryData(pipelineQueries.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS,mode,1,queryReadback.Get(),mode*QueryStride);
        last->ResolveQueryData(occlusionQueries.Get(),D3D12_QUERY_TYPE_OCCLUSION,mode,1,queryReadback.Get(),mode*QueryStride+sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));}
    Check(last->Close());std::array<ID3D12CommandList*,9> commands;for(UINT i=0;i<9;++i)commands[i]=lists[i].Get();
    auto keep=cvr::gpu::PrepareCommandResources(UINT(commands.size()),commands.data());chain.reset();for(auto& output:seeded)output.reset();
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    // Separate Execute calls establish ordering between the recorded stages.
    for(auto* command:commands)queue->ExecuteCommandLists(1,&command);
    cvr::gpu::SubmitCommandResources(queue.Get(),std::move(keep));
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));Check(queue->Signal(fence.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event");Check(fence->SetEventOnCompletion(1,event));
    const auto waited=WaitForSingleObject(event,5000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"GPU timeout");
    void* mapped{};Check(readback->Map(0,nullptr,&mapped));UINT64 compared{},nonzero{};std::array<UINT64,2> writtenDepth{};
    for(size_t i=0;i<copies.size();i+=2){const auto& a=copies[i];const auto& b=copies[i+1];
        for(UINT row=0;row<a.rows;++row){const auto* expected=static_cast<const uint8_t*>(mapped)+a.layout.Offset+SIZE_T(row)*a.layout.Footprint.RowPitch;
            const auto* actual=static_cast<const uint8_t*>(mapped)+b.layout.Offset+SIZE_T(row)*b.layout.Footprint.RowPitch;
            Require(!std::memcmp(expected,actual,SIZE_T(a.rowBytes)),"Retained chain differs from native targets");
            if((i/2)%6==4)for(UINT x=0;x<Width;++x){float depth{};std::memcpy(&depth,expected+x*(a.rowBytes/Width),4);writtenDepth[i/12]+=depth>0;}
            for(UINT64 byte=0;byte<a.rowBytes;++byte)nonzero+=expected[byte]!=0;compared+=a.rowBytes;}
    }
    std::array<std::array<uint64_t,3>,2> deltas{};uint64_t baselineNonzero{};
    for(const auto& comparison:baselineCopies){const auto stride=comparison.versions[0].rowBytes/Width;
        const UINT bytes=comparison.slot==4?(comparison.plane?1u:4u):4u;
        const UINT component=comparison.slot==4 && comparison.plane && stride==8?4u:0u;
        Require(component+bytes<=stride,"Baseline plane component exceeds its pixel");
        for(UINT y=0;y<Height;++y)for(UINT x=0;x<Width;++x){std::array<const uint8_t*,4> pixels{};
            for(UINT mode=0;mode<pixels.size();++mode){const auto& copy=comparison.versions[mode];pixels[mode]=static_cast<const uint8_t*>(mapped)+
                copy.layout.Offset+SIZE_T(y)*copy.layout.Footprint.RowPitch+SIZE_T(x)*stride+component;}
            Require(!std::memcmp(pixels[1],pixels[2],bytes),"Reference and optimized seeded color/depth/stencil differ");
            Require(!std::memcmp(pixels[0],pixels[3],bytes),"Rejected geometry changed the nonzero baseline");
            deltas[comparison.eye][comparison.slot==4?comparison.plane+1:0]+=std::memcmp(pixels[0],pixels[1],bytes)!=0;
            if(comparison.slot<3){uint32_t before{};std::memcpy(&before,pixels[0],4);baselineNonzero+=before!=0;}
        }
    }
    const D3D12_RANGE none{};readback->Unmap(0,&none);Require(nonzero>1000 && writtenDepth[0]>100 && writtenDepth[1]>100,"Blank fixture");
    Require(baselineNonzero==uint64_t(2)*3*Width*Height,"Baseline did not contain nonzero color outside the tested geometry");
    for(const auto& eye:deltas)Require(eye[0] && eye[1] && eye[2],"Accepted geometry made no color, depth or stencil contribution");
    std::array<uint64_t,36> executed{};Check(queryReadback->Map(0,nullptr,&mapped));std::memcpy(executed.data(),mapped,sizeof(executed));queryReadback->Unmap(0,&none);
    for(UINT mode=0;mode<2;++mode)Require(executed[mode*12+2] && executed[mode*12+7] && executed[mode*12+11],"Accepted draw had no actual VS/PS/sample execution");
    Require(std::all_of(executed.begin()+24,executed.end(),[](auto value){return value==0;}),"Rejected draw executed the graphics pipeline");
    for(auto& list:lists)cvr::gpu::ResetCommandResources(list.Get());cvr::gpu::CollectCommandResources();
    for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T size{};messages->GetMessage(i,nullptr,&size);std::vector<uint8_t> data(size);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());Check(messages->GetMessage(i,message,&size));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("D3D12 validation error");}}
    std::printf("PASS %s retained chain and full nonzero baseline: identical rendering, rejected color/depth/stencil unchanged, positive/zero GPU execution, %llu exact native bytes\n",hardware?"hardware":"WARP",compared);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
