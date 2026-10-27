#include "Render/ViewInstancedPipeline.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdio>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr,const char* m){if(FAILED(hr)){std::fprintf(stderr,"%s %08X\n",m,unsigned(hr));throw std::runtime_error(m);}}
static std::vector<char> Read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Missing shader");return {std::istreambuf_iterator<char>(f),{}};}
int wmain(int argc,wchar_t** argv) try {
    if(argc<2 || argc>4)throw std::runtime_error("material_pipeline_test <fixture directory> [alternate VS prefix] [depth]");const std::filesystem::path dir=argv[1];
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"Debug");debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Factory");ComPtr<IDXGIAdapter1> adapter;
    Check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)),"Adapter");
    ComPtr<ID3D12Device2> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)),"Device");
    ComPtr<ID3D12InfoQueue> info;Check(device.As(&info),"Info queue");
    D3D12_DESCRIPTOR_RANGE ranges[6]{};D3D12_ROOT_PARAMETER params[6]{};
    for(UINT i=0;i<4;++i){ranges[i].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE(i);ranges[i].NumDescriptors=i==3?16:64;
        params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&ranges[i]};}
    ranges[4]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,32768,0,1,0};params[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[4].DescriptorTable={1,&ranges[4]};
    ranges[5]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,32768,0,4,0};params[5].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[5].DescriptorTable={1,&ranges[5]};
    D3D12_ROOT_SIGNATURE_DESC rs{6,params,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> blob,error;Check(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Serialize");
    ComPtr<ID3D12RootSignature> root;Check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"Root");
    D3D12_INPUT_ELEMENT_DESC input[]{
        {"POSITION",0,DXGI_FORMAT_R16G16B16A16_SNORM,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BONEINDEX",0,DXGI_FORMAT_R32_UINT,0,8,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R16G16_FLOAT,1,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R10G10B10A2_UNORM,2,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TANGENT",0,DXGI_FORMAT_R10G10B10A2_UNORM,2,4,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,3,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R16G16_FLOAT,3,4,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"INSTANCE_TRANSFORM",0,DXGI_FORMAT_R32G32B32A32_FLOAT,7,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_TRANSFORM",1,DXGI_FORMAT_R32G32B32A32_FLOAT,7,16,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_TRANSFORM",2,DXGI_FORMAT_R32G32B32A32_FLOAT,7,32,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_SKINNING_DATA",0,DXGI_FORMAT_R32G32B32A32_UINT,7,48,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1}};
    auto original=Read(dir/L"10042-Vertex.dxil"),patched=Read(dir/L"10042-Vertex-viewid.dxil"),pixel=Read(dir/L"10042-Pixel.dxil"),pixel61=Read(dir/L"10042-Pixel-sm61.dxil");
    if(argc>=3){original=Read(std::filesystem::path(std::wstring(argv[2])+L".dxil"));patched=Read(std::filesystem::path(std::wstring(argv[2])+L"-viewid.dxil"));}
    for(unsigned mode=0;mode<2;++mode) {
        auto& vs=mode?patched:original;auto& ps=mode?pixel61:pixel;D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};d.pRootSignature=root.Get();d.VS={vs.data(),vs.size()};d.PS={ps.data(),ps.size()};
        d.InputLayout={input,UINT(std::size(input))};d.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;d.NumRenderTargets=3;
        d.RTVFormats[0]=d.RTVFormats[1]=DXGI_FORMAT_R10G10B10A2_UNORM;d.RTVFormats[2]=DXGI_FORMAT_R8G8B8A8_UNORM;
        d.DSVFormat=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;d.SampleDesc={1,0};d.SampleMask=UINT_MAX;
        d.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;d.RasterizerState.CullMode=D3D12_CULL_MODE_BACK;d.RasterizerState.DepthClipEnable=TRUE;
        for(auto& b:d.BlendState.RenderTarget){b.SrcBlend=b.SrcBlendAlpha=D3D12_BLEND_ONE;b.DestBlend=b.DestBlendAlpha=D3D12_BLEND_ZERO;
            b.BlendOp=b.BlendOpAlpha=D3D12_BLEND_OP_ADD;b.LogicOp=D3D12_LOGIC_OP_NOOP;b.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;}
        d.DepthStencilState.DepthEnable=TRUE;d.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;d.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        d.DepthStencilState.FrontFace=d.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
        if(argc==4){d.PS={};d.NumRenderTargets=0;for(auto& f:d.RTVFormats)f=DXGI_FORMAT_UNKNOWN;}
        ComPtr<ID3D12PipelineState> pso;cvr::stereo::ViewInstancedLayout layout;layout.allowMasking=true;
        const auto hr=mode?cvr::stereo::CreateViewInstancedPipeline(device.Get(),d,layout,&pso):device->CreateGraphicsPipelineState(&d,IID_PPV_ARGS(&pso));
        for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size{};info->GetMessage(i,nullptr,&size);std::vector<char> data(size);
            auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());info->GetMessage(i,m,&size);std::printf("mode %u D3D12 %u: %s\n",mode,m->ID,m->pDescription);}
        info->ClearStoredMessages();Check(hr,mode?"Instanced material PSO":"Native material PSO");
        std::printf("mode %u accepted\n",mode);
    }
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
