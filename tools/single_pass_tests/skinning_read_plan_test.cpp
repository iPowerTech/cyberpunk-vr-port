#include "Render/SkinningReadPlan.hpp"
#include <cstdio>
#include <stdexcept>
using namespace cvr::stereo;
static void Require(bool value){if(!value)throw std::runtime_error("skinning read contract failed");}
int main()try{
    D3D12_INPUT_ELEMENT_DESC input[]{
        {"POSITION",0,DXGI_FORMAT_R16G16B16A16_SNORM,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDINDICES",0,DXGI_FORMAT_R8G8B8A8_UINT,0,8,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDINDICES",1,DXGI_FORMAT_R8G8B8A8_UINT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDWEIGHT",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDWEIGHT",1,DXGI_FORMAT_R8G8B8A8_UNORM,0,20,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"EXTRA_DATA",0,DXGI_FORMAT_R16G16B16A16_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"INSTANCE_TRANSFORM",0,DXGI_FORMAT_R32G32B32A32_FLOAT,7,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_TRANSFORM",1,DXGI_FORMAT_R32G32B32A32_FLOAT,7,16,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_TRANSFORM",2,DXGI_FORMAT_R32G32B32A32_FLOAT,7,32,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_SKINNING_DATA",0,DXGI_FORMAT_R32G32B32A32_UINT,7,48,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
    };
    D3D12_INPUT_LAYOUT_DESC layout{input,UINT(std::size(input))};
    Require(skinning::DepthContract(skinning::DepthShader,layout)==1);
    Require(!skinning::DepthContract("different",layout));Require(!skinning::DepthContract(skinning::DepthShader,{}));
    for(auto& element:input){const auto original=element;
        element.Format=DXGI_FORMAT_R32G32B32A32_UINT;if(element.Format!=original.Format)Require(!skinning::DepthContract(skinning::DepthShader,layout));
        element=original;element.InputSlot=3;Require(!skinning::DepthContract(skinning::DepthShader,layout));
        element=original;element.AlignedByteOffset=D3D12_APPEND_ALIGNED_ELEMENT;Require(!skinning::DepthContract(skinning::DepthShader,layout));
        element=original;++element.InstanceDataStepRate;Require(!skinning::DepthContract(skinning::DepthShader,layout));
        element=original;element.SemanticName=nullptr;Require(!skinning::DepthContract(skinning::DepthShader,layout));element=original;
    }
    auto duplicate=std::vector<D3D12_INPUT_ELEMENT_DESC>(std::begin(input),std::end(input));duplicate.push_back(input[1]);
    Require(!skinning::DepthContract(skinning::DepthShader,{duplicate.data(),UINT(duplicate.size())}));
    --layout.NumElements;Require(!skinning::DepthContract(skinning::DepthShader,layout));
    auto alpha=std::vector<D3D12_INPUT_ELEMENT_DESC>(std::begin(input),std::end(input));
    Require(!skinning::DepthContract(skinning::AlphaShader,{alpha.data(),UINT(alpha.size())}));
    alpha.insert(alpha.end(),{
        {"TEXCOORD",0,DXGI_FORMAT_R16G16_FLOAT,1,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R10G10B10A2_UNORM,2,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TANGENT",0,DXGI_FORMAT_R10G10B10A2_UNORM,2,4,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
    });
    auto alphaContract=[](const auto& elements){return skinning::DepthContract(skinning::AlphaShader,{elements.data(),UINT(elements.size())});};
    Require(alphaContract(alpha)==2);
    Require(skinning::DepthContract(skinning::DepthShader,{alpha.data(),UINT(alpha.size())})==1);
    for(size_t i=0;i<alpha.size();++i){
        auto mismatch=[&](auto mutate){auto changed=alpha;mutate(changed[i]);Require(!alphaContract(changed));};
        mismatch([](auto& e){e.Format=DXGI_FORMAT_UNKNOWN;});
        mismatch([](auto& e){++e.InputSlot;});
        mismatch([](auto& e){e.AlignedByteOffset=D3D12_APPEND_ALIGNED_ELEMENT;});
        mismatch([](auto& e){++e.InstanceDataStepRate;});
        mismatch([](auto& e){e.InputSlotClass=e.InputSlotClass==D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA?
            D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA:D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;});
        mismatch([](auto& e){e.SemanticIndex+=10;});
        mismatch([](auto& e){e.SemanticName=nullptr;});
        auto missing=alpha;missing.erase(missing.begin()+i);Require(!alphaContract(missing));
        auto repeated=alpha;repeated.push_back(alpha[i]);Require(!alphaContract(repeated));
    }
    std::reverse(alpha.begin(),alpha.end());Require(alphaContract(alpha)==2);
    Require(!skinning::DepthContract(skinning::AlphaShader,{}));
    std::array<uint8_t,112> materialA{},materialB{};materialA.fill(0x11);materialB.fill(0x77);
    for(const auto offset:{64u,68u,72u,80u,84u,88u})std::memcpy(materialB.data()+offset,materialA.data()+offset,4);
    Require(skinning::SameMaterial(materialA,materialB));
    for(const auto offset:{64u,68u,72u,80u,84u,88u})for(UINT byte=0;byte<4;++byte){
        auto changed=materialB;changed[offset+byte]^=1;Require(!skinning::SameMaterial(materialA,changed));
    }
    std::array<uint8_t,448> modifiersA{},modifiersB{};modifiersA.fill(0x11);modifiersB.fill(0x77);
    const uint32_t positiveZero=0,negativeZero=0x80000000;
    std::memcpy(modifiersA.data()+16,&positiveZero,4);
    for(const auto offset:{16u,24u,32u})std::memcpy(modifiersB.data()+offset,modifiersA.data()+offset,4);
    Require(skinning::ZeroCull(modifiersA) && skinning::SameModifiers(modifiersA,modifiersB));
    for(const auto offset:{16u,24u,32u})for(UINT byte=0;byte<4;++byte){
        auto changed=modifiersB;changed[offset+byte]^=1;Require(!skinning::SameModifiers(modifiersA,changed));
    }
    std::memcpy(modifiersB.data()+16,&negativeZero,4);
    Require(skinning::ZeroCull(modifiersB) && !skinning::SameModifiers(modifiersA,modifiersB));
    std::memcpy(modifiersA.data()+16,&negativeZero,4);Require(skinning::SameModifiers(modifiersA,modifiersB));
    for(const auto count:{1u,0x80000001u,0x3f800000u,0xbf800000u,0x7f800000u,0xff800000u,0x7fc00000u,0x7f800001u,0xffc00000u}){
        auto changed=modifiersB;std::memcpy(changed.data()+16,&count,4);
        Require(!skinning::ZeroCull(changed) && !skinning::SameModifiers(changed,changed) &&
            !skinning::SameModifiers(modifiersA,changed) && !skinning::SameModifiers(changed,modifiersA));
    }
    for(const uint32_t boneContract:{1u,2u}){
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_TYPELESS;view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
    view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Buffer={0,15728640,0,D3D12_BUFFER_SRV_FLAG_RAW};
    std::array<uint8_t,64> instance{};
    auto values=[&](uint32_t base,uint32_t stride){std::memcpy(instance.data()+48,&base,4);std::memcpy(instance.data()+52,&stride,4);};
    std::vector<skinning::BoneRange> ranges;
    values(676080,48);Require(skinning::BoneRanges(boneContract,instance,view,62914560,ranges));Require(ranges==std::vector<skinning::BoneRange>{{676080,12288}});
    const auto first=instance;values(673616,48);std::vector<uint8_t> both(first.begin(),first.end());both.insert(both.end(),instance.begin(),instance.end());
    Require(skinning::BoneRanges(boneContract,both,view,62914560,ranges));Require(ranges==std::vector<skinning::BoneRange>{{673616,14752}});
    values(760832,48);both.insert(both.end(),instance.begin(),instance.end());
    Require(skinning::BoneRanges(boneContract,both,view,62914560,ranges));Require(ranges.size()==2 && ranges[1]==skinning::BoneRange{760832,12288});
    for(const auto base:{672736u,668464u}){values(base,48);both.insert(both.end(),instance.begin(),instance.end());}
    Require(skinning::BoneRanges(boneContract,both,view,62914560,ranges));
    Require(ranges==std::vector<skinning::BoneRange>{{668464,19904},{760832,12288}});
    values(16,0);Require(skinning::BoneRanges(boneContract,instance,view,62914560,ranges));Require(ranges[0]==skinning::BoneRange{16,48});
    view.Buffer.FirstElement=4;view.Buffer.NumElements=64;Require(skinning::BoneRanges(boneContract,instance,view,272,ranges));Require(ranges[0]==skinning::BoneRange{32,48});
    Require(!skinning::BoneRanges(boneContract,instance,view,271,ranges) && ranges.empty());
    values(208,0);Require(skinning::BoneRanges(boneContract,instance,view,272,ranges));
    values(212,0);Require(!skinning::BoneRanges(boneContract,instance,view,272,ranges));
    view.Buffer.FirstElement=0;view.Buffer.NumElements=UINT_MAX;
    values(UINT_MAX-47,0);Require(skinning::BoneRanges(boneContract,instance,view,uint64_t(UINT_MAX)*4,ranges));
    values(UINT_MAX-43,0);Require(!skinning::BoneRanges(boneContract,instance,view,uint64_t(UINT_MAX)*4,ranges));
    values(0,UINT_MAX-3);Require(!skinning::BoneRanges(boneContract,instance,view,uint64_t(UINT_MAX)*4,ranges));
    values(0,2);Require(!skinning::BoneRanges(boneContract,instance,view,uint64_t(UINT_MAX)*4,ranges));
    values(2,48);Require(!skinning::BoneRanges(boneContract,instance,view,uint64_t(UINT_MAX)*4,ranges));values(0,48);
    for(const auto start:{1ull,UINT64_MAX}){view.Buffer.FirstElement=start;Require(!skinning::BoneRanges(boneContract,instance,view,UINT64_MAX,ranges));}
    view.Buffer.FirstElement=0;view.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_NONE;Require(!skinning::BoneRanges(boneContract,instance,view,UINT64_MAX,ranges));
    view.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;view.Buffer.StructureByteStride=4;Require(!skinning::BoneRanges(boneContract,instance,view,UINT64_MAX,ranges));view.Buffer.StructureByteStride=0;
    const auto validView=view;
    view.Format=DXGI_FORMAT_R32_UINT;Require(!skinning::BoneRanges(boneContract,instance,view,UINT64_MAX,ranges));view=validView;
    view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;Require(!skinning::BoneRanges(boneContract,instance,view,UINT64_MAX,ranges));view=validView;
    view.Shader4ComponentMapping=0;Require(!skinning::BoneRanges(boneContract,instance,view,UINT64_MAX,ranges));view=validView;
    view.Buffer.NumElements=0;Require(!skinning::BoneRanges(boneContract,instance,view,UINT64_MAX,ranges));view=validView;
    for(const uint32_t unsupported:{0u,3u,UINT_MAX})Require(!skinning::BoneRanges(unsupported,instance,view,UINT64_MAX,ranges));
    Require(!skinning::BoneRanges(boneContract,{},view,UINT64_MAX,ranges));
    Require(!skinning::BoneRanges(boneContract,std::span(instance).first(63),view,UINT64_MAX,ranges));
    }
    std::puts("PASS exact depth/alpha skinning VS/input/constants contracts, zero cull counts and bounded raw bone palettes");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
