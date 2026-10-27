#include "Render/StereoShaderCamera.hpp"
#include <DirectXMath.h>
#include <fstream>
#include <cstdio>
#include <stdexcept>
using namespace cvr::stereo;
static void* Invert(const void* source,void* destination) {
    const auto input=DirectX::XMLoadFloat4x4(static_cast<const DirectX::XMFLOAT4X4*>(source));
    DirectX::XMStoreFloat4x4(static_cast<DirectX::XMFLOAT4X4*>(destination),DirectX::XMMatrixInverse(nullptr,input));
    return destination;
}
static void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv) try {
    Require(argc==2,"Pass the independently recorded fixture file");
    std::ifstream stream(argv[1],std::ios::binary);Require(bool(stream),"Cannot read fixture");
    auto read=[&](auto& value){stream.read(reinterpret_cast<char*>(&value),sizeof(value));Require(bool(stream),"Truncated fixture");};
    uint32_t count{};read(count);Require(count>0 && count<10000,"Invalid sample count");
    unsigned inverseWords{};float maxInverseError{};
    for(uint32_t sample=0;sample<count;++sample) {
        ShaderCamera source{},expected{},actual{};CameraInput current,previous;
        read(source);read(current.bytes);read(previous.bytes);read(expected);
        const auto unchanged=source;
        float weight{};std::memcpy(&weight,expected.data()+588,4);
        Require(BuildPeerShaderCamera(source,current,previous,weight,Invert,actual),"Valid native sample was rejected");
        Require(source==unchanged,"Source constants changed");
        for(unsigned offset=0;offset<848;offset+=4) {
            if(std::memcmp(expected.data()+offset,actual.data()+offset,4)==0)continue;
            if(offset<64 || offset>=128) {
                std::fprintf(stderr,"sample %u field 0x%X differs\n",sample,offset);
                Require(false,"Non-inverse shader field differs from native capture");
            }
            float a{},b{};std::memcpy(&a,actual.data()+offset,4);std::memcpy(&b,expected.data()+offset,4);
            const float error=std::abs(a-b)/(1+std::abs(b));
            maxInverseError=std::max(maxInverseError,error);++inverseWords;
            Require(std::isfinite(error) && error<0.000005f,"Independent inverse exceeds tolerance");
        }
        const auto valid=actual;
        current.Write<float>(0x20,0);
        Require(!BuildPeerShaderCamera(source,current,previous,weight,Invert,actual),"Unsupported orthographic camera accepted");
        Require(actual==valid,"Rejected input changed output");
    }
    std::printf("PASS %u native camera/history fixtures; all non-inverse fields bit-exact; independent inverse relative error %.9g (%u differing words)\n",count,maxInverseError,inverseWords);
    return 0;
} catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
