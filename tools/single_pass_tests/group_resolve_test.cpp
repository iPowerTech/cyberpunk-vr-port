#include "Render/StereoGroupResolve.hpp"
#include "Render/StereoTargetArray.hpp"
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D12 failure "+std::to_string(unsigned(h)));}
static void Require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
static void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};list->ResourceBarrier(1,&x);}
int main(int argc,char** argv) try {
    const bool hardware=argc>1 && std::strcmp(argv[1],"--hardware")==0;
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    Check(hardware?factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)):factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
    const char shader[]=R"(
cbuffer Input:register(b0){float z;float dx;uint seed;};
float4 VS(uint id:SV_VertexID):SV_Position {
 float2 p[3]={float2(-.8,-.85),float2(.7,-.7),float2(.05,.9)};return float4(p[id]+float2(dx,0),z,1);
}
struct Out{float4 a:SV_Target0;float4 b:SV_Target1;float4 c:SV_Target2;};
Out PS(float4 p:SV_Position){if((uint(p.x)+uint(p.y)*3+seed)%13<3)discard;
 Out o;float4 v=float4((uint(p.x)*37+seed)%1024/1023.,(uint(p.y)*73+seed)%1024/1023.,.431,1);
 o.a=v;o.b=v.zyxw;o.c=float4(v.xy,.173,.671);return o;})";
    ComPtr<ID3DBlob> vs,ps,error,rs;Check(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&error));
    Check(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&error));
    D3D12_ROOT_PARAMETER param{};param.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;param.Constants={0,0,3};
    D3D12_ROOT_SIGNATURE_DESC rd{1,&param,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};Check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&rs,&error));
    ComPtr<ID3D12RootSignature> root;Check(device->CreateRootSignature(0,rs->GetBufferPointer(),rs->GetBufferSize(),IID_PPV_ARGS(&root)));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets=3;pd.RTVFormats[0]=pd.RTVFormats[1]=DXGI_FORMAT_R10G10B10A2_UNORM;pd.RTVFormats[2]=DXGI_FORMAT_R8G8B8A8_UNORM;pd.DSVFormat=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;pd.SampleDesc.Count=1;pd.SampleMask=UINT_MAX;
    pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
    for(auto& b:pd.BlendState.RenderTarget){b.SrcBlend=b.SrcBlendAlpha=D3D12_BLEND_ONE;b.DestBlend=b.DestBlendAlpha=D3D12_BLEND_ZERO;b.BlendOp=b.BlendOpAlpha=D3D12_BLEND_OP_ADD;b.LogicOp=D3D12_LOGIC_OP_NOOP;b.RenderTargetWriteMask=15;}
    auto& ds=pd.DepthStencilState;ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D12_COMPARISON_FUNC_GREATER_EQUAL;ds.StencilEnable=TRUE;ds.StencilReadMask=ds.StencilWriteMask=255;
    ds.FrontFace=ds.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_REPLACE,D3D12_COMPARISON_FUNC_ALWAYS};
    Require(cvr::stereo::StereoGroupResolve::Supports(pd.BlendState,ds),"Valid opaque policy rejected");
    auto bad=ds;bad.FrontFace.StencilFailOp=D3D12_STENCIL_OP_ZERO;Require(!cvr::stereo::StereoGroupResolve::Supports(pd.BlendState,bad),"Stencil side effects accepted");
    bad=ds;bad.DepthFunc=D3D12_COMPARISON_FUNC_EQUAL;Require(!cvr::stereo::StereoGroupResolve::Supports(pd.BlendState,bad),"Equal depth accepted");
    auto blend=pd.BlendState;blend.RenderTarget[0].BlendEnable=TRUE;Require(!cvr::stereo::StereoGroupResolve::Supports(blend,ds),"Blend accepted");
    blend=pd.BlendState;blend.RenderTarget[0].RenderTargetWriteMask=7;Require(!cvr::stereo::StereoGroupResolve::Supports(blend,ds),"Partial colour writes accepted");
    ComPtr<ID3D12PipelineState> pipeline;Check(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pipeline)));
    constexpr UINT width=129,height=97;const DXGI_FORMAT formats[]{DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32G8X24_TYPELESS};
    std::array<std::array<cvr::stereo::StereoTargetArray,4>,3> surfaces;
    ComPtr<ID3D12DescriptorHeap> rtv,dsv;D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,18,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv)));hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=6;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsv)));
    auto rh=[&](UINT mode,UINT eye,UINT target){auto h=rtv->GetCPUDescriptorHandleForHeapStart();h.ptr+=(mode*6+eye*3+target)*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);return h;};
    auto dh=[&](UINT mode,UINT eye){auto h=dsv->GetCPUDescriptorHandleForHeapStart();h.ptr+=(mode*2+eye)*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);return h;};
    for(UINT mode=0;mode<3;++mode)for(UINT t=0;t<4;++t){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=width;d.Height=height;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=formats[t];d.Flags=t==3?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL:D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Check(surfaces[mode][t].Initialize(device.Get(),d,t==3?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET));
        for(UINT eye=0;eye<2;++eye){if(t<3){D3D12_RENDER_TARGET_VIEW_DESC v{};v.Format=formats[t];v.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2DARRAY;v.Texture2DArray.FirstArraySlice=eye;v.Texture2DArray.ArraySize=1;device->CreateRenderTargetView(surfaces[mode][t].Resource(),&v,rh(mode,eye,t));}
            else {D3D12_DEPTH_STENCIL_VIEW_DESC v{};v.Format=pd.DSVFormat;v.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY;v.Texture2DArray.FirstArraySlice=eye;v.Texture2DArray.ArraySize=1;device->CreateDepthStencilView(surfaces[mode][t].Resource(),&v,dh(mode,eye));}}}
    cvr::stereo::StereoGroupResolve resolver;Check(resolver.Initialize(device.Get(),{surfaces[2][0].Resource(),surfaces[2][1].Resource(),surfaces[2][2].Resource(),surfaces[2][3].Resource()}));
    std::array<std::array<std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,4>,4>,2> fp{};UINT64 total{};
    UINT rows[4][4]{};UINT64 rowBytes[4][4]{};
    for(UINT mode=0;mode<2;++mode)for(UINT t=0;t<4;++t){auto d=surfaces[mode][t].Resource()->GetDesc();total=(total+511)&~UINT64(511);UINT64 size{};
        device->GetCopyableFootprints(&d,0,t==3?4:2,total,fp[mode][t].data(),rows[t],rowBytes[t],&size);total+=size;}
    D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=total;b.Height=b.DepthOrArraySize=b.MipLevels=b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES mem{};mem.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> readback;Check(device->CreateCommittedResource(&mem,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    UINT64 compared{},different{};
    for(UINT cycle=0;cycle<12;++cycle){
        const float depths[]{0.f,.6f,1.f};const float z=depths[cycle%3];const UINT stencil=cycle%2?7:0;
        ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        const D3D12_VIEWPORT vp{0,0,float(width),float(height),0,1};const D3D12_RECT scissor{3,5,LONG(width-7),LONG(height-9)};
        for(UINT mode=0;mode<3;++mode)for(UINT eye=0;eye<2;++eye){const float clear[]{.17f,.29f+.1f*eye,.3f,1};const float zero[4]{};
            for(UINT t=0;t<3;++t)list->ClearRenderTargetView(rh(mode,eye,t),mode==2?zero:clear,0,nullptr);
            list->ClearDepthStencilView(dh(mode,eye),D3D12_CLEAR_FLAGS(3),0,mode==2?255:17+eye*34,0,nullptr);
            if(mode!=2)for(UINT strip=0;strip<4;++strip){const D3D12_RECT r{LONG(strip*width/4),0,LONG((strip+1)*width/4),height};
                const float occluder=strip==0?0:strip==1?z:strip==2?1.f:.35f;list->ClearDepthStencilView(dh(mode,eye),D3D12_CLEAR_FLAGS(3),occluder,UINT8(33+strip*23+eye),1,&r);}
            if(mode==1)continue;
            const D3D12_CPU_DESCRIPTOR_HANDLE targets[]{rh(mode,eye,0),rh(mode,eye,1),rh(mode,eye,2)};const auto depth=dh(mode,eye);
            list->OMSetRenderTargets(3,targets,FALSE,&depth);list->OMSetStencilRef(stencil);list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&scissor);list->SetPipelineState(pipeline.Get());list->SetGraphicsRootSignature(root.Get());list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            if(cycle!=11)for(UINT object=0;object<1+cycle%3;++object){struct {float z,dx;UINT seed;} input{z,(eye?.07f:-.11f)+object*.09f,cycle+object*61};list->SetGraphicsRoot32BitConstants(0,3,&input,0);list->DrawInstanced(3,1,0,0);}}
        for(UINT eye=0;eye<2;++eye)resolver.Record(list.Get(),eye,stencil,vp,scissor,{rh(1,eye,0),rh(1,eye,1),rh(1,eye,2)},dh(1,eye));
        for(UINT mode=0;mode<2;++mode)for(UINT t=0;t<4;++t){auto* r=surfaces[mode][t].Resource();const auto state=t==3?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET;Barrier(list.Get(),r,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
            for(UINT sub=0;sub<(t==3?4u:2u);++sub){D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=r;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=sub;to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp[mode][t][sub];list->CopyTextureRegion(&to,0,0,0,&from,nullptr);}Barrier(list.Get(),r,D3D12_RESOURCE_STATE_COPY_SOURCE,state);}
        Check(list->Close());ID3D12CommandList* commands[]{list.Get()};queue->ExecuteCommandLists(1,commands);Check(queue->Signal(fence.Get(),cycle+1));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"event");Check(fence->SetEventOnCompletion(cycle+1,event));const auto wait=WaitForSingleObject(event,5000);CloseHandle(event);Require(wait==WAIT_OBJECT_0,"GPU timeout");
        void* mapped{};D3D12_RANGE range{0,SIZE_T(total)};Check(readback->Map(0,&range,&mapped));UINT64 diff[4]{};
        for(UINT t=0;t<4;++t)for(UINT sub=0;sub<(t==3?4u:2u);++sub)for(UINT y=0;y<rows[t][sub];++y){const auto* a=static_cast<const uint8_t*>(mapped)+fp[0][t][sub].Offset+y*fp[0][t][sub].Footprint.RowPitch;const auto* b=static_cast<const uint8_t*>(mapped)+fp[1][t][sub].Offset+y*fp[1][t][sub].Footprint.RowPitch;
            for(UINT64 x=0;x<rowBytes[t][sub];++x){diff[t]+=a[x]!=b[x];++compared;}}
        D3D12_RANGE none{};readback->Unmap(0,&none);for(auto d:diff)different+=d;std::printf("cycle %u depth %.2f stencil %u: differences %llu/%llu/%llu/%llu\n",cycle,z,stencil,diff[0],diff[1],diff[2],diff[3]);
    }
    ComPtr<ID3D12InfoQueue> info;Check(device.As(&info));for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T n{};info->GetMessage(i,nullptr,&n);std::vector<char> data(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());info->GetMessage(i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",m->pDescription);throw std::runtime_error("D3D12 validation error");}}
    Require(different==0,"Resolve differs from native opaque group");std::printf("PASS %s depth-aware resolve: %llu bytes exact, 12 cases, two eyes, RGB10/RGBA8/depth/stencil, no D3D12 errors\n",hardware?"hardware":"WARP",compared);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
