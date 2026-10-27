#include "Render/StereoCameraInput.hpp"
#include "Render/CameraWordMask.hpp"
#include <cstdio>
#include <stdexcept>
using namespace cvr::stereo;
static unsigned calls{};
static void Rebuild(void*) {++calls;}
static void Require(bool v) {if(!v)throw std::runtime_error("peer camera assertion failed");}
int main() try {
    CameraInput source,result;
    // Independently captured fixed-point origins and basis from PID 11564.
    const int32_t left[]={457816185,-49763572,17682253};
    const int32_t rightPosition[]={457813153,-49771392,17682253};
    const float axis[]={-0.3614504337310791f,-0.9323914051055908f,0};
    for(unsigned i=0;i<3;++i)source.Write<int32_t>(i*4,left[i]);
    const auto original=source.bytes;
    const auto jitter=PeekR2Jitter(308322,1485,1485);
    Require(PreparePeerCamera(source.bytes.data(),axis,.032f,jitter,1485,1485,Rebuild,result));
    Require(calls==1 && source.bytes==original);
    for(unsigned i=0;i<3;++i)Require(result.Read<int32_t>(i*4)==rightPosition[i]);
    Require(result.Read<float>(0x370)==.390625f && result.Read<float>(0x374)==.296875f);
    Require(result.Read<uint32_t>(0x378)==1485 && result.Read<uint32_t>(0x380)==2);
    CameraInput restored;
    Require(PreparePeerCamera(result.bytes.data(),axis,-.032f,jitter,1485,1485,Rebuild,restored));
    for(unsigned i=0;i<3;++i)Require(restored.Read<int32_t>(i*4)==left[i]);
    const float invalidAxis[]={2,0,0};
    Require(!PreparePeerCamera(source.bytes.data(),invalidAxis,.032f,jitter,1485,1485,Rebuild,result));
    source.Write<int32_t>(0,std::numeric_limits<int32_t>::max());
    const float unit[]={1,0,0};
    Require(!PreparePeerCamera(source.bytes.data(),unit,.032f,jitter,1485,1485,Rebuild,result));
    Require(!PreparePeerCamera(source.bytes.data(),unit,.032f,jitter,0,1485,Rebuild,result));
    Require(calls==2);
    auto known=CameraWordMask::All(),required=known;known.Exclude(147);
    Require(!known.Contains(required));required.Exclude(147);Require(known.Contains(required));
    CameraInput nativeSource;nativeSource.Write<int32_t>(0,rightPosition[0]);nativeSource.Write<int32_t>(4,rightPosition[1]);nativeSource.Write<int32_t>(8,rightPosition[2]);
    nativeSource.Write<float>(0x18,-.8212610483169556f);nativeSource.Write<float>(0x1C,.5705527067184448f);
    source.Write<float>(0x18,-.8212627172470093f);source.Write<float>(0x1C,.5705501437187195f);
    const auto before=source.bytes;
    Require(PrepareCameraAtPose(source.bytes.data(),nativeSource.bytes.data(),jitter,1485,1485,Rebuild,result));
    Require(source.bytes==before && result.Read<int32_t>(0)==rightPosition[0]);
    Require(result.Read<float>(0x18)==nativeSource.Read<float>(0x18));
    std::puts("PASS captured eye origins, reverse eye, jitter, source ownership, overflow and invalid inputs");return 0;
} catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
