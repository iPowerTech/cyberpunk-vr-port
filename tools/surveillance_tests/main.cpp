#include "Camera/SurveillanceFollow.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace cvr::camera;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Near(float x,float y,const char* why){Check(std::abs(SurveillanceWrap(x-y))<.0001f,why);}
int main()try {
    SurveillanceInputSample sample;
    float input[3]={0,.2f,-.1f},automatic[3]={0,4,5},other[3]={1,2,3};
    sample.Capture(17,input,true);input[1]+=32.09f;input[2]+=37.15f;
    Check(sample.Finish(17,automatic,input),"live camera modifier scope was lost");
    Near(input[1],.2f,"native automatic pitch reached the HMD motor");
    Near(input[2],-.1f,"native automatic yaw reached the HMD motor");
    Near(automatic[1],0,"alternative auto rotation replaced the HMD input");
    Check(!sample.Finish(17,automatic,input),"one input sample was reused twice");
    sample.Capture(17,input,true);
    Check(!sample.Finish(18,automatic,input),"unrelated controller was modified");
    sample.Capture(17,input,true);
    Check(!sample.Finish(17,automatic,other),"unrelated input buffer was modified");
    Near(other[1],2,"native unrelated input changed");
    sample.Capture(17,input,false);
    Check(!sample.Finish(17,automatic,input),"menu or inactive mode filtered native input");
    Near(SurveillanceManualYaw(44,4,-45,45),1,"manual view crosses native yaw stop");
    Near(SurveillanceManualYaw(45,4,-45,45),0,"held mouse accumulates past native yaw stop");
    Near(SurveillanceManualYaw(45,-1,-45,45),-1,"native stop delays reverse mouse input");
    Near(SurveillanceManualYaw(355,10,300,60),10,"wrapped native yaw arc blocks movement");
    Near(SurveillanceManualYaw(355,10,0,0),10,"unlimited camera yaw was clamped");
    for(int fps:{30,45,60,90,144})for(float speed:{.05f,.5f,2.f,4.f}) {
        SurveillanceFollow follow;float motor=0,manual=0;const float initial=1.1f;
        for(int i=0;i<fps*12;++i) {
            const float t=float(i)/fps,head=std::sin(t*speed)*1.5f,pitch=std::sin(t*.7f)*1.4f;
            const float owner=.03f*t,mouse=i%17==0 ? .002f:0.f;manual+=mouse;
            const uint64_t now=1000+uint64_t(i)*1000/fps;
            const auto delta=follow.Step(1,2,3,now,{head,pitch},owner+initial+motor,owner,mouse,true);
            motor=std::clamp(motor+delta.yaw,-1.f,1.f); // native motor limit
            float view{};Check(follow.View(1,2,3,now,owner,view),"active view unavailable");
            Near(view,owner+initial+manual,"physical head turn was applied twice to view base");
            const auto repeat=follow.Step(1,2,3,now,{head,pitch},owner+initial+motor,owner,0,true);
            Near(repeat.yaw,0,"same HMD sample moved motor twice");Near(repeat.pitch,0,"same pitch moved motor twice");
        }
    }
    SurveillanceFollow f;f.Step(1,2,3,1000,{3.13f,0},.7f,0,0,true);
    Near(f.Step(1,2,3,1010,{-3.13f,0},.7f,0,0,true).yaw,.0231853f,"yaw wrap spun motor backwards");
    auto stopped=f.Step(1,2,3,1020,{1,1},.7f,0,0,false);Near(stopped.yaw,0,"menu moved motor");
    Near(f.Step(1,2,3,1030,{2,1},.7f,0,0,true).yaw,0,"resume applied menu head movement");
    Near(f.Step(1,2,4,1040,{-1,0},.9f,0,0,true).yaw,0,"recenter drove old head delta");
    Near(f.Step(5,6,4,1050,{1,0},-1.2f,0,0,true).yaw,0,"camera switch drove old head delta");
    float view{};Check(!f.View(1,2,3,1050,0,view),"old camera retains view");
    Check(f.View(5,6,4,1050,0,view),"new camera has no view");Near(view,-1.2f,"new camera retained old orientation");
    Check(!f.View(5,6,4,2000,0,view),"expired controller steers render");
    Near(f.Step(5,6,4,2000,{-2,0},.2f,0,0,true).yaw,0,"long tracking gap rotates motor");
    Check(f.View(5,6,4,2000,0,view),"reacquired camera has no view");
    Near(view,.2f,"reacquiring the same camera retained its expired base");
    Near(f.Step(5,6,4,2010,{1,0},.2f,0,0,true).yaw,0,"tracking discontinuity rotates motor");
    SurveillanceAngles a{};const float half=std::sqrt(.5f);
    Check(SurveillanceHeadAngles(0,std::sin(.2f),0,std::cos(.2f),0,a),"valid yaw rejected");Near(a.yaw,.4f,"head yaw direction wrong");
    Check(SurveillanceHeadAngles(half,0,0,half,.8f,a),"vertical head rejected");Near(a.yaw,.8f,"vertical head invents a yaw");Near(a.pitch,SurveillancePi*.5f,"head pitch direction wrong");
    Check(!SurveillanceHeadAngles(std::numeric_limits<float>::quiet_NaN(),0,0,1,0,a),"NaN tracking accepted");
    Check(!SurveillanceHeadAngles(0,0,0,0,0,a),"invalid quaternion accepted");
    f.Reset();Check(!f.View(5,6,4,2010,0,view),"released camera retained heading");
    SurveillanceFollow sniper;
    auto aligned=sniper.Step(7,8,9,1000,{.4f,-.2f},.7f,.1f,.05f,true,true,.6f,.03f);
    Near(aligned.yaw,.4f,"sniper did not align initial head yaw");
    Near(aligned.pitch,-.83f,"sniper retained its authored barrel elevation");
    Check(sniper.View(7,8,9,1000,.1f,view),"sniper view unavailable");
    Near(view+.4f,.7f+.05f+aligned.yaw,"sniper alignment fed back into rendered yaw");
    const auto held=sniper.Step(7,8,9,1010,{.4f,-.2f},1.15f,.1f,0,true,true,-.2f,0);
    Near(held.yaw,0,"sniper repeated its initial yaw alignment");Near(held.pitch,0,"sniper repeated its initial pitch alignment");
    const auto moved=sniper.Step(7,8,9,1020,{.5f,-.3f},1.15f,.1f,0,true,true,-.2f,0);
    Near(moved.yaw,.1f,"sniper no longer follows head yaw");Near(moved.pitch,-.1f,"sniper no longer follows head pitch");
    sniper.Reset();
    Near(sniper.Step(7,8,9,1000,{.4f,-.2f},.7f,.1f,0,false,true,.6f,0).yaw,0,"sniper acquired while menu had input");
    Near(sniper.Step(7,8,9,1010,{.4f,-.2f},.7f,.1f,0,true,true,.6f,0).pitch,-.8f,"sniper lost alignment while awaiting input");
    std::cout<<"PASS 30-144 FPS, slow/fast HMD, moving owner, native motor limit, mouse input, repeated samples, pause, reset, switch, wrap, tracking loss\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
