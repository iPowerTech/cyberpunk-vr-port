#include "Render/ViewInstancedPipeline.hpp"
#include "Render/StereoTargetArray.hpp"
#include "Render/StereoGeometryChain.hpp"
#include "Render/GeometryTargetCheckpoint.hpp"
#include "Render/CommandResources.hpp"
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <vector>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <source_location>
using Microsoft::WRL::ComPtr;
using namespace cvr::stereo;
static void Require(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
static void Check(HRESULT hr,std::source_location at=std::source_location::current()){
    if(FAILED(hr)){std::fprintf(stderr,"HRESULT %08X at line %u\n",UINT(hr),at.line());throw std::runtime_error("D3D12 failure");}}
static std::vector<uint8_t> Read(const char* path){std::ifstream f(path,std::ios::binary);Require(bool(f),"Missing fixture shader");return {std::istreambuf_iterator<char>(f),{}};}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,D3D12_HEAP_TYPE type,UINT64 bytes,const void* data=nullptr){
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES h{};h.Type=type;ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&result)));
    if(data){void* p{};Check(result->Map(0,nullptr,&p));std::memcpy(p,data,SIZE_T(bytes));result->Unmap(0,nullptr);}return result;
}
static void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}
int main(int argc,char** argv) try {
    Require(argc==6 || argc==8 || (argc==9 && !std::strcmp(argv[8],"--checkpoints")),"Expected original VS/PS, camera-only VS/PS, linked main-only PS [original/paired PS3] [--checkpoints]");
    const bool checkpoints=argc==9,chain=argc>=8;const UINT colorCount=chain?4:2;
    auto vs=Read(argv[1]),ps=Read(argv[2]),cameraVs=Read(argv[3]),cameraPs=Read(argv[4]),mainOnlyPs=Read(argv[5]);
    auto ps3=chain?Read(argv[6]):std::vector<uint8_t>{},cameraPs3=chain?Read(argv[7]):std::vector<uint8_t>{};
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    Check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device2> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 caps{};Check(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3,&caps,sizeof(caps)));
    if(!caps.ViewInstancingTier){std::puts("SKIP View Instancing unavailable");return 77;}
    ComPtr<ID3D12InfoQueue> messages;Check(device.As(&messages));
    D3D12_DESCRIPTOR_RANGE ranges[]{{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,1,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,5,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0}};
    std::array<D3D12_ROOT_PARAMETER,3> parameters{};
    for(UINT i=0;i<3;++i){parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[i].DescriptorTable={1,&ranges[i]};parameters[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;}
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxAnisotropy=1;
    sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rd{UINT(parameters.size()),parameters.data(),1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> bytes,error;Check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&bytes,&error));ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0,bytes->GetBufferPointer(),bytes->GetBufferSize(),IID_PPV_ARGS(&root)));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={vs.data(),vs.size()};pd.PS={ps.data(),ps.size()};
    pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.SampleMask=UINT_MAX;pd.SampleDesc.Count=1;pd.NumRenderTargets=colorCount;
    const DXGI_FORMAT formats[]{chain?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM,
        chain?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16_FLOAT};
    std::copy_n(formats,colorCount,pd.RTVFormats);pd.DSVFormat=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
    for(auto& b:pd.BlendState.RenderTarget){b.RenderTargetWriteMask=15;b.SrcBlend=b.SrcBlendAlpha=D3D12_BLEND_ONE;b.DestBlend=b.DestBlendAlpha=D3D12_BLEND_ZERO;
        b.BlendOp=b.BlendOpAlpha=D3D12_BLEND_OP_ADD;b.LogicOp=D3D12_LOGIC_OP_NOOP;}
    auto& ds=pd.DepthStencilState;ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
    ds.StencilEnable=TRUE;ds.StencilReadMask=ds.StencilWriteMask=255;
    ds.FrontFace=ds.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_REPLACE,D3D12_COMPARISON_FUNC_ALWAYS};
    ComPtr<ID3D12PipelineState> original,stereo,wrongPixel,originalDepth,stereoDepth,originalThree,stereoThree;
    ViewInstancedLayout layout;layout.allowMasking=true;
    if(chain){auto depth=pd;depth.PS={};depth.NumRenderTargets=0;std::fill_n(depth.RTVFormats,8,DXGI_FORMAT_UNKNOWN);
        Check(device->CreateGraphicsPipelineState(&depth,IID_PPV_ARGS(&originalDepth)));
        depth.VS={cameraVs.data(),cameraVs.size()};Check(CreateViewInstancedPipeline(device.Get(),depth,layout,&stereoDepth));
        pd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_EQUAL;
        auto three=pd;three.NumRenderTargets=3;three.RTVFormats[3]=DXGI_FORMAT_UNKNOWN;three.PS={ps3.data(),ps3.size()};
        Check(device->CreateGraphicsPipelineState(&three,IID_PPV_ARGS(&originalThree)));
        three.VS={cameraVs.data(),cameraVs.size()};three.PS={cameraPs3.data(),cameraPs3.size()};Check(CreateViewInstancedPipeline(device.Get(),three,layout,&stereoThree));}
    Check(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&original)));
    pd.VS={cameraVs.data(),cameraVs.size()};pd.PS={mainOnlyPs.data(),mainOnlyPs.size()};
    Check(CreateViewInstancedPipeline(device.Get(),pd,layout,&wrongPixel));
    pd.PS={cameraPs.data(),cameraPs.size()};Check(CreateViewInstancedPipeline(device.Get(),pd,layout,&stereo));

    std::array<std::array<float,256>,6> cameras{};std::array<std::array<float,64>,3> materials{};
    for(UINT draw=0;draw<3;++draw){materials[draw][0]=1;materials[draw][1]=.6f+.1f*draw;materials[draw][2]=.7f;materials[draw][3]=float(draw&1);
        for(UINT eye=0;eye<2;++eye){auto& c=cameras[draw*2+eye];
            c[0]=eye?.06f:-.06f;c[1]=draw==1?.35f:draw==2?-.35f:0;c[2]=.7f-.18f*draw;c[3]=.24f;
            for(UINT word=1;word<=2;++word){c[word*4]=eye?.85f:.35f;c[word*4+1]=eye?.3f:.8f;c[word*4+2]=.2f+.15f*word;c[word*4+3]=1;}
            c[208]=eye?.85f:.15f;
        }
    }
    auto cb=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,sizeof(cameras),cameras.data());
    auto material=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,sizeof(materials),materials.data());
    ComPtr<ID3D12DescriptorHeap> heap,rtvs,dsvs;
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,13,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));const auto stride=device->GetDescriptorHandleIncrementSize(hd.Type);
    auto cpu=[&](UINT i){return D3D12_CPU_DESCRIPTOR_HANDLE{heap->GetCPUDescriptorHandleForHeapStart().ptr+UINT64(i)*stride};};
    auto gpu=[&](UINT i){return D3D12_GPU_DESCRIPTOR_HANDLE{heap->GetGPUDescriptorHandleForHeapStart().ptr+UINT64(i)*stride};};
    for(UINT i=0;i<6;++i){const D3D12_CONSTANT_BUFFER_VIEW_DESC d{cb->GetGPUVirtualAddress()+i*1024,1024};device->CreateConstantBufferView(&d,cpu(i));}
    for(UINT i=0;i<3;++i){const D3D12_CONSTANT_BUFFER_VIEW_DESC d{cb->GetGPUVirtualAddress()+i*2048,2048};device->CreateConstantBufferView(&d,cpu(6+i));
        const D3D12_CONSTANT_BUFFER_VIEW_DESC m{material->GetGPUVirtualAddress()+i*256,256};device->CreateConstantBufferView(&m,cpu(9+i));}
    hd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,4*colorCount,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs)));
    hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=4;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsvs)));
    const auto rtStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV),dsStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    auto rtv=[&](UINT target,UINT plane){return D3D12_CPU_DESCRIPTOR_HANDLE{rtvs->GetCPUDescriptorHandleForHeapStart().ptr+(target*colorCount+plane)*rtStride};};
    auto dsv=[&](UINT target){return D3D12_CPU_DESCRIPTOR_HANDLE{dsvs->GetCPUDescriptorHandleForHeapStart().ptr+target*dsStride};};
    std::array<std::array<ComPtr<ID3D12Resource>,4>,4> colors;std::array<ComPtr<ID3D12Resource>,4> depths;
    std::array<std::array<StereoTargetArray,4>,2> colorArrays;std::array<StereoTargetArray,2> depthArrays;
    std::array<GeometryTargetCheckpoint,2> before,after;
    StereoGeometryChain retained;if(chain)Check(retained.Create(device.Get(),128,64));
    D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC image{};image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;image.Width=128;image.Height=64;image.MipLevels=image.SampleDesc.Count=1;
    for(UINT target=0;target<4;++target){image.DepthOrArraySize=target<2?1:2;
        for(UINT plane=0;plane<colorCount;++plane){image.Format=formats[plane];image.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            if(target<2)Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&colors[target][plane])));
            else if(chain && target==2)colors[target][plane]=retained.Resource(plane);
            else{auto native=image;native.DepthOrArraySize=1;Check(colorArrays[target-2][plane].Initialize(device.Get(),native,D3D12_RESOURCE_STATE_RENDER_TARGET));
                colors[target][plane]=colorArrays[target-2][plane].Resource();}
            device->CreateRenderTargetView(colors[target][plane].Get(),nullptr,rtv(target,plane));}
        image.Format=pd.DSVFormat;image.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if(target<2)Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&depths[target])));
        else if(chain && target==2)depths[target]=retained.Resource(4);
        else{auto native=image;native.DepthOrArraySize=1;Check(depthArrays[target-2].Initialize(device.Get(),native,D3D12_RESOURCE_STATE_DEPTH_WRITE));
            depths[target]=depthArrays[target-2].Resource();}
        if(chain && target==2){D3D12_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
            view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY;view.Texture2DArray.ArraySize=2;
            device->CreateDepthStencilView(depths[target].Get(),&view,dsv(target));
        }else device->CreateDepthStencilView(depths[target].Get(),nullptr,dsv(target));
    }
    image.Width=image.Height=2;image.DepthOrArraySize=1;image.Format=DXGI_FORMAT_R8G8B8A8_UNORM;image.Flags=D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> texture;Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture)));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureLayout{};UINT64 textureBytes{};device->GetCopyableFootprints(&image,0,1,0,&textureLayout,nullptr,nullptr,&textureBytes);
    const uint8_t texels[]{255,64,128,255,128,255,64,255,64,128,255,255,255,255,255,255};std::vector<uint8_t> textureData(static_cast<size_t>(textureBytes));
    for(UINT y=0;y<2;++y)std::memcpy(textureData.data()+y*textureLayout.Footprint.RowPitch,texels+y*8,8);
    auto textureUpload=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,textureBytes,textureData.data());
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=image.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    device->CreateShaderResourceView(texture.Get(),&srv,cpu(12));
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12GraphicsCommandList1> views;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));Check(list.As(&views));
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=textureUpload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=textureLayout;
    to.pResource=texture.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    Transition(list.Get(),texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap* heaps[]{heap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetGraphicsRootSignature(root.Get());list->SetGraphicsRootDescriptorTable(2,gpu(12));
    const D3D12_VIEWPORT viewport{0,0,128,64,0,1};const D3D12_RECT scissor{0,0,128,64};list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);const float clear[4]{};const UINT masks[]{3,1,2};
    for(UINT target=0;target<4;++target){
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE,4> rt{};for(UINT i=0;i<colorCount;++i)rt[i]=rtv(target,i);const auto depth=dsv(target);
        if(!chain || target!=2){
            for(UINT i=0;i<colorCount;++i)list->ClearRenderTargetView(rt[i],clear,0,nullptr);
            list->ClearDepthStencilView(depth,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,1,0,0,nullptr);
        }
        if(chain && target<2){
            scene_state::GeometryTargets captured;captured.colors=colorCount;
            for(UINT slot=0;slot<5;++slot){auto* resource=slot==4?depths[target].Get():colors[target][slot].Get();const auto desc=resource->GetDesc();
                captured.resources[slot]=resource;captured.handles[slot]=slot==4?depth:rt[slot];
                captured.views[slot]={1+target*5+slot,reinterpret_cast<uintptr_t>(resource),desc.Width,desc.Height,UINT(desc.Format),UINT(desc.Format),UINT(desc.Flags),0,0};}
            if(checkpoints){Check(before[target].Create(device.Get(),captured));Check(after[target].Create(device.Get(),captured));
                Require(before[target].Capture(list.Get(),captured),"Native pre-draw checkpoint failed");}
            else Check(retained.PrepareEye(list.Get(),target,captured));
        }
        if(checkpoints && target==2)for(UINT eye=0;eye<2;++eye)
            Check(retained.PrepareEye(list.Get(),eye,before[eye].Targets()));
        const auto emit=[&](ID3D12PipelineState* pipeline,UINT count){
            if(chain && target==2)Require(retained.Bind(list.Get(),3,count),"Retained both-eye bind failed");
            else list->OMSetRenderTargets(count,count?rt.data():nullptr,FALSE,&depth);
            list->SetPipelineState(pipeline);
            for(UINT draw=0;draw<3;++draw){if(target<2 && !(masks[draw]&(1u<<target)))continue;
                if(target>=2)views->SetViewInstanceMask(masks[draw]);
                list->SetGraphicsRootDescriptorTable(0,gpu(target<2?draw*2+(1-target):6+draw));
                list->SetGraphicsRootDescriptorTable(1,gpu(9+draw));list->OMSetStencilRef(11+draw);list->DrawInstanced(3,2,0,0);
            }
        };
        if(chain)emit(target<2?originalDepth.Get():stereoDepth.Get(),0);
        emit(target<2?original.Get():target==2?stereo.Get():wrongPixel.Get(),colorCount);
        if(chain)emit(target<2?originalThree.Get():stereoThree.Get(),3);
        if(checkpoints && target<2){scene_state::GeometryTargets captured;captured.colors=colorCount;
            for(UINT slot=0;slot<5;++slot){auto* resource=slot==4?depths[target].Get():colors[target][slot].Get();const auto desc=resource->GetDesc();
                captured.resources[slot]=resource;captured.handles[slot]=slot==4?depth:rt[slot];
                captured.views[slot]={1+target*5+slot,reinterpret_cast<uintptr_t>(resource),desc.Width,desc.Height,UINT(desc.Format),UINT(desc.Format),UINT(desc.Flags),0,0};}
            Require(after[target].Capture(list.Get(),captured),"Native post-draw checkpoint failed");
            const float overwritten[]{.9f,.1f,.8f,1};
            for(UINT slot=0;slot<colorCount;++slot)list->ClearRenderTargetView(rt[slot],overwritten,0,nullptr);
            list->ClearDepthStencilView(depth,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,.2f,233,0,nullptr);
        }
    }
    if(chain)Require(retained.Counts()==std::array<uint64_t,7>{5,5,0,0,3,0,0},"Retained view-instanced chain copied between passes");
    struct Copy {ID3D12Resource* resource{};UINT subresource{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};UINT rows{};UINT64 rowBytes{};};
    std::vector<Copy> copies;UINT64 total{};
    for(UINT kind=0;kind<colorCount+2;++kind)for(UINT eye=0;eye<2;++eye)for(UINT mode=0;mode<3;++mode){const UINT target=mode?mode+1:eye;
        auto* resource=checkpoints && !mode?after[eye].Targets().resources[kind<colorCount?kind:4].Get():
            kind<colorCount?colors[target][kind].Get():depths[target].Get();const auto d=resource->GetDesc();
        const UINT sub=(mode?eye:0)+(kind==colorCount+1?d.DepthOrArraySize:0);Copy c{resource,sub};UINT64 size{};
        device->GetCopyableFootprints(&d,sub,1,total,&c.layout,&c.rows,&c.rowBytes,&size);total+=size;copies.push_back(c);
    }
    auto readback=Buffer(device.Get(),D3D12_HEAP_TYPE_READBACK,total);
    for(UINT target=0;target<4;++target){for(UINT i=0;i<colorCount;++i)Transition(list.Get(),colors[target][i].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(list.Get(),depths[target].Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE);}
    if(checkpoints)for(UINT eye=0;eye<2;++eye)for(UINT slot=0;slot<5;++slot)
        Transition(list.Get(),after[eye].Targets().resources[slot].Get(),slot==4?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    for(const auto& c:copies){from={};to={};from.pResource=c.resource;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=c.subresource;
        to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=c.layout;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);}
    Check(list->Close());ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ID3D12CommandList* raw=list.Get();auto keep=cvr::gpu::PrepareCommandResources(1,&raw);
    queue->ExecuteCommandLists(1,&raw);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(keep));
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));Check(queue->Signal(fence.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event");Check(fence->SetEventOnCompletion(1,event));const auto waited=WaitForSingleObject(event,5000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"GPU timeout");
    void* mapped{};Check(readback->Map(0,nullptr,&mapped));std::array<size_t,2> nonblank{},negative{};
    for(size_t i=0;i<copies.size();i+=3){const auto& reference=copies[i];const auto& merged=copies[i+1];const auto& wrong=copies[i+2];const auto eye=(i/3)%2,kind=i/6;
        Require(reference.rows==merged.rows && reference.rowBytes==merged.rowBytes,"Footprint mismatch");
        for(UINT y=0;y<reference.rows;++y){auto at=[&](const Copy& c){return static_cast<const uint8_t*>(mapped)+c.layout.Offset+size_t(y)*c.layout.Footprint.RowPitch;};
            Require(!std::memcmp(at(reference),at(merged),SIZE_T(reference.rowBytes)),"Native-root VS/PS color/depth/stencil differs");
            for(UINT64 x=0;x<reference.rowBytes;++x){if(kind<colorCount){nonblank[eye]+=at(reference)[x]!=0;negative[eye]+=at(reference)[x]!=at(wrong)[x];}
                else Require(at(reference)[x]==at(wrong)[x],"Pixel-only negative control changed geometry");}
        }
    }
    readback->Unmap(0,nullptr);Require(nonblank[0]>1000 && nonblank[1]>1000,"Blank output");
    Require(negative[0]>1000 && !negative[1],"Unconverted PS did not expose the peer-camera error");
    for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T n{};messages->GetMessage(i,nullptr,&n);std::vector<uint8_t> data(n);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());Check(messages->GetMessage(i,message,&n));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("D3D12 validation error");}}
    cvr::gpu::ResetCommandResources(list.Get());cvr::gpu::CollectCommandResources();
    std::puts(checkpoints?"PASS delayed paired depth/G-buffer draw from per-eye pre-draw checkpoints, native post-draw targets overwritten, MRT/depth/stencil byte-exact and pixel-camera negative detected":
        chain?"PASS owned retained depth -> four MRT -> three MRT chain, native formats including motion, no intermediate copies, both eyes byte-exact and pixel-camera negative detected":
        "PASS original-root converted VS+PS, dynamic pixel camera indexing, shared texture/material bindings, two MRT arrays, depth/stencil and masks 3/1/2; unconverted-PS negative detected");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
