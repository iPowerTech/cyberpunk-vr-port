#include "Render/AlphaDepthContract.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace contract=cvr::stereo::alpha_depth;
using Element=D3D12_INPUT_ELEMENT_DESC;
using Elements=std::vector<Element>;
void Require(bool good,const std::source_location at=std::source_location::current()){
    if(!good)throw std::runtime_error("alpha depth contract assertion at line "+std::to_string(at.line()));
}
Elements Consumed(){
    return {
        {"POSITION",0,DXGI_FORMAT_R16G16B16A16_SNORM,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R16G16_FLOAT,1,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"INSTANCE_TRANSFORM",0,DXGI_FORMAT_R32G32B32A32_FLOAT,7,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_TRANSFORM",1,DXGI_FORMAT_R32G32B32A32_FLOAT,7,16,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_TRANSFORM",2,DXGI_FORMAT_R32G32B32A32_FLOAT,7,32,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
    };
}
Elements Native(){
    auto elements=Consumed();
    elements.insert(elements.begin()+2,{
        {"NORMAL",0,DXGI_FORMAT_R10G10B10A2_UNORM,2,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TANGENT",0,DXGI_FORMAT_R10G10B10A2_UNORM,2,4,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,3,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R16G16_FLOAT,3,4,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
    });
    return elements;
}
Elements SkinnedConsumed(){
    return {
        {"POSITION",0,DXGI_FORMAT_R16G16B16A16_SNORM,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDINDICES",0,DXGI_FORMAT_R8G8B8A8_UINT,0,8,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDINDICES",1,DXGI_FORMAT_R8G8B8A8_UINT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDWEIGHT",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDWEIGHT",1,DXGI_FORMAT_R8G8B8A8_UNORM,0,20,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"EXTRA_DATA",0,DXGI_FORMAT_R16G16B16A16_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R16G16_FLOAT,1,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R10G10B10A2_UNORM,2,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TANGENT",0,DXGI_FORMAT_R10G10B10A2_UNORM,2,4,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"INSTANCE_TRANSFORM",0,DXGI_FORMAT_R32G32B32A32_FLOAT,7,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_TRANSFORM",1,DXGI_FORMAT_R32G32B32A32_FLOAT,7,16,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_TRANSFORM",2,DXGI_FORMAT_R32G32B32A32_FLOAT,7,32,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_SKINNING_DATA",0,DXGI_FORMAT_R32G32B32A32_UINT,7,48,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
    };
}
Elements SkinnedNative(){
    auto elements=SkinnedConsumed();
    elements.insert(elements.begin()+9,{
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,3,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R16G16_FLOAT,3,4,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
    });
    return elements;
}
uint32_t Check(const Elements& elements,std::string_view vertex=contract::VertexShader,
               std::string_view pixel=contract::PixelShader){
    return contract::Contract(vertex,pixel,{elements.data(),UINT(elements.size())});
}
void AcceptedLayouts(){
    Require(Check(Consumed())==1);auto native=Native();Require(native.size()==9 && Check(native)==1);
    std::reverse(native.begin(),native.end());Require(Check(native)==1);
    auto minimal=Consumed();std::rotate(minimal.begin(),minimal.begin()+2,minimal.end());Require(Check(minimal)==1);
    auto unused=Native();
    for(size_t i=2;i<6;++i){
        unused[i].Format=DXGI_FORMAT_R32_FLOAT;unused[i].InputSlot=13;
        unused[i].AlignedByteOffset=48;unused[i].InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
        unused[i].InstanceDataStepRate=2;
    }
    Require(Check(unused)==1);
    unused=Consumed();
    unused.push_back({"CUSTOM",0,DXGI_FORMAT_R32_FLOAT,12,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0});
    unused.push_back({"POSITION",1,DXGI_FORMAT_R32_FLOAT,12,4,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0});
    unused.push_back({"INSTANCE_TRANSFORM",3,DXGI_FORMAT_R32G32B32A32_FLOAT,7,48,
        D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1});
    Require(Check(unused)==1);
    while(unused.size()<32)unused.push_back({"UNUSED",UINT(unused.size()),DXGI_FORMAT_R32_FLOAT,12,0,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0});
    Require(Check(unused)==1);unused.push_back(unused.back());Require(Check(unused)==0);
}
void ShaderPairs(){
    constexpr std::string_view family39="02f81c75e38964877d832f7378a39251132ab93580dfe89017e65d01fc22bc4c";
    const auto layout=Consumed();
    Require(Check(layout,family39)==0);
    Require(Check(layout,contract::PixelShader,contract::VertexShader)==0);
    Require(Check(layout,contract::VertexShader,contract::VertexShader)==0);
    Require(Check(layout,{},contract::PixelShader)==0);
    Require(Check(layout,contract::VertexShader,{})==0);
    Require(Check(layout,{},{})==0);
    auto wrongPixel=std::string(contract::PixelShader);wrongPixel.back()='0';
    Require(Check(layout,contract::VertexShader,wrongPixel)==0);
    auto wrongVertex=std::string(contract::VertexShader);wrongVertex.front()='0';
    Require(Check(layout,wrongVertex)==0);
}
void SkinnedLayoutsAndShaders(){
    auto minimal=SkinnedConsumed();Require(minimal.size()==13 && Check(minimal,contract::SkinnedVertexShader)==2);
    auto native=SkinnedNative();Require(native.size()==15 && Check(native,contract::SkinnedVertexShader)==2);
    std::reverse(native.begin(),native.end());Require(Check(native,contract::SkinnedVertexShader)==2);
    std::rotate(minimal.begin(),minimal.begin()+5,minimal.end());Require(Check(minimal,contract::SkinnedVertexShader)==2);
    auto unused=SkinnedNative();
    for(const size_t input:{9u,10u}){
        unused[input].Format=DXGI_FORMAT_R32_FLOAT;unused[input].InputSlot=13;
        unused[input].AlignedByteOffset=48;unused[input].InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
        unused[input].InstanceDataStepRate=2;
    }
    Require(Check(unused,contract::SkinnedVertexShader)==2);
    unused[9].SemanticName=nullptr;Require(Check(unused,contract::SkinnedVertexShader)==0);
    Require(Check(minimal,contract::SkinnedVertexShader,{})==0);
    Require(Check(minimal,contract::SkinnedVertexShader,contract::VertexShader)==0);
    Require(Check(minimal,contract::SkinnedVertexShader,contract::SkinnedVertexShader)==0);
    Require(Check(minimal,contract::PixelShader,contract::SkinnedVertexShader)==0);
    Require(Check(minimal,{})==0);
    Require(Check(minimal,cvr::stereo::skinning::DepthShader)==0);
    auto wrongPixel=std::string(contract::PixelShader);wrongPixel.back()='0';
    Require(Check(minimal,contract::SkinnedVertexShader,wrongPixel)==0);
    auto wrongVertex=std::string(contract::SkinnedVertexShader);wrongVertex.front()='1';
    Require(Check(minimal,wrongVertex)==0);
    Require(contract::InstanceStride(1)==48 && contract::InstanceStride(2)==64);
    for(const uint32_t unsupported:{0u,3u,48u,64u,UINT32_MAX})Require(contract::InstanceStride(unsupported)==0);
}
void ConsumedMismatches(const Elements& original,std::string_view vertex){
    for(size_t input=0;input<original.size();++input){
        auto check=[&](auto mutate){auto layout=original;mutate(layout[input]);Require(Check(layout,vertex)==0);};
        check([](auto& element){element.SemanticName="UNCONSUMED";});
        check([](auto& element){element.SemanticName="";});
        check([](auto& element){element.SemanticName=nullptr;});
        check([](auto& element){element.SemanticIndex+=10;});
        check([](auto& element){++element.InputSlot;});
        check([](auto& element){++element.AlignedByteOffset;});
        check([](auto& element){element.AlignedByteOffset=D3D12_APPEND_ALIGNED_ELEMENT;});
        check([](auto& element){element.Format=DXGI_FORMAT_R32_FLOAT;});
        check([](auto& element){element.InputSlotClass=element.InputSlotClass==D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA?
            D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA:D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;});
        check([](auto& element){element.InstanceDataStepRate=element.InstanceDataStepRate?0:1;});
        check([](auto& element){element.InstanceDataStepRate=2;});
        auto missing=original;missing.erase(missing.begin()+input);Require(Check(missing,vertex)==0);
        auto duplicate=original;duplicate.push_back(duplicate[input]);Require(Check(duplicate,vertex)==0);
    }
    auto wrongCase=original;wrongCase[0].SemanticName="position";Require(Check(wrongCase,vertex)==0);
}
void MissingDescriptors(){
    Require(contract::Contract(contract::VertexShader,contract::PixelShader,{nullptr,5})==0);
    const auto elements=Consumed();
    Require(contract::Contract(contract::VertexShader,contract::PixelShader,{elements.data(),0})==0);
    Require(contract::Contract(contract::VertexShader,contract::PixelShader,{nullptr,0})==0);
    std::array<Element,33> oversized{};
    std::copy(elements.begin(),elements.end(),oversized.begin());
    Require(contract::Contract(contract::VertexShader,contract::PixelShader,{oversized.data(),33})==0);
    auto unused=Native();unused[2].SemanticName=nullptr;Require(Check(unused)==0);
    Require(contract::Contract(contract::SkinnedVertexShader,contract::PixelShader,{nullptr,13})==0);
    const auto skinned=SkinnedConsumed();
    Require(contract::Contract(contract::SkinnedVertexShader,contract::PixelShader,{skinned.data(),0})==0);
    auto boundary=skinned;
    while(boundary.size()<32)boundary.push_back({"UNUSED",UINT(boundary.size()),DXGI_FORMAT_R32_FLOAT,12,0,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0});
    Require(Check(boundary,contract::SkinnedVertexShader)==2);
    boundary.push_back(boundary.back());Require(Check(boundary,contract::SkinnedVertexShader)==0);
}
}
int main()try{
    AcceptedLayouts();ShaderPairs();SkinnedLayoutsAndShaders();
    ConsumedMismatches(Consumed(),contract::VertexShader);
    ConsumedMismatches(SkinnedConsumed(),contract::SkinnedVertexShader);MissingDescriptors();
    std::puts("PASS exact alpha depth shader pair and consumed input-layout contract");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
