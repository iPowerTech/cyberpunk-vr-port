#include "Render/GBufferMaterialContract.hpp"
#include <cstdio>
#include <initializer_list>
#include <stdexcept>

namespace material=cvr::stereo::gbuffer_material;
namespace {
void Require(bool good,const char* message){if(!good)throw std::runtime_error(message);}
void Word(std::span<std::byte> bytes,size_t offset,uint32_t value) {
    for(size_t i=0;i<4;++i)bytes[offset+i]=std::byte((value>>(8*i))&255u);
}
void Scalar(std::span<std::byte> bytes,size_t offset,float value){Word(bytes,offset,std::bit_cast<uint32_t>(value));}
struct Fixture {
    std::array<std::byte,material::MaterialBytes> b4{};
    std::array<std::byte,material::ModifierBytes> b7{};
    std::array<std::byte,material::GlobalFrameBytes> frame{};
    Fixture() {
        for(size_t i=0;i<material::SelectorOffsets.size();++i)Word(b4,material::SelectorOffsets[i],uint32_t(i+10));
        Scalar(b4,288,1.f);Scalar(b7,4,1.f);Word(frame,0,0x80000042u);
    }
    material::Inputs Inputs() const{return {b4,b7,frame};}
};
void DependenciesAndFlags() {
    Fixture fixture;const auto original=fixture;material::Analysis output;
    Require(material::MatchesShaders(material::VertexShaderSha256,material::PixelShaderSha256),"Exact shader pair rejected");
    Require(!material::MatchesShaders(material::PixelShaderSha256,material::VertexShaderSha256) &&
        !material::MatchesShaders(material::VertexShaderSha256,""),"Different shader pair accepted");
    Require(material::GlobalFrameOffset==452 && material::Analyze(fixture.Inputs(),output)==material::Failure::None,"Valid inputs rejected");
    Require(output.textureCount==4 && !output.alphaTestEnabled && !output.optionalTextureEnabled && output.neutralDither,"Initial flags changed");
    Require(output.sampler.required && output.sampler.shaderRegister==0 && output.sampler.registerSpace==0,"s0 dependency lost");
    Require(output.dither.required && output.dither.shaderRegister==67 && output.dither.registerSpace==0 &&
        output.dither.mip==0 && output.dither.slice==2 && output.dither.coordinateMask==63,"t67 load dependency differs");
    for(uint32_t i=0;i<output.textureCount;++i)
        Require(output.textures[i].selectorOffset==material::SelectorOffsets[i] && output.textures[i].index==i+10,"Texture role/index changed");
    Require(fixture.b4==original.b4 && fixture.b7==original.b7 && fixture.frame==original.frame,"Analysis modified shader inputs");
    Scalar(fixture.b4,192,1.f);Scalar(fixture.b7,0,-2.f);Scalar(fixture.b7,8,1.f);
    Require(material::Analyze(fixture.Inputs(),output)==material::Failure::None && output.textureCount==5 &&
        output.alphaTestEnabled && output.optionalTextureEnabled && !output.neutralDither && output.textures[4].index==14,
        "Shader branch flags were substituted");
    for(const float optional:{-1.f,-0.f,0.f}){
        Scalar(fixture.b4,192,optional);
        Require(material::Analyze(fixture.Inputs(),output)==material::Failure::None && output.textureCount==4,"Nonpositive optional branch enabled");
    }
}
void SelectorBounds() {
    material::Analysis output;
    for(size_t selector=0;selector<material::SelectorOffsets.size();++selector){
        Fixture fixture;Scalar(fixture.b4,192,1.f);
        for(const uint32_t index:{0u,material::DescriptorCount-1}){
            Word(fixture.b4,material::SelectorOffsets[selector],index);
            Require(material::Analyze(fixture.Inputs(),output)==material::Failure::None && output.textures[selector].index==index,"Valid range endpoint rejected");
        }
        for(const uint32_t index:{material::DescriptorCount,0xffffffffu}){
            Word(fixture.b4,material::SelectorOffsets[selector],index);
            Require(material::Analyze(fixture.Inputs(),output)==material::Failure::TextureIndex && !output.textureCount &&
                !output.sampler.required && !output.dither.required,"Invalid selector accepted or left stale output");
        }
    }
    Fixture inactive;Word(inactive.b4,160,0xffffffffu);
    Require(material::Analyze(inactive.Inputs(),output)==material::Failure::None && output.textureCount==4,"Unused selector incorrectly required");
    for(const auto offset:material::SelectorOffsets)Word(inactive.b4,offset,7);
    Scalar(inactive.b4,192,1.f);
    Require(material::Analyze(inactive.Inputs(),output)==material::Failure::None && output.textureCount==5,"Duplicate indices lost texture roles");
    for(const auto& use:output.textures)Require(use.index==7,"Duplicate index changed");
}
void SizesAndFiniteValues() {
    Fixture fixture;material::Analysis output;
    const std::array<std::byte,material::MaterialBytes+1> oversized{};
    auto input=fixture.Inputs();input.material=oversized;
    Require(material::Analyze(input,output)==material::Failure::MaterialSize,"Oversized material accepted");
    input=fixture.Inputs();input.material=input.material.first(material::MaterialBytes-1);
    Require(material::Analyze(input,output)==material::Failure::MaterialSize,"Truncated material accepted");
    input=fixture.Inputs();input.modifiers=input.modifiers.first(material::ModifierBytes-1);
    Require(material::Analyze(input,output)==material::Failure::ModifierSize,"Truncated modifiers accepted");
    const std::array<std::byte,material::ModifierBytes+1> oversizedModifiers{};
    input=fixture.Inputs();input.modifiers=oversizedModifiers;
    Require(material::Analyze(input,output)==material::Failure::ModifierSize,"Oversized modifiers accepted");
    input=fixture.Inputs();input.globalFrameSlice=input.globalFrameSlice.first(material::GlobalFrameBytes-1);
    Require(material::Analyze(input,output)==material::Failure::GlobalFrameSize,"Truncated frame slice accepted");
    const std::array<std::byte,material::GlobalFrameBytes+1> oversizedFrame{};
    input=fixture.Inputs();input.globalFrameSlice=oversizedFrame;
    Require(material::Analyze(input,output)==material::Failure::GlobalFrameSize,"Oversized frame slice accepted");
    const std::array<std::byte,476> fullGlobals{};input=fixture.Inputs();input.globalFrameSlice=fullGlobals;
    Require(material::Analyze(input,output)==material::Failure::GlobalFrameSize,"Unsliced b0 silently accepted");
    for(const uint32_t bits:{0x7f800000u,0xff800000u,0x7fc00000u}){
        for(size_t offset=0;offset<16;offset+=4){
            auto changed=fixture;Word(changed.b7,offset,bits);
            Require(material::Analyze(changed.Inputs(),output)==material::Failure::NonFiniteModifier,"Nonfinite modifier accepted");
        }
        for(const size_t offset:std::array<size_t,10>{16,20,24,48,64,96,112,144,192,288}){
            auto changed=fixture;Word(changed.b4,offset,bits);
            Require(material::Analyze(changed.Inputs(),output)==material::Failure::NonFiniteMaterial,"Nonfinite consumed material accepted");
        }
        for(const size_t offset:std::array<size_t,4>{176,180,184,224}){
            auto changed=fixture;Word(changed.b4,offset,bits);
            Require(material::Analyze(changed.Inputs(),output)==material::Failure::None,"Inactive optional float was consumed");
            Scalar(changed.b4,192,1.f);
            Require(material::Analyze(changed.Inputs(),output)==material::Failure::NonFiniteMaterial,"Nonfinite optional float accepted");
        }
        auto changed=fixture;Word(changed.b4,272,bits);
        Require(material::Analyze(changed.Inputs(),output)==material::Failure::None,"Disabled alpha threshold was consumed");
        Scalar(changed.b7,0,1.f);
        Require(material::Analyze(changed.Inputs(),output)==material::Failure::NonFiniteMaterial,"Nonfinite enabled alpha threshold accepted");
    }
}
void InputEquality() {
    Fixture main,peer;
    Require(material::SameInputs(main.Inputs(),peer.Inputs()),"Equal inputs rejected");
    for(size_t byte=0;byte<material::MaterialBytes;++byte){
        peer=main;peer.b4[byte]^=std::byte{1};
        Require(!material::SameInputs(main.Inputs(),peer.Inputs()),"Material byte difference ignored");
    }
    for(size_t byte=0;byte<material::ModifierBytes;++byte){
        peer=main;peer.b7[byte]^=std::byte{1};
        Require(!material::SameInputs(main.Inputs(),peer.Inputs()),"Modifier byte difference ignored");
    }
    for(size_t byte=0;byte<material::GlobalFrameBytes;++byte){
        peer=main;peer.frame[byte]^=std::byte{64};
        Require(!material::SameInputs(main.Inputs(),peer.Inputs()),"Frame word difference ignored despite equal slice");
    }
    Word(main.b4,0,material::DescriptorCount);peer=main;
    Require(!material::SameInputs(main.Inputs(),peer.Inputs()),"Equal invalid inputs accepted");
}
}
int main() {
    try{DependenciesAndFlags();SelectorBounds();SizesAndFiniteValues();InputEquality();
        std::puts("GBuffer material contract tests passed");return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
