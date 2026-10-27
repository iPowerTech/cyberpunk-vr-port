#include "Camera/ExternalViewMath.hpp"
#include "Camera/ExternalViewLease.hpp"
#include "Utils/ThreadScope.hpp"
#include <thread>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
using namespace cvr::camera;
namespace {
void Check(bool yes,const char* why){if(!yes){std::cerr<<why<<'\n';std::exit(1);}}
void Near(double a,double b,double tolerance,const char* why){Check(std::abs(a-b)<=tolerance,why);}
XrQuaternionf Heading(float angle){return {0,0,std::sin(angle*.5f),std::cos(angle*.5f)};}
ExternalViewPose Base(){return {{430408419,-94501450,14550412},Heading(.7f)};}
XrPosef Head(float yaw,float pitch,float x=0,float y=0,float z=0) {
    return {MultiplyQuat({0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)},
                          {std::sin(pitch*.5f),0,0,std::cos(pitch*.5f)}),{x,y,z}};
}
void QuatNear(XrQuaternionf a,XrQuaternionf b){
    double sum=0;for(int i=0;i<4;++i)sum+=double((&a.x)[i])*(&b.x)[i];
    Near(std::abs(sum),1,2e-6,"head rotation composed twice or in wrong axes");
}
void Neutral(){auto base=Base();ExternalViewPose out{};Check(ComposeExternal(base,Head(0,0),1,0,out),"neutral rejected");Check(base.position==out.position,"neutral moved the authored camera");QuatNear(base.rotation,out.rotation);}
void Tracking(){
    auto base=Base();for(float yaw:{-2.f,0.f,1.3f})for(float pitch:{-1.f,0.f,.8f}) {
        ExternalViewPose out{};const auto head=Head(yaw,pitch,.12f,-.08f,-.1f);
        Check(ComposeExternal(base,head,1,0,out),"tracked camera rejected");
        QuatNear(out.rotation,MultiplyQuat(base.rotation,{head.orientation.x,-head.orientation.z,head.orientation.y,head.orientation.w}));
        Near(double(out.position[0]-base.position[0])/131072,.12*std::cos(.7)-.1*std::sin(.7),1e-5,"lean right/forward not in camera yaw frame");
        Near(double(out.position[2]-base.position[2])/131072,-.08,1e-5,"lean height changed with camera pitch");
    }
}
void Stereo(){for(float scale:{.5f,1.f,2.f})for(float yaw:{-2.f,0.f,1.3f})for(int swap:{-1,1}) {
    auto base=Base();ExternalViewPose main{};const float half=.032f*scale;
    Check(ComposeExternal(base,Head(yaw,.4f,.05f,0,.1f),scale,half*swap,main),"MAIN rejected");
    auto other=main;Check(ExternalEye(other,-2*half*swap),"other eye rejected");
    const auto right=RotateVector(main.rotation,{1,0,0});double distance=0;
    for(int k=0;k<3;++k){const double d=double(other.position[k]-main.position[k])/131072;
        Near(d,-2*half*swap*(&right.x)[k],1e-5,"eye sign or double IPD mismatch");distance+=d*d;}
    Near(std::sqrt(distance),2*half,1e-5,"eye separation changed with rotation");QuatNear(main.rotation,other.rotation);
}}
void Blend(){for(int i=0;i<=20;++i){
    const float t=float(i)/20;auto a=Base(),b=a;b.position[0]+=400000;b.rotation=Heading(-1);
    ExternalViewPose left{},right{},out{};const auto head=Head(.3f,-.2f);
    Check(ComposeExternal(a,head,1,-.032f,left)&&ComposeExternal(b,head,1,-.032f,right),"blend sources invalid");
    const ExternalViewInput inputs[]{{left,1-t},{right,t}};
    Check(BlendExternal(inputs,out),"valid transition rejected");
    Near(out.position[0],double(left.position[0])*(1-t)+double(right.position[0])*t,.6,"transition changed weighted position");
    auto q=NlerpQuat(left.rotation,right.rotation,t);QuatNear(out.rotation,q);
    auto opposite=out;Check(ExternalEye(opposite,.064f),"transition eye failed");
}}
void Invalid(){auto base=Base(),output=base;auto broken=base;broken.rotation={0,0,0,0};
    Check(!ComposeExternal(broken,Head(0,0),1,0,output)&&output.position==base.position,"invalid quaternion changed output");
    const ExternalViewInput input[]{{base,.3f},{base,.3f}};Check(!BlendExternal(input,output),"invalid blend weights accepted");
    const ExternalViewInput one[]{{base,.3f}};Check(BlendExternal(one,output),"native single-camera weight was normalized incorrectly");
    auto p=base;Check(!ExternalEye(p,std::numeric_limits<float>::quiet_NaN()),"invalid eye displacement accepted");
}
void FixedPoint(){auto base=Base();base.position={2147483600,0,0};base.rotation={0,0,0,1};auto saved=base;
    Check(!ShiftExternal(base,{1,0,0})&&base.position==saved.position,"overflow partially wrote camera position");
    base=Base();Check(ShiftExternal(base,{.00002f,0,0}),"small shift rejected");
    Check(base.position[0]-Base().position[0]==3,"large world position lost a submillimetre camera change");
}
void Repeated(){auto base=Base();ExternalViewPose expected{},out{};auto head=Head(.5f,-.4f,.1f,.1f,.1f);
    Check(ComposeExternal(base,head,1,-.032f,expected),"setup failed");
    for(int i=0;i<10000;++i){Check(ComposeExternal(base,head,1,-.032f,out),"repeat failed");Check(out.position==expected.position,"HMD translation accumulated across reads");QuatNear(out.rotation,expected.rotation);}
}
void SourceUnchanged(){auto base=Base(),saved=base;ExternalViewPose out{};Check(ComposeExternal(base,Head(1,.6f),1,-.032f,out),"setup failed");Check(std::memcmp(&base,&saved,sizeof(base))==0,"changed native orbit source instead of temporary setup");}
void Lease(){
    ExternalViewLease<int> lease;ExternalViewKey key{100,200,3};int out=0;
    Check(!lease.Read(key,1000000,out),"uninitialized external lease accepted");
    lease.Publish(42,key,1000000);
    Check(lease.Read(key,1100001,out)&&out==42,"slow director tick fell back to player camera");
    Check(lease.Read(key,1249999,out),"MAIN snapshot missing");
    Check(lease.Read(key,100000000,out)&&out==42,"missing MAIN update incorrectly selected FPP");
    Check(!lease.Read({100,201,3},1100000,out),"recreated VRCAM inherited old camera");
    Check(!lease.Read({101,200,3},1100000,out),"new player inherited old camera");
    Check(!lease.Read({100,200,4},1100000,out),"tracking reset reused stale pose");
    Check(!lease.Read(key,999999,out),"backward clock accepted");
    lease.Clear();Check(!lease.Read(key,1100000,out),"explicit FPP transition retained TPP");
}
void AimCentre(){
    for(float sign:{-1.f,1.f})for(float half:{.025f,.0325f,.04f})
    for(float yaw:{-2.f,0.f,2.f})for(float pitch:{-1.f,0.f,1.f}) {
        const auto head=Head(yaw,pitch,.1f,-.15f,.08f);ExternalViewPose eye{},centre{};
        Check(ComposeExternal(Base(),head,1,sign*half,eye)&&ComposeExternal(Base(),head,1,0,centre),"aim setup failed");
        Check(ExternalEye(eye,-sign*half),"aim centring failed");
        Check(eye.position==centre.position,"weapon aim inherited an eye offset or added HMD translation twice");
        QuatNear(eye.rotation,centre.rotation);
    }
}
void AimScope(){
    struct Frame {int id;};using Scope=cvr::ThreadScope<Frame>;Frame shot{10},nested{20};
    Check(!Scope::Get(),"aim scope leaked before entry");
    {Scope scope(&shot);Check(Scope::Get()->id==10,"shot snapshot missing");
        std::thread worker([]{Check(!Scope::Get(),"player aim leaked to another camera/physics thread");});worker.join();
        {Scope inactive(nullptr);Check(!Scope::Get(),"unrelated nested vehicle inherited player aim");}
        Check(Scope::Get()->id==10,"inactive call lost outer aim");
        try {Scope active(&nested);Check(Scope::Get()->id==20,"nested shot did not bind its own snapshot");throw 1;}
        catch(int){}
        Check(Scope::Get()->id==10,"exception leaked the nested aim snapshot");
    }
    Check(!Scope::Get(),"native camera getter kept player override after targeting returned");
}
void AimDirection(){
    for(float range:{2.f,20.f,300.f,1000.f})for(float side:{-3.f,0.f,3.f}) {
        const XrVector3f origin{3200+side,-400,140},target{3200,-400+range,142};XrVector3f direction{};
        Check(AimAtPoint(origin,target,direction),"valid camera target rejected");
        const double dx=double(target.x)-origin.x,dy=double(target.y)-origin.y,dz=double(target.z)-origin.z;
        const double distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        Near(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z,1,2e-6,"cannon speed changed with target distance");
        Near(origin.x+direction.x*distance,target.x,.002,"cannon did not converge horizontally");
        Near(origin.z+direction.z*distance,target.z,.002,"cannon ignored camera elevation");
    }
    XrVector3f out{1,2,3};Check(!AimAtPoint({0,0,0},{0,0,0},out),"coincident muzzle and target accepted");
    Check(!AimAtPoint({0,0,0},{0,std::numeric_limits<float>::quiet_NaN(),0},out),"invalid target accepted");
    Check(out.x==1 && out.y==2 && out.z==3,"invalid target partially changed direction");
}
void PanzerForward(){
    for(float yaw:{-3.1415f,-1.f,0.f,1.f,3.1415f})for(float pitch:{-.9f,0.f,.9f}) {
        auto q=MultiplyQuat(Heading(yaw),XrQuaternionf{std::sin(pitch*.5f),0,0,std::cos(pitch*.5f)});
        XrVector3f forward{};Check(PlanarExternalForward(q,forward),"ordinary tank gaze rejected");
        Near(forward.x,-std::sin(yaw),2e-6,"tank turn sign wrong");Near(forward.y,std::cos(yaw),2e-6,"pitch changed tank heading");
        Check(forward.z==0,"head pitch tilted tank target");
    }
    XrVector3f out{1,2,3};Check(!PlanarExternalForward({0,0,0,0},out),"invalid head steered tank");
    Check(!PlanarExternalForward({.70710678f,0,0,.70710678f},out),"vertical gaze has an arbitrary yaw");
    Check(out.x==1&&out.y==2&&out.z==3,"invalid gaze partially changed target");
}
}
int main(int argc,char** argv){if(argc!=2)return 2;std::string name=argv[1];
 if(name=="neutral")Neutral();else if(name=="tracking")Tracking();else if(name=="stereo")Stereo();else if(name=="blend")Blend();
 else if(name=="invalid")Invalid();else if(name=="fixed_point")FixedPoint();else if(name=="repeated")Repeated();else if(name=="source_unchanged")SourceUnchanged();else if(name=="lease")Lease();else if(name=="panzer_forward")PanzerForward();else if(name=="aim_centre")AimCentre();else if(name=="aim_scope")AimScope();else if(name=="aim_direction")AimDirection();else return 2;
 std::cout<<"PASS "<<name<<'\n';}
