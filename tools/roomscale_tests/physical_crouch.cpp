#include "Runtimes/PhysicalCrouch.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace cvr::body;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Near(float a,float b,float e,const char* message){Check(std::abs(a-b)<e,message);}
int main() {
 try {
    unsigned cases=0;
    for(float hz:{30.f,90.f,144.f})for(float hinge:{0.f,.35f,.70f,1.05f})for(float squat:{0.f,.35f}) {
        BendTracker bend;PhysicalCrouch crouch;uint64_t now=1000000;
        float depth=0;BendSample sample{};
        for(int i=0;i<int(hz*2);++i) {
            const float t=std::min(i/hz,1.f),a=hinge*t;
            sample=bend.Update({0,-squat*t+BendReach*(std::cos(a)-1),-BendReach*std::sin(a)}, {0,0,0,1},1);
            depth=CrouchDepth(bend.NeckDrop(),sample);
            now+=uint64_t(1e6/hz);crouch.Update(depth,.25f,now,1,123,true);
        }
        Near(depth,squat,.025f,"bend and crouch height were not separated");
        Check(crouch.Active()==(squat>0),"posture classification mismatch");++cases;
    }
    // Looking down while lowering the knees must not use the reach fallback to
    // explain away the entire squat. Include the actual eye/neck lever trajectory.
    for(float down:{0.f,.7f,1.0f}) {
        BendTracker bend;PhysicalCrouch crouch;float depth=0;uint64_t now=1000000;
        for(int i=0;i<240;++i) {
            const float t=std::min(i/120.f,1.f),a=-down*t,c=std::cos(a),s=std::sin(a);
            const float y=c*.08f+s*.15f-.08f,z=s*.08f-c*.15f+.15f;
            const auto b=bend.Update({0,-.4f*t+y,z},{std::sin(a*.5f),0,0,std::cos(a*.5f)},1);
            depth=CrouchDepth(bend.NeckDrop(),b);
            crouch.Update(depth,.25f,now+=8333,1,123,true);
        }
        Check(crouch.Active(),"downward gaze stole a physical crouch");++cases;
    }
    PhysicalCrouch c;uint64_t now=1000000;
    for(int i=0;i<500;++i)c.Update(.24f+.008f*std::sin(float(i)),.25f,now+=10000,1,123,true);
    Check(!c.Active(),"threshold noise entered crouch");
    for(int i=0;i<20;++i)c.Update(.4f,.25f,now+=10000,1,123,true);
    Check(c.Active(),"stable crouch not accepted");
    for(int i=0;i<100;++i)c.Update(.20f+.015f*std::sin(float(i)),.25f,now+=10000,1,123,true);
    Check(c.Active(),"height hysteresis chattered");
    for(int i=0;i<20;++i)c.Update(0,.25f,now+=10000,1,123,true);
    Check(!c.Active(),"standing did not release crouch");++cases;
    for(int reset=0;reset<4;++reset) {
        for(int i=0;i<20;++i)c.Update(.4f,.25f,now+=10000,1,123,true);
        Check(c.Active(),"lifecycle setup");
        if(reset==0)c.Update(.4f,.25f,now+=10000,1,123,false);
        if(reset==1)c.Update(.4f,.25f,now+=10000,2,123,true);
        if(reset==2)c.Update(.4f,.25f,now+=10000,1,456,true);
        if(reset==3)c.Update(.4f,.25f,now+=300000,1,123,true);
        Check(!c.Active(),"tracking/context reset retained crouch");c.Reset();++cases;
    }
    c.Update(std::numeric_limits<float>::quiet_NaN(),.25f,now,1,123,true);
    Check(!c.Active(),"invalid tracking crouched");
    std::cout<<cases<<" physical posture, priority, hysteresis and lifecycle scenarios passed\n";
 } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
