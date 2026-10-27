#include "Stereo/RenderParity.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace cvr::stereo;
int checks{};
void Check(bool value,const char* why){++checks;if(!value){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
int main(){
    const RenderOwner owner{1,2,3,4,5,0x77AD6D6871650500ull,2560,2560};
    SkyRadiance out{.03f,.060146496f,.0937846f,1};
    SkyRadianceSample source{owner,{30.000004f,60.146496f,93.7846f,1},100,true};
    Check(source.Read(owner,100,out) && out==source.color,"MAIN-first radiance");
    Check(source.Read(owner,101,out) && out==source.color,"VRCAM-first uses preceding MAIN");
    const SkyRadiance sentinel{9,8,7,6};out=sentinel;
    Check(!source.Read(owner,102,out) && out==sentinel,"skipped frame does not overwrite target");
    Check(!source.Read(owner,99,out),"future observation rejected");
    for(int field=0;field<8;++field){
        auto changed=owner;
        switch(field){
        case 0:++changed.renderer;break;case 1:++changed.player;break;
        case 2:++changed.mainCamera;break;case 3:++changed.eyeCamera;break;
        case 4:++changed.origin;break;case 5:++changed.eyeName;break;
        case 6:++changed.width;break;case 7:++changed.height;break;}
        Check(!source.Read(changed,101,out),"load, recenter, camera and resolution changes invalidate sample");
    }
    source.frame=0xffffffffu;Check(source.Read(owner,0,out),"native frame counter wrap");
    source.valid=false;Check(!source.Read(owner,0,out),"no MAIN observation");source.valid=true;
    Check(!source.Read({},0,out),"missing live stereo owner");
    // Weather changes and non-unit intensity must be copied exactly; a fixed
    // x1000 compensation cannot reproduce these cases or recover a black input.
    for(const auto color:{SkyRadiance{0,0,0,1},SkyRadiance{.7f,1.3f,2.1f,137.5f},
                          SkyRadiance{100000,240000,190000,.6f},SkyRadiance{1,1,1,0}}){
        source.color=color;out={0,0,0,0};
        Check(source.Read(owner,0,out) && out==color,"current authored radiance and zero remain valid");
    }
    for(int i=0;i<4;++i)for(float bad:{-1.f,std::numeric_limits<float>::infinity(),
                                       std::numeric_limits<float>::quiet_NaN()}){
        source.color={1,2,3,1};source.color[i]=bad;out=sentinel;
        Check(!source.Read(owner,0,out) && out==sentinel,"invalid channels fail without output writes");
    }
    source.color={std::numeric_limits<float>::max(),1,1,2};
    Check(!source.Read(owner,0,out),"overflowing radiance rejected");
    std::cout<<"PASS "<<checks<<" sky radiance, weather and lifetime checks\n";
}
