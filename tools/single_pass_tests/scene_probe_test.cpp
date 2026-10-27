#include "Render/StereoGpuProbe.hpp"
#include "Render/NativeStereoCameraUpload.hpp"
#include "Render/ViewInstancedPipeline.hpp"
#include "Render/CommandResources.hpp"
#include "Render/StereoSceneState.hpp"
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace cvr::stereo::camera_upload {Result CurrentBinding(){return {0x1000,0x2000,2048,0};}}
namespace cvr::stereo::native_probe {bool Enabled(){return true;}bool ObserveBindings(){return false;}bool CurrentState(ID3D12GraphicsCommandList*,DrawRecord&){return false;}}
extern "C" {__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_NativeStereoProbeState{1};}
static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("D3D12 API failure "+std::to_string(unsigned(hr)));}
static void Require(bool v,const char* text){if(!v)throw std::runtime_error(text);}
static std::vector<char> Read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);Require(bool(f),"shader missing");return {std::istreambuf_iterator<char>(f),{}};}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* d,UINT64 size,D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES h{};h.Type=type;D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=size;b.Height=1;b.DepthOrArraySize=b.MipLevels=1;b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;Check(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&b,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)));return r;
}
int wmain(int argc,wchar_t** argv) try {
    Require(argc==2 || argc==3,"scene_probe_test <compiled shader directory> [--native-reference|--cross-queue|--group|--group-mismatch]");const std::filesystem::path dir=argv[1];
    const bool crossQueue=argc==3 && std::wstring(argv[2])==L"--cross-queue";
    const bool fineRate=argc==3 && std::wstring(argv[2])==L"--scene-fine-rate";
    const bool depthBatches=argc==3 && std::wstring(argv[2])==L"--depth-batches";
    const bool depthLater=argc==3 && std::wstring(argv[2])==L"--depth-later-run";
    const bool depthReorder=depthLater || (argc==3 && std::wstring(argv[2])==L"--depth-anchor-reorder");
    const bool depthAnchor=depthReorder || (argc==3 && std::wstring(argv[2])==L"--depth-anchor");
    const bool depthOnly=depthAnchor || depthBatches || (argc==3 && std::wstring(argv[2])==L"--depth-prepass");
    const UINT instanceCount=depthBatches?3:1;
    const bool directTimeout=argc==3 && std::wstring(argv[2])==L"--scene-direct-timeout";
    const bool mixed=argc==3 && std::wstring(argv[2])==L"--scene-mixed";
    const bool direct=depthOnly || mixed || directTimeout || (argc==3 && std::wstring(argv[2])==L"--scene-direct");
    const bool routeTimeout=directTimeout || (argc==3 && std::wstring(argv[2])==L"--scene-route-timeout");
    const bool route=direct || routeTimeout || (argc==3 && std::wstring(argv[2])==L"--scene-route");
    const bool scene=fineRate || route || (argc==3 && std::wstring(argv[2])==L"--scene-depth");
    const bool mismatch=argc==3 && std::wstring(argv[2])==L"--group-mismatch";
    const bool reuse=argc==3 && std::wstring(argv[2])==L"--group-reuse";
    const bool visibility=argc==3 && std::wstring(argv[2])==L"--group-visibility";
    const bool pair=argc==3 && std::wstring(argv[2])==L"--group-pair";
    const bool lateOnly=argc==3 && std::wstring(argv[2])==L"--late-visibility";
    const bool lateFallback=argc==3 && std::wstring(argv[2])==L"--late-fallback";
    const bool lateTimeout=routeTimeout || (argc==3 && std::wstring(argv[2])==L"--late-timeout");
    const bool lateMaterial=argc==3 && std::wstring(argv[2])==L"--late-material-mismatch";
    const bool late=scene || lateOnly || lateFallback || lateTimeout || lateMaterial;
    const bool timeoutGate=lateTimeout || (argc==3 && std::wstring(argv[2])==L"--group-gate-timeout");
    const bool gate=late || timeoutGate || (argc==3 && std::wstring(argv[2])==L"--group-gate");
    const bool group=gate || mismatch || reuse || visibility || pair || (argc==3 && std::wstring(argv[2])==L"--group");
    const bool nativeReference=group || crossQueue || (argc==3 && std::wstring(argv[2])==L"--native-reference");
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;Check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device2> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
    D3D12_ROOT_PARAMETER camera{};camera.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;camera.Constants.Num32BitValues=2;
    D3D12_DESCRIPTOR_RANGE cameraRange{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,0,0,0};
    if(scene){camera.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;camera.DescriptorTable={1,&cameraRange};}
    D3D12_DESCRIPTOR_RANGE unusedPixel{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,31,0,0};
    D3D12_ROOT_PARAMETER graphicsParams[2]{camera,{}};
    graphicsParams[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;graphicsParams[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    graphicsParams[1].DescriptorTable={1,&unusedPixel};
    // Depth-only native PSOs do not initialize the pixel-stage descriptor table.
    // Subsequent draws and compute dispatches must still survive our resolve.
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};rootDesc.NumParameters=depthOnly?2:1;rootDesc.pParameters=graphicsParams;rootDesc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;ComPtr<ID3DBlob> rs,error;
    Check(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&rs,&error));ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0,rs->GetBufferPointer(),rs->GetBufferSize(),IID_PPV_ARGS(&root)));
    cvr::stereo::scene_state::RootCreated(root.Get(),rs->GetBufferPointer(),rs->GetBufferSize());
    auto vs=Read(dir/L"scene-probe-vs.dxil"),ps=Read(dir/L"scene-probe-ps.dxil");D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature=root.Get();pd.VS={vs.data(),vs.size()};pd.PS={ps.data(),ps.size()};pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets=3;pd.RTVFormats[0]=pd.RTVFormats[1]=DXGI_FORMAT_R10G10B10A2_UNORM;pd.RTVFormats[2]=DXGI_FORMAT_R8G8B8A8_UNORM;pd.DSVFormat=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;pd.SampleDesc={1,0};pd.SampleMask=UINT_MAX;
    pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
    for(auto& b:pd.BlendState.RenderTarget){b.SrcBlend=b.SrcBlendAlpha=D3D12_BLEND_ONE;b.DestBlend=b.DestBlendAlpha=D3D12_BLEND_ZERO;b.BlendOp=b.BlendOpAlpha=D3D12_BLEND_OP_ADD;b.LogicOp=D3D12_LOGIC_OP_NOOP;b.RenderTargetWriteMask=15;}
    pd.DepthStencilState.DepthEnable=TRUE;pd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    pd.DepthStencilState.FrontFace=pd.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    if(scene){pd.DepthStencilState.StencilEnable=TRUE;pd.DepthStencilState.StencilReadMask=pd.DepthStencilState.StencilWriteMask=255;
        pd.DepthStencilState.FrontFace.StencilPassOp=pd.DepthStencilState.BackFace.StencilPassOp=D3D12_STENCIL_OP_REPLACE;}
    if(depthOnly){pd.PS={};pd.NumRenderTargets=0;for(auto& f:pd.RTVFormats)f=DXGI_FORMAT_UNKNOWN;}
    ComPtr<ID3D12PipelineState> mono,stereo;Check(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&mono)));cvr::stereo::ViewInstancedLayout layout;layout.allowMasking=true;Check(cvr::stereo::CreateViewInstancedPipeline(device.Get(),pd,layout,&stereo));
    cvr::stereo::scene_state::PipelineCreated(mono.Get(),scene);
    ComPtr<ID3D12PipelineState> alternateMono,alternateStereo;
    if(mixed){auto other=pd;other.RasterizerState.CullMode=D3D12_CULL_MODE_BACK;other.RasterizerState.FrontCounterClockwise=TRUE;
        Check(device->CreateGraphicsPipelineState(&other,IID_PPV_ARGS(&alternateMono)));Check(cvr::stereo::CreateViewInstancedPipeline(device.Get(),other,layout,&alternateStereo));
        cvr::stereo::scene_state::PipelineCreated(alternateMono.Get(),true);}
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));ComPtr<ID3D12CommandAllocator> allocator;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),mono.Get(),IID_PPV_ARGS(&list)));
    auto indices=Buffer(device.Get(),7212*2,D3D12_HEAP_TYPE_UPLOAD);void* mapped{};D3D12_RANGE none{};Check(indices->Map(0,&none,&mapped));for(UINT i=0;i<7212;++i)static_cast<uint16_t*>(mapped)[i]=uint16_t(i%3);indices->Unmap(0,nullptr);
    ComPtr<ID3D12Resource> parameters;ComPtr<ID3D12DescriptorHeap> parameterHeap;
    if(scene){parameters=Buffer(device.Get(),2048,D3D12_HEAP_TYPE_UPLOAD);Check(parameters->Map(0,&none,&mapped));std::memset(mapped,0,2048);
        for(UINT eye=0;eye<2;++eye)for(UINT object=0;object<4;++object){auto* data=reinterpret_cast<UINT*>(static_cast<char*>(mapped)+(eye*4+object)*256);data[0]=eye;data[1]=object;}parameters->Unmap(0,nullptr);
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,9,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&parameterHeap)));
        auto h=parameterHeap->GetCPUDescriptorHandleForHeapStart();for(UINT i=0;i<8;++i){D3D12_CONSTANT_BUFFER_VIEW_DESC cb{parameters->GetGPUVirtualAddress()+i*256,256};device->CreateConstantBufferView(&cb,h);h.ptr+=device->GetDescriptorHandleIncrementSize(hd.Type);}}
    auto setObject=[&](ID3D12GraphicsCommandList* l,UINT eye,UINT object){if(scene){auto h=parameterHeap->GetGPUDescriptorHandleForHeapStart();h.ptr+=(eye*4+object)*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);l->SetGraphicsRootDescriptorTable(0,h);cvr::stereo::scene_state::Table(l,0,h);}else {l->SetGraphicsRoot32BitConstant(0,eye,0);l->SetGraphicsRoot32BitConstant(0,object,1);}};
    auto setRoot=[&](ID3D12GraphicsCommandList* l){l->SetGraphicsRootSignature(root.Get());l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);if(scene){
        cvr::stereo::scene_state::Reset(l,true);cvr::stereo::scene_state::Root(l,root.Get());auto* h=parameterHeap.Get();l->SetDescriptorHeaps(1,&h);cvr::stereo::scene_state::Heaps(l,1,&h);cvr::stereo::scene_state::Topology(l,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);l->OMSetStencilRef(0);}};
    ComPtr<ID3D12RootSignature> computeRoot;ComPtr<ID3D12PipelineState> computePipeline;ComPtr<ID3D12Resource> computeOutput,computeReadback;
    if(scene) {
        D3D12_DESCRIPTOR_RANGE ranges[]{{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0}};D3D12_ROOT_PARAMETER params[2]{};
        for(UINT i=0;i<2;++i){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&ranges[i]};}
        D3D12_ROOT_SIGNATURE_DESC d{2,params,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> blob,cs;
        Check(D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));Check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&computeRoot)));
        cvr::stereo::scene_state::RootCreated(computeRoot.Get(),blob->GetBufferPointer(),blob->GetBufferSize());
        const char source[]="cbuffer C:register(b0){uint eye;uint object;}RWByteAddressBuffer O:register(u0);[numthreads(1,1,1)]void CS(){O.Store(0,(eye+1)*100+object);}";
        Check(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"CS","cs_5_0",0,0,&cs,&error));D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=computeRoot.Get();pd.CS={cs->GetBufferPointer(),cs->GetBufferSize()};Check(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&computePipeline)));
        D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=256;b.Height=b.DepthOrArraySize=b.MipLevels=b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;b.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES mem{};mem.Type=D3D12_HEAP_TYPE_DEFAULT;Check(device->CreateCommittedResource(&mem,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&computeOutput)));
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=DXGI_FORMAT_R32_TYPELESS;u.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=64;u.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
        auto h=parameterHeap->GetCPUDescriptorHandleForHeapStart();h.ptr+=8*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);device->CreateUnorderedAccessView(computeOutput.Get(),nullptr,&u,h);computeReadback=Buffer(device.Get(),256,D3D12_HEAP_TYPE_READBACK);
    }
    auto setCompute=[&](ID3D12GraphicsCommandList* l,UINT eye){if(!scene)return;l->SetComputeRootSignature(computeRoot.Get());cvr::stereo::scene_state::ComputeRoot(l,computeRoot.Get());
        for(UINT i=0;i<2;++i){auto h=parameterHeap->GetGPUDescriptorHandleForHeapStart();h.ptr+=(i?8:eye*4+3)*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);l->SetComputeRootDescriptorTable(i,h);cvr::stereo::scene_state::ComputeTable(l,i,h);}};
    auto dispatchCompute=[&](ID3D12GraphicsCommandList* l,UINT eye){if(!scene)return;
        // No root/table/heap rebinding: resolve must preserve existing compute state.
        l->SetPipelineState(computePipeline.Get());l->Dispatch(1,1,1);D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={computeOutput.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);l->CopyBufferRegion(computeReadback.Get(),eye*4,computeOutput.Get(),0,4);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);l->ResourceBarrier(1,&b);};
    const D3D12_INDEX_BUFFER_VIEW ib{indices->GetGPUVirtualAddress(),7212*2,DXGI_FORMAT_R16_UINT};list->IASetIndexBuffer(&ib);setRoot(list.Get());setObject(list.Get(),0,0);
    setCompute(list.Get(),0);
    ComPtr<ID3D12DescriptorHeap> rtv,dsv;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=6;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv)));hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=2;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsv)));
    auto rh=[&](UINT i){auto h=rtv->GetCPUDescriptorHandleForHeapStart();h.ptr+=i*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);return h;};
    auto dh=[&](UINT i){auto h=dsv->GetCPUDescriptorHandleForHeapStart();h.ptr+=i*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);return h;};
    ComPtr<ID3D12Resource> colors[2][3],depths[2];D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=desc.Height=160;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;
    const float zero[4]{};
    for(UINT mode=0;mode<2;++mode) {
        for(UINT t=0;t<3;++t){desc.Format=t<2?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&colors[mode][t])));device->CreateRenderTargetView(colors[mode][t].Get(),nullptr,rh(mode*3+t));list->ClearRenderTargetView(rh(mode*3+t),zero,0,nullptr);}
        desc.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&depths[mode])));device->CreateDepthStencilView(depths[mode].Get(),nullptr,dh(mode));list->ClearDepthStencilView(dh(mode),D3D12_CLEAR_FLAG_DEPTH,0,0,0,nullptr);
    }
    if(scene)for(UINT mode=0;mode<2;++mode){for(UINT t=0;t<3;++t)cvr::stereo::scene_state::RtvCreated(colors[mode][t].Get(),nullptr,rh(mode*3+t));cvr::stereo::scene_state::DsvCreated(depths[mode].Get(),nullptr,dh(mode));
        list->ClearDepthStencilView(dh(mode),D3D12_CLEAR_FLAGS(3),0,13,0,nullptr);const D3D12_RECT r{20,0,75,160};list->ClearDepthStencilView(dh(mode),D3D12_CLEAR_FLAGS(3),.9f,47,1,&r);}
    const D3D12_VIEWPORT viewport=scene?D3D12_VIEWPORT{0,0,160,160,0,1}:D3D12_VIEWPORT{9,11,83,57,0,1};const D3D12_RECT scissor=scene?D3D12_RECT{0,0,160,160}:D3D12_RECT{10,12,80,50};list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);
    D3D12_CPU_DESCRIPTOR_HANDLE refTargets[]{rh(0),rh(1),rh(2)};const auto refDepth=dh(0);list->OMSetRenderTargets(depthOnly?0:3,refTargets,FALSE,&refDepth);list->DrawIndexedInstanced(7212,instanceCount,0,0,0);
    if(route){for(UINT object=1;object<3;++object){list->SetPipelineState(mixed && object==1?alternateMono.Get():mono.Get());setObject(list.Get(),0,object);list->DrawIndexedInstanced(7212,instanceCount,0,0,0);}setObject(list.Get(),0,0);}
    D3D12_CPU_DESCRIPTOR_HANDLE actualTargets[]{rh(3),rh(4),rh(5)};const auto actualDepth=dh(1);list->OMSetRenderTargets(depthOnly?0:3,actualTargets,FALSE,&actualDepth);
    cvr::stereo::native_probe::DrawRecord draw;draw.side=1;draw.indices=7212;draw.instances=instanceCount;draw.rtCount=3;draw.flags=63;draw.viewport=viewport;draw.scissor=scissor;draw.pipeline=reinterpret_cast<uintptr_t>(mono.Get());draw.depth=actualDepth.ptr;for(UINT i=0;i<3;++i)draw.targets[i]=actualTargets[i].ptr;
    draw.frame=1;draw.instanceDataBytes=64;draw.index=ib;
    draw.root=reinterpret_cast<uintptr_t>(root.Get());
    if(depthOnly){draw.rtCount=0;draw.targets={};}CyberpunkVR_StereoGpuProbeDepthPrepass=depthOnly;
    CyberpunkVR_StereoGpuProbeStartIndices=depthAnchor && !depthLater?7212:0;
    CyberpunkVR_StereoGpuProbeStartRun=depthLater?2:0;
    CyberpunkVR_StereoGpuProbeSceneDepth=scene;
    CyberpunkVR_StereoGpuProbeSceneRoute=direct?2:route?1:0;
    CyberpunkVR_StereoGpuProbeTiming=scene;
    CyberpunkVR_StereoGpuProbeMixedMaterials=mixed;
    CyberpunkVR_StereoGpuProbeFineRate=fineRate;
    UINT sequence{};
    auto submitProbe=[&](ID3D12GraphicsCommandList* l,cvr::stereo::native_probe::DrawRecord& d){d.sequence=++sequence;
        auto* p=mixed && d.instanceData[0]==1?alternateMono.Get():mono.Get();auto* v=mixed && d.instanceData[0]==1?alternateStereo.Get():stereo.Get();d.pipeline=reinterpret_cast<uintptr_t>(p);l->SetPipelineState(p);
        if(mixed || depthOnly)cvr::stereo::gpu_probe::Boundary(l,d,true,v);
        std::vector<uint8_t> batch;
        if(depthBatches){d.vertices[7].StrideInBytes=48;batch.resize(d.instances*48);for(size_t i=0;i<batch.size();++i)batch[i]=uint8_t(i+d.instanceData[0]);}
        cvr::stereo::gpu_probe::Draw(l,v,d,batch);
        if(route && !cvr::stereo::gpu_probe::RouteNative(l,d))l->DrawIndexedInstanced(d.indices,d.instances,d.firstIndex,d.baseVertex,d.firstInstance);};
    CyberpunkVR_StereoGpuProbeNativeReference=nativeReference;
    CyberpunkVR_StereoGpuProbeGroup=group;
    CyberpunkVR_StereoGpuProbeMainViewMask=visibility?3:pair?2:UINT_MAX;
    CyberpunkVR_StereoGpuProbeMainReferenceMask=pair?2:UINT_MAX;
    CyberpunkVR_StereoGpuProbePrepareGateMs=gate?40:0;
    CyberpunkVR_StereoGpuProbeLateVisibility=late;
    const auto sourceGroup=group?cvr::stereo::gpu_probe::BeginGroup(1,6):cvr::stereo::gpu_probe::GroupContext{};
    CyberpunkVR_StereoGpuProbeRequest=1;
    if(depthLater){auto earlier=draw;earlier.instanceData[0]=99;setObject(list.Get(),0,3);submitProbe(list.Get(),earlier);
        Require(CyberpunkVR_StereoGpuProbeState==0,"Wrong source run selected");
        auto gap=draw;gap.pipeline^=8;cvr::stereo::gpu_probe::Boundary(list.Get(),gap,true,nullptr);setObject(list.Get(),0,0);}
    submitProbe(list.Get(),draw);Require(CyberpunkVR_StereoGpuProbeState==1,"Probe did not record");
    if(group){for(UINT object=1;object<3;++object){draw.instanceData[0]=uint8_t(object);setObject(list.Get(),0,object);submitProbe(list.Get(),draw);}
        if(scene)cvr::stereo::gpu_probe::Boundary(list.Get(),draw,false);
        cvr::stereo::gpu_probe::EndGroup(sourceGroup);setObject(list.Get(),0,0);}
    draw.instanceData[0]=0;
    // No state rebinding here: this draw verifies the probe restored everything.
    list->DrawIndexedInstanced(7212,instanceCount,0,0,0);
    dispatchCompute(list.Get(),0);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[2]{};UINT64 total{};UINT rowCount{};UINT64 rowBytes{};
    desc=colors[0][0]->GetDesc();device->GetCopyableFootprints(&desc,0,1,0,&footprints[0],&rowCount,&rowBytes,&total);footprints[1]=footprints[0];footprints[1].Offset=(total+511)&~UINT64(511);auto readback=Buffer(device.Get(),footprints[1].Offset+total,D3D12_HEAP_TYPE_READBACK);
    for(UINT mode=0;mode<2;++mode){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={colors[mode][0].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);
        D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=colors[mode][0].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprints[mode];list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
    ComPtr<ID3D12Fence> producerGate;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&producerGate)));
    // Hold the producer on GPU only, so MAIN must record and submit while the
    // producer is unfinished. Release after MAIN submission; no timing race.
    if(nativeReference)Check(queue->Wait(producerGate.Get(),1));
    Check(list->Close());ID3D12CommandList* lists[]{list.Get()};auto resources=cvr::gpu::PrepareCommandResources(1,lists);cvr::stereo::gpu_probe::BeforeSubmit(queue.Get(),1,lists);queue->ExecuteCommandLists(1,lists);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(resources));cvr::stereo::gpu_probe::Submitted(queue.Get(),1,lists);
    ComPtr<ID3D12CommandAllocator> mainAllocator;ComPtr<ID3D12GraphicsCommandList> mainList;
    ComPtr<ID3D12CommandQueue> mainQueue=queue;if(crossQueue)Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&mainQueue)));
    if(nativeReference) {
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&mainAllocator)));
        if(reuse){mainList=list;Check(mainList->Reset(mainAllocator.Get(),mono.Get()));}
        else Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,mainAllocator.Get(),mono.Get(),IID_PPV_ARGS(&mainList)));
        setRoot(mainList.Get());setObject(mainList.Get(),1,0);mainList->IASetIndexBuffer(&ib);
        if(fineRate){ComPtr<ID3D12GraphicsCommandList5> l5;Check(mainList.As(&l5));const D3D12_SHADING_RATE_COMBINER c[]{D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,D3D12_SHADING_RATE_COMBINER_PASSTHROUGH};
            l5->RSSetShadingRate(D3D12_SHADING_RATE_2X2,c);cvr::stereo::scene_state::ShadingRate(mainList.Get(),D3D12_SHADING_RATE_2X2,c);}
        setCompute(mainList.Get(),1);
        mainList->RSSetViewports(1,&viewport);mainList->RSSetScissorRects(1,&scissor);
        D3D12_RESOURCE_BARRIER restore{};restore.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;restore.Transition={colors[1][0].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET};mainList->ResourceBarrier(1,&restore);
        mainList->OMSetRenderTargets(depthOnly?0:3,actualTargets,FALSE,&actualDepth);auto mainDraw=draw;mainDraw.side=0;
        if(scene){for(auto h:actualTargets)mainList->ClearRenderTargetView(h,zero,0,nullptr);mainList->ClearDepthStencilView(actualDepth,D3D12_CLEAR_FLAGS(3),.45f,31,0,nullptr);
            const D3D12_RECT r{65,0,115,160};mainList->ClearDepthStencilView(actualDepth,D3D12_CLEAR_FLAGS(3),.9f,73,1,&r);}
        if(timeoutGate)std::this_thread::sleep_for(std::chrono::milliseconds(65));
        const auto mainGroup=group?cvr::stereo::gpu_probe::BeginGroup(0,6):cvr::stereo::gpu_probe::GroupContext{};
        if(depthAnchor){auto earlier=mainDraw;earlier.instanceData[0]=99;setObject(mainList.Get(),1,3);submitProbe(mainList.Get(),earlier);
            Require(CyberpunkVR_StereoGpuProbeMainDraws==0 && CyberpunkVR_StereoGpuProbeSceneRouted[1]==0,"Earlier MAIN geometry was mistaken for the requested prefix");setObject(mainList.Get(),1,0);}
        if(depthReorder){mainDraw.instanceData[0]=1;setObject(mainList.Get(),1,1);}
        submitProbe(mainList.Get(),mainDraw);
        if(group){for(UINT object=1;object<(mismatch?4u:(visibility||lateOnly)?2u:3u);++object){
                const auto rendered=depthReorder && object==1?0:(lateFallback||lateMaterial) && object==2?3:object;
                mainDraw.instanceData[0]=uint8_t(lateMaterial?object:rendered);
                setObject(mainList.Get(),1,rendered);submitProbe(mainList.Get(),mainDraw);}
            if(scene)cvr::stereo::gpu_probe::Boundary(mainList.Get(),mainDraw,false);
            cvr::stereo::gpu_probe::EndGroup(mainGroup);}
        if(fineRate){
            // First draw inherits VRS restored by the private comparison. The
            // reference then explicitly sets coarse rate; both must be equal.
            D3D12_RESOURCE_BARRIER restoreRef{};restoreRef.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            restoreRef.Transition={colors[0][0].Get(),0,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET};mainList->ResourceBarrier(1,&restoreRef);
            for(UINT mode=1;;--mode){const D3D12_CPU_DESCRIPTOR_HANDLE ts[]{rh(mode*3),rh(mode*3+1),rh(mode*3+2)};const auto d=dh(mode);
                if(!mode){ComPtr<ID3D12GraphicsCommandList5> l5;Check(mainList.As(&l5));l5->RSSetShadingRate(D3D12_SHADING_RATE_2X2,nullptr);}
                for(auto t:ts)mainList->ClearRenderTargetView(t,zero,0,nullptr);mainList->ClearDepthStencilView(d,D3D12_CLEAR_FLAGS(3),0,0,0,nullptr);
                mainList->OMSetRenderTargets(3,ts,FALSE,&d);mainList->SetPipelineState(mono.Get());mainList->DrawIndexedInstanced(7212,instanceCount,0,0,0);
                D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={colors[mode][0].Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
                mainList->ResourceBarrier(1,&b);
                D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=colors[mode][0].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprints[mode];
                mainList->CopyTextureRegion(&dst,0,0,0,&src,nullptr);if(!mode)break;}
        }
        dispatchCompute(mainList.Get(),1);
        Check(mainList->Close());
        ID3D12CommandList* mainLists[]{mainList.Get()};auto mainResources=cvr::gpu::PrepareCommandResources(1,mainLists);
        cvr::stereo::gpu_probe::BeforeSubmit(mainQueue.Get(),1,mainLists);mainQueue->ExecuteCommandLists(1,mainLists);cvr::gpu::SubmitCommandResources(mainQueue.Get(),std::move(mainResources));cvr::stereo::gpu_probe::Submitted(mainQueue.Get(),1,mainLists);
        Check(producerGate->Signal(1));
        Require(CyberpunkVR_StereoGpuProbeQueueWaits==(crossQueue?1:0),"GPU dependency must wait only across queues");
    }
    if(lateTimeout)CyberpunkVR_NativeStereoProbeState=0;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while((CyberpunkVR_StereoGpuProbeState==1 || CyberpunkVR_StereoGpuProbeState==2) && std::chrono::steady_clock::now()<deadline){cvr::stereo::gpu_probe::Poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    Require(CyberpunkVR_StereoGpuProbeState==(lateTimeout?6u:3u),"GPU probe did not finish with the expected state");
    Require((mismatch||lateMaterial)?CyberpunkVR_StereoGpuProbeDifferentBytes>0:CyberpunkVR_StereoGpuProbeDifferentBytes==0,"Unexpected common/native group comparison result");
    if(group && !lateTimeout)Require(CyberpunkVR_StereoGpuProbeSourceDraws==3 && CyberpunkVR_StereoGpuProbeMainDraws==(mismatch?4u:(visibility||lateOnly)?2u:3u),"Group did not include every native material draw");
    if(late && !lateTimeout)Require(CyberpunkVR_StereoGpuProbeMatchedDraws==((scene||lateMaterial)?3u:2u) && CyberpunkVR_StereoGpuProbeFallbackDraws==(lateFallback?1u:0u),"Late matching did not preserve shared/eye-only draws");
    if(scene && !lateTimeout)Require(CyberpunkVR_StereoGpuProbeScenePhase[0]==2 && CyberpunkVR_StereoGpuProbeScenePhase[1]==2 && CyberpunkVR_StereoGpuProbeSceneReject==0 && CyberpunkVR_StereoGpuProbeSceneSamples[0]>0 && CyberpunkVR_StereoGpuProbeSceneSamples[1]>0,"Scene prefix did not complete or had no passing samples");
    if(route)Require(CyberpunkVR_StereoGpuProbeSceneCommitted==(routeTimeout?0u:1u) && CyberpunkVR_StereoGpuProbeSceneRouted[0]==3 && (routeTimeout || CyberpunkVR_StereoGpuProbeSceneRouted[1]==3),"Native draws did not use the committed common group/fallback");
    if(scene && !lateTimeout){Require(CyberpunkVR_StereoGpuProbeTimingDrops==0 && CyberpunkVR_StereoGpuProbeFrequency[0]>0 && CyberpunkVR_StereoGpuProbeFrequency[1]>0,"GPU timers incomplete");
        Require(CyberpunkVR_StereoGpuProbeTimedCount[0][0]==3 && CyberpunkVR_StereoGpuProbeTimedCount[0][1]==3 && CyberpunkVR_StereoGpuProbeTimedCount[1][0]==3,"GPU draw timers missed draws");
        Require(CyberpunkVR_StereoGpuProbeTimedCount[2][0]==1 && CyberpunkVR_StereoGpuProbeTimedCount[2][1]==1 && CyberpunkVR_StereoGpuProbeTimedCount[3][0]==1 && CyberpunkVR_StereoGpuProbeTimedCount[3][1]==1,"GPU import/resolve timers incomplete");
        if(route)Require(CyberpunkVR_StereoGpuProbeTimedCount[4][0]==(direct?0u:1u) && CyberpunkVR_StereoGpuProbeTimedCount[4][1]==(direct?0u:1u),"GPU copy-back timers incomplete");}
    Require(CyberpunkVR_StereoGpuProbeEyeDifferences[0]==0 && CyberpunkVR_StereoGpuProbeEyeDifferences[1]==CyberpunkVR_StereoGpuProbeDifferentBytes,"Per-eye differences disagree");
    if(mismatch||lateMaterial)Require(CyberpunkVR_StereoGpuProbeDifferenceCount>0 && CyberpunkVR_StereoGpuProbeDifferences[0].eye==1,"Missing differing pixel samples");
    if(gate)Require(CyberpunkVR_StereoGpuProbePrepareGateReason==(timeoutGate?2u:1u) &&
        CyberpunkVR_StereoGpuProbePrepareGateStart>0 && CyberpunkVR_StereoGpuProbePrepareGateMain>0 && CyberpunkVR_StereoGpuProbePrepareGateRelease>0,
        "Preparation gate did not release through the expected MAIN/watchdog path");
    D3D12_RANGE range{0,SIZE_T(footprints[1].Offset+total)};Check(readback->Map(0,&range,&mapped));size_t different{},covered{};
    for(UINT row=0;row<rowCount;++row){const auto* a=static_cast<const uint8_t*>(mapped)+footprints[0].Offset+row*footprints[0].Footprint.RowPitch;const auto* b=static_cast<const uint8_t*>(mapped)+footprints[1].Offset+row*footprints[1].Footprint.RowPitch;
        for(UINT64 x=0;x<rowBytes;++x){different+=a[x]!=b[x];covered+=a[x]!=0;}}
    readback->Unmap(0,&none);Require((covered>0 || depthOnly) && different==0,"Native PSO/targets/viewport/scissor state was not restored");
    if(scene){D3D12_RANGE r{0,8};Check(computeReadback->Map(0,&r,&mapped));const auto* values=static_cast<const UINT*>(mapped);Require(values[0]==103 && values[1]==203,"Compute table bindings were not restored");computeReadback->Unmap(0,&none);}
    ComPtr<ID3D12InfoQueue> info;Check(device.As(&info));for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size{};info->GetMessage(i,nullptr,&size);std::vector<char> bytes(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());info->GetMessage(i,m,&size);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",m->pDescription);throw std::runtime_error("D3D12 validation error");}}
    cvr::gpu::CollectCommandResources();std::printf("PASS native GPU probe: state %u, %llu bytes compared, %llu different (expected mismatch %d), coverage %llu/%llu; native state restored, no validation errors\n",CyberpunkVR_StereoGpuProbeState.load(),CyberpunkVR_StereoGpuProbeComparedBytes,CyberpunkVR_StereoGpuProbeDifferentBytes,mismatch||lateMaterial,CyberpunkVR_StereoGpuProbeCoverage[0],CyberpunkVR_StereoGpuProbeCoverage[1]);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
