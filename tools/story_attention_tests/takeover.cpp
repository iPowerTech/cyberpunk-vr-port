#include "Camera/TakeoverTargeting.hpp"
#include <array>
#include <cstdio>
#include <limits>

namespace {
int failures{};
void Check(bool ok,const char* message) {if(!ok){std::printf("FAIL: %s\n",message);++failures;}}
template<class T>void Write(auto& data,size_t offset,T value){std::memcpy(data.data()+offset,&value,sizeof(value));}
float Read(const auto& data,size_t offset){float value;std::memcpy(&value,data.data()+offset,4);return value;}
}
int main() {
    using namespace cvr::camera;
    std::array<uint8_t,0x31240> state{};
    constexpr uintptr_t player=0x12345000;constexpr uint64_t entity=1;
    Write(state,0,player);Write(state,8,entity);Write(state,24,uint32_t{1});
    const float pose[]{-1838.8783f,-2258.7278f,46.7982f,0.f,
                      -.0270721503f,.1968160123f,-.9709246755f,.1335512251f};
    std::memcpy(state.data()+TargetingCameraTransformOffset,pose,sizeof(pose));
    const float body[]{-1863.1859f,-2340.3289f,46.4356f,1.f};
    std::memcpy(state.data()+TargetingDefaultPositionOffset,body,sizeof(body));
    const auto before=state;
    Check(RefreshTakeoverTargeting(state.data(),player,entity),"active player takeover");
    Check(std::abs(Read(state,TargetingDefaultPositionOffset)-pose[0])<.001f,"origin follows remote lens");
    const float reed[]{-1835.4882f,-2273.1521f,40.4745f};
    float oldDot=0,newDot=0,distance2=0;
    for(size_t i=0;i<3;++i){
        const float forward=Read(state,TargetingDefaultForwardOffset+i*4);
        oldDot+=(reed[i]-body[i])*forward;
        newDot+=(reed[i]-pose[i])*forward;
        distance2+=(reed[i]-pose[i])*(reed[i]-pose[i]);
    }
    Check(oldDot<0,"captured body ray rejects Reed as behind the player");
    Check(newDot/std::sqrt(distance2)>std::cos(15.f*3.14159265f/180.f),"camera ray meets native 15 degree condition");
    Check(std::abs(Read(state,TargetingDefaultAnglesOffset+4)-22.9183f)<.001f,"native pitch convention");
    Check(std::abs(Read(state,TargetingDefaultAnglesOffset+8)+164.3362f)<.001f,"native yaw convention");
    auto away=before;
    const float awayRotation[]{0.f,0.f,0.f,1.f};
    std::memcpy(away.data()+TargetingCameraTransformOffset+16,awayRotation,sizeof(awayRotation));
    Check(RefreshTakeoverTargeting(away.data(),player,entity),"opposite camera direction accepted as input");
    float awayDot=0;
    for(size_t i=0;i<3;++i)awayDot+=(reed[i]-pose[i])*Read(away,TargetingDefaultForwardOffset+i*4);
    Check(awayDot<0,"looking away still fails the native target test");
    auto switched=state;
    Write(switched,TargetingCameraTransformOffset,2000.f);
    Check(RefreshTakeoverTargeting(switched.data(),player,entity) &&
          Read(switched,TargetingDefaultPositionOffset)==2000.f,"camera switching reads the new frame origin");
    for(size_t i=0;i<state.size();++i){
        const bool changed=i>=TargetingDefaultPositionOffset && i<TargetingDefaultAnglesOffset+12;
        if(!changed && state[i]!=before[i]){Check(false,"unrelated state preserved");break;}
    }
    auto fixed=state;
    Check(RefreshTakeoverTargeting(state.data(),player,entity) && state==fixed,"repeated update is stable");
    for(auto invalidPlayer:{uintptr_t{0},player+8}){
        Check(!RefreshTakeoverTargeting(state.data(),invalidPlayer,entity) && state==fixed,"non-player rejected");
    }
    Check(!RefreshTakeoverTargeting(state.data(),player,entity+1) && state==fixed,"replaced entity rejected");
    Write(state,24,uint32_t{0});fixed=state;
    Check(!RefreshTakeoverTargeting(state.data(),player,entity) && state==fixed,"inactive user rejected");
    Write(state,24,uint32_t{1});
    Write(state,TargetingCameraTransformOffset,std::numeric_limits<float>::quiet_NaN());fixed=state;
    Check(!RefreshTakeoverTargeting(state.data(),player,entity) && state==fixed,"invalid position rejected");
    std::memcpy(state.data()+TargetingCameraTransformOffset,pose,sizeof(pose));
    std::memset(state.data()+TargetingCameraTransformOffset+16,0,16);fixed=state;
    Check(!RefreshTakeoverTargeting(state.data(),player,entity) && state==fixed,"missing camera rotation rejected");
    std::printf("Takeover targeting: %d failures\n",failures);
    return failures?1:0;
}
