#include "Runtimes/NativeBodyHeading.hpp"
#include "Runtimes/BodyYawFollower.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using cvr::body::NativeBodyHeading;
namespace {
void near(float actual,float expected,const char* message) {
    if(!std::isfinite(actual) || std::abs(NativeBodyHeading::Wrap(actual-expected))>2e-5f) {
        std::fprintf(stderr,"%s: %.8f != %.8f\n",message,actual,expected);std::exit(1);
    }
}
void require(bool value,const char* message) { if(!value){std::puts(message);std::exit(1);} }
}
int main() {
    constexpr float rad=.0174532925199f;
    NativeBodyHeading frame;
    require(frame.Observe(1,1,130*rad),"initial native orientation rejected");
    // A real native adapter accumulates the motion request; one 60-degree
    // body request remains in that persistent quaternion without a repeated write.
    float nativeBody=130*rad;
    nativeBody+=60*rad;frame.Commit(nativeBody,0);
    for(int i=0;i<10000;++i) {
        frame.Observe(1,1,nativeBody);
        near(frame.View(),130*rad,"idle/skipped requests changed the view reference");
        near(frame.Offset(),60*rad,"accepted body turn disappeared");
    }
    // Turning input moves both frames; physical body following moves only one.
    frame.Observe(1,1,nativeBody);nativeBody-=30*rad;frame.Commit(nativeBody,-30*rad);
    near(frame.View(),100*rad,"native snap did not move tracking");
    near(frame.CentreYaw(),130*rad,"snap orbited the calibrated optical centre");
    near(frame.Offset(),60*rad,"native snap changed body/head disagreement");
    // Failed consumption: do not commit the plan. Retry from native feedback.
    cvr::roomscale::BodyYawFollower solver;solver.SetEnabled(true);
    solver.Observe(frame.Offset(),1);solver.Step(100*rad,0,1000000,1);
    solver.Observe(frame.Offset(),1);
    near(solver.Offset(),60*rad,"unconsumed request accumulated fictitious rotation");
    near(frame.View(),100*rad,"failed consumption moved the view");
    // An authored body turn during a controller transition is not view input.
    nativeBody+=100*rad;frame.Observe(1,1,nativeBody);
    near(frame.View(),100*rad,"controller transition dragged the view");
    near(frame.Offset(),160*rad,"authored body turn not observed");
    // Native forward already agrees with the graph's inverse entity transform.
    const float vx=-std::sin(frame.Body()),vy=std::cos(frame.Body());
    near(std::cos(frame.Body())*vx+std::sin(frame.Body())*vy,0,"forward became strafe");
    near(-std::sin(frame.Body())*vx+std::cos(frame.Body())*vy,1,"forward became backward");
    frame.Observe(1,2,nativeBody);
    near(frame.CentreYaw(),130*rad,"recenter orbited the calibrated optical centre");
    near(frame.Offset(),0,"explicit recenter retained previous origin offset");
    require(!frame.Matches(2,2),"another puppet inherited tracking");
    frame.Observe(2,2,-2);near(frame.View(),-2,"new puppet not rebased");
    require(!frame.Observe(2,2,std::numeric_limits<float>::quiet_NaN()),"invalid native heading accepted");
    near(frame.View(),-2,"invalid input poisoned tracking");
    frame.Reset();require(!frame.Matches(2,2),"released scope retained tracking");
    std::puts("PASS native body heading: retained turns, dropped requests, snap, native motion, forward, lifecycle");
}
