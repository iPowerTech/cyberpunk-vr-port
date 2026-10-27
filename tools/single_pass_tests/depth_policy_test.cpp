#include "Render/NativeDepthPolicy.hpp"
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <wrl/client.h>
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include <array>
#include <cstdint>
using Microsoft::WRL::ComPtr;
void Check(bool value){if(!value)throw std::runtime_error("depth policy assertion failed");}
ComPtr<ID3DBlob> Compile(const char* source,const char* target="ps_5_1"){
    ComPtr<ID3DBlob> code,error;
    const auto hr=D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main",target,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
    if(FAILED(hr)){if(error)std::fprintf(stderr,"%s\n",static_cast<char*>(error->GetBufferPointer()));Check(false);}return code;
}
int main(int argc,char** argv)try{
    if(argc==3 || argc==4){SetEnvironmentVariableA("CVR_STEREO_REFLECTION_DXC",argv[1]);
        std::ifstream file(std::filesystem::path(argv[2]),std::ios::binary);Check(bool(file));
        const std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};
        for(unsigned i=0;i<16;++i)Check(cvr::stereo::ShaderReadOnly({bytes.data(),bytes.size()})==(argc==3));
    }else Check(argc==1);
    auto read=Compile("Texture2D<float4> t:register(t0); float4 main(float4 p:SV_Position):SV_Target{return t.Load(int3(p.xy,0));}");
    auto write=Compile("RWStructuredBuffer<uint> b:register(u0); float4 main(float4 p:SV_Position):SV_Target{b[0]=uint(p.x);return 1;}");
    Check(cvr::stereo::ShaderReadOnly({read->GetBufferPointer(),read->GetBufferSize()}));
    Check(!cvr::stereo::ShaderReadOnly({write->GetBufferPointer(),write->GetBufferSize()}));
    Check(cvr::stereo::ShaderReadOnly({}));Check(!cvr::stereo::ShaderReadOnly({nullptr,32}));
    const char invalid[32]{};Check(!cvr::stereo::ShaderReadOnly({invalid,sizeof(invalid)}));
    auto vertex=Compile("float4 main(float4 p:POSITION):SV_Position{return p;}","vs_5_1");
    auto array=Compile("struct O{float4 p:SV_Position;uint layer:SV_RenderTargetArrayIndex;};O main(float4 p:POSITION){O o;o.p=p;o.layer=1;return o;}","vs_5_1");
    auto viewport=Compile("struct O{float4 p:SV_Position;uint viewport:SV_ViewportArrayIndex;};O main(float4 p:POSITION){O o;o.p=p;o.viewport=1;return o;}","vs_5_1");
    auto vertexWrite=Compile("RWStructuredBuffer<uint>b:register(u0);float4 main(float4 p:POSITION):SV_Position{b[0]=1;return p;}","vs_5_1");
    auto compute=Compile("[numthreads(1,1,1)]void main(){}","cs_5_1");
    const auto single=[](ID3DBlob* blob,bool vs){return cvr::stereo::ShaderSingleLayerReadOnly({blob->GetBufferPointer(),blob->GetBufferSize()},vs);};
    Check(single(vertex.Get(),true) && single(read.Get(),false));
    Check(!single(array.Get(),true) && !single(viewport.Get(),true) && !single(write.Get(),false) && !single(vertexWrite.Get(),true));
    Check(!single(vertex.Get(),false) && !single(read.Get(),true) && !single(compute.Get(),true) && !single(compute.Get(),false));
    Check(cvr::stereo::ShaderSingleLayerReadOnly({},false) && !cvr::stereo::ShaderSingleLayerReadOnly({},true));
    Check(!cvr::stereo::ShaderSingleLayerReadOnly({nullptr,32},false));
    Check(!cvr::stereo::ShaderSingleLayerReadOnly({invalid,sizeof(invalid)},true));
    // The legacy reference policy still permits read-only array-selecting VSs.
    Check(cvr::stereo::ShaderReadOnly({array->GetBufferPointer(),array->GetBufferSize()}));
    Check(cvr::stereo::ShaderReadOnly({viewport->GetBufferPointer(),viewport->GetBufferSize()}));
    std::array<uint8_t,52> features{};std::memcpy(features.data(),"DXBC",4);
    const auto word=[&](size_t at,uint32_t value){std::memcpy(features.data()+at,&value,4);};
    word(24,UINT(features.size()));word(28,1);word(32,36);std::memcpy(features.data()+36,"SFI0",4);word(40,8);
    const uint64_t viewId=D3D_SHADER_REQUIRES_VIEW_ID;std::memcpy(features.data()+44,&viewId,8);
    Check(cvr::stereo::ExplicitShaderResources({features.data(),features.size()}));
    Check(!cvr::stereo::ShaderSingleLayerReadOnly({features.data(),features.size()},true));
    std::puts("PASS native reference/single-layer shader policy, stage identity, UAV rejection and array/viewport routing rejection");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
