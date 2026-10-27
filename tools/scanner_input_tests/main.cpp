#include "Hooks/ScannerGesture.hpp"
#include "Camera/GazeCenter.hpp"
#include "Camera/TakeoverTargeting.hpp"
#include <array>
#include <cstring>
#include <stdexcept>
#include <limits>
void Check(bool value){if(!value)throw std::runtime_error("scanner input regression");}
int main(){
    constexpr float pi=3.14159265358979323846f;
    const float up=std::tan(44*pi/180),down=std::tan(-55*pi/180),half=(up-down)*.5f;
    const auto gaze=cvr::gaze::FromWindows(up,down,up,down);
    Check(std::abs((gaze.tangentY-(up+down)*.5f)/half)<1.e-6f);
    // Native-dot translation and the XR panel must land on the same pixel.
    const float nativeY=(1+gaze.ndcY)*3088*.5f;
    Check(std::abs(nativeY+cvr::gaze::CanvasOffset(gaze,3088,2856.f/3840)*(2856.f/3840)-1544)<.001f);
    std::array<uint8_t,0x31240> cache{};
    std::array<uint8_t,0x400> source{};
    const uintptr_t sourceAddress=reinterpret_cast<uintptr_t>(source.data());
    std::memcpy(cache.data()+0x30CC8,&sourceAddress,sizeof(sourceAddress));
    uintptr_t player=0x123400;uint64_t entity=42;uint32_t active=1;
    std::memcpy(cache.data(),&player,8);std::memcpy(cache.data()+8,&entity,8);std::memcpy(cache.data()+24,&active,4);
    const float camera[]{1,2,3,1,0,0,0,1},origin[]{4,5,6,1},forward[]{0,1,0,1};
    using namespace cvr::camera;
    std::memcpy(cache.data()+TargetingCameraTransformOffset,camera,sizeof(camera));
    std::memcpy(cache.data()+TargetingDefaultPositionOffset,origin,sizeof(origin));
    std::memcpy(cache.data()+TargetingDefaultForwardOffset,forward,sizeof(forward));
    std::memcpy(source.data()+0x350,origin,sizeof(origin));
    std::memcpy(source.data()+0x370,forward,sizeof(forward));
    Check(!CenterHeadGaze(cache.data(),player,entity+1,gaze.tangentY));
    Check(CenterHeadGaze(cache.data(),player,entity,gaze.tangentY));
    float ray[4];std::memcpy(ray,cache.data()+TargetingDefaultForwardOffset,sizeof(ray));
    Check(std::abs(ray[2]/ray[1]-gaze.tangentY)<1.e-6f);
    Check(!std::memcmp(cache.data()+TargetingCameraTransformOffset,camera,sizeof(camera)));
    Check(!std::memcmp(cache.data()+TargetingDefaultPositionOffset,origin,sizeof(origin)));
    Check(!std::memcmp(source.data()+0x350,origin,sizeof(origin)));
    Check(!std::memcmp(source.data()+0x370,ray,sizeof(ray)));
    Check(cvr::gaze::FromWindows(1,-1,1,-1).tangentY==0);
    cvr::gaze::Publish(gaze);cvr::gaze::headLook=false;Check(cvr::gaze::Read().tangentY==0);cvr::gaze::headLook=true;
    cvr::input::ScannerEarZone zone;
    Check(zone.Contains(-.25f,-.10f,.15f,false));
    Check(zone.Contains(-.20f,-.18f,.12f,false));
    Check(zone.Contains(-.20f,0,-.05f,false));
    Check(!zone.Contains(-.20f,-.30f,-.40f,false)); // ordinary hand rest / weapon position
    Check(!zone.Contains(.10f,-.03f,.06f,false)); // other side of the head
    Check(!zone.Contains(std::numeric_limits<float>::quiet_NaN(),0,0,false));
    cvr::input::ScannerRequest request;bool held=true;
    request.Update(held,0,1000);Check(held);
    request.Update(held,0,1749);Check(held);
    request.Update(held,0,1750);Check(!held); // rejected opening releases LB
    held=true;request.Update(held,0,2000);request.Update(held,1,2040);
    request.Update(held,1,10000);Check(held); // accepted hold can last indefinitely
    request.Update(held,0,10020);Check(!held); // game-driven exit releases our latch
    held=true;request.Update(held,-1,11000);request.Update(held,-1,11750);Check(!held);
    held=true;request.Update(held,1,12000);held=false;request.Update(held,1,12020);
    held=true;request.Update(held,0,12040);Check(held); // a new squeeze gets its own acceptance window
}
