#include "Render/StereoShaderCamera.hpp"
#include <stdexcept>
#include <cstdio>
using namespace cvr::stereo;
void Require(bool value){if(!value)throw std::runtime_error("prepared camera history failed");}
template<class T>void Put(ShaderCameraHistory& h,size_t offset,const T& value){std::memcpy(h.bytes.data()+offset,&value,sizeof(value));}
int main()try{
    CameraInput current,previous;current.Write<uint32_t>(0x378,1485);current.Write<uint32_t>(0x37C,1485);
    current.Write<float>(0x20,100);current.Write<uint8_t>(0x384,4);
    ShaderCameraHistory history;history.bytes[0]=1;
    const int32_t pose[]{1234,5678,9012};const float quaternion[]{0,0,0,1};
    const camera_math::Matrix view{{{1,2,3,0},{4,5,6,0},{7,8,9,0},{10,11,12,1}}};
    const camera_math::Matrix projection{{{1,0,0,0},{0,1,0,0},{0,0,2,1},{0,0,.02f,0}}};
    Put(history,8,pose);Put(history,0x20,quaternion);Put(history,0x30,view);Put(history,0xB0,projection);Put(history,0x180,.02f);
    const auto before=history.bytes;float weight{};
    Require(DecodeShaderCameraHistory(current,history,previous,weight));Require(history.bytes==before && weight==.02f);
    Require(previous.Read<int32_t>(0)==1234 && previous.Read<int32_t>(8)==9012);
    Require(previous.Read<float>(0x250)==1 && previous.Read<float>(0x254)==4 && previous.Read<float>(0x258)==7 && previous.Read<float>(0x25C)==0);
    Require(previous.Read<float>(0x260)==3 && previous.Read<float>(0x270)==2);
    Require(previous.Read<uint32_t>(0x378)==1485 && previous.Read<float>(0x20)==100);
    const auto valid=previous.bytes;
    history.bytes[0]=0;Require(!DecodeShaderCameraHistory(current,history,previous,weight));Require(previous.bytes==valid);
    history.bytes[0]=1;Put(history,0x180,-1.f);Require(!DecodeShaderCameraHistory(current,history,previous,weight));
    Put(history,0x180,.02f);Put(history,0xB0+32,.1f);Require(!DecodeShaderCameraHistory(current,history,previous,weight));
    std::puts("PASS native history layout, axis order, ownership and rejection");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
