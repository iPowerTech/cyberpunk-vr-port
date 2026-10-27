#pragma once
#include "Runtimes/HybridBodyYaw.hpp"
#include "Runtimes/LookDownCone.hpp"
#include "Core/LiveControls.hpp"

struct HybridRun {
    TrackedBodyYaw tracker;
    HybridBodyYaw policy;
    cvr::roomscale::BodyYawFollower follower;
    uint64_t seq=0,time=1000000,origin=1;
    float hz,total=0,maxStep=0;
    HybridCommand last{};
    explicit HybridRun(float rate):hz(rate){tracker.Reset(0,false);follower.SetEnabled(true);}
    float Step(const Pose& pose,float body,float head,float down=0,float bend=0,bool hands=true){
        const auto before=follower.Offset();
        auto f=Frame(pose,body,head,++seq,time+=uint64_t(1000000/hz),-down*deg);
        f.origin=origin;f.tracked[0]=hands;
        auto estimate=tracker.Update(f);
        last=policy.Step({head,down,bend*deg,before,10,estimate,true,estimate.valid&&tracker.Ready(),time,origin});
        const float delta=follower.Step(last.targetYaw,0,time,origin);total+=delta;
        maxStep=std::max(maxStep,std::abs(delta)/deg);
        Check(Error(total-follower.Offset(),0)<.002f,"hybrid rotation leaked into view cancellation");
        return delta;
    }
    void Hold(const Pose& pose,float body,float head,float down=0,float bend=0,float seconds=.5f){
        for(int i=0;i<int(hz*seconds);++i)Step(pose,body,head,down,bend);
    }
};
void HybridPolicy(){
    LiveControls defaults{};
    Check(defaults.xrHybridBodyRotation==1 && defaults.xrBodyRotationMode==3 && defaults.xrBodyFreeLookDeg==10,"hybrid defaults are not active / 10 deg");
    Check(RotationModeFromFlags(0,0,1)==RotationMode::Hybrid,"hybrid requires legacy master");
    Check(RotationModeFromFlags(1,1,1)==RotationMode::Tracked,"explicit tracked-only request lost priority");
    Check(RotationModeFromFlags(1,0,0)==RotationMode::HeadCone && RotationModeFromFlags(0,0,0)==RotationMode::Off,"legacy/off modes lost");
    HybridRun r(90);r.Hold(poses[0],0,0);
    for(float head:{-9.f,9.f}){r.Hold(poses[0],0,head*deg);Check(Error(r.follower.Offset(),0)<.01f,"head moved body inside 10 deg cone");}
    r.Hold(poses[0],0,25*deg,0,0,1);Check(Error(r.follower.Offset(),25*deg)<.05f,"upright cone did not follow head");
    const float prior=r.follower.Offset();r.Step(poses[0],0,25*deg,10);
    Check(r.last.tracked && r.last.coneDegrees==0 && Error(prior,r.follower.Offset())<.0001f,"look-down handover jumped or retained a cone");
    r.Hold(poses[0],0,-60*deg,45);Check(Error(r.follower.Offset(),prior)<.1f,"hybrid looked down but still followed head yaw");
    const float delta=r.Step(poses[0],0,-60*deg,8);
    Check(!r.last.tracked && r.last.coneDegrees==10 && std::abs(delta)<.00001f,"standing up injected an angle at handover");
    r.Hold(poses[0],0,-60*deg,0,0,1.5f);Check(Error(r.follower.Offset(),-60*deg)<=10.01f,"head cone did not resume");
    Check(r.maxStep<=720/90.f+.001f,"hybrid head handover exceeded its angular-rate limit");
}
void HybridTransitions(){int count=0;float worst=0;
    for(const auto& pose:poses)for(float hz:{30.f,45.f,90.f,144.f})for(float sign:{-1.f,1.f})for(int byBend=0;byBend<2;++byBend){
        HybridRun r(hz);r.Hold(pose,0,sign*45*deg,0,0,1.5f);
        const float entry=r.follower.Offset();
        Check(std::abs(r.Step(pose,0,sign*45*deg,byBend?0:30,byBend?15:0))<.0001f,"source switch added a body step");
        r.Hold(pose,0,sign*80*deg,byBend?0:45,byBend?15:0);
        Check(Error(r.follower.Offset(),entry)<.2f,"independent head turn changed hybrid tracked heading");
        for(int i=1;i<=int(hz);++i){float turn=sign*35*deg*i/hz;r.Step(pose,turn,sign*80*deg+turn,byBend?0:45,byBend?15:0);}
        r.Hold(pose,sign*35*deg,sign*115*deg,byBend?0:45,byBend?15:0);
        const float error=Error(r.follower.Offset(),entry+sign*35*deg);worst=std::max(worst,error);
        Check(error<1,"coherent torso turn was lost after hybrid handover");
        Check(std::abs(r.Step(pose,sign*35*deg,sign*115*deg,0,0))<.0001f,"tracked-to-head handover jumped");
        r.Hold(pose,sign*35*deg,sign*115*deg,0,0,1.5f);
        Check(Error(r.follower.Offset(),sign*115*deg)<=10.01f,"head mode did not recover after hybrid");
        Check(r.maxStep<=720/hz+.01f,"hybrid source switch exceeded angular-rate limit");++count;
    }
    std::cout<<count<<" hybrid head/bend/hand-pose transitions; worst tracked turn error "<<worst<<" deg\n";
}
void HybridBoundary(){int changes=0;HybridRun r(90);r.Hold(poses[0],0,0);bool prior=false;
    auto feed=[&](float down,float bend){r.Step(poses[0],0,0,down,bend);if(r.last.tracked!=prior){++changes;prior=r.last.tracked;}};
    for(int i=0;i<1000;++i)feed(9+.8f*std::sin(float(i)),0);
    Check(changes==0,"sub-threshold pitch activated hybrid");feed(10,0);
    for(int i=0;i<1000;++i)feed(9+.8f*std::sin(float(i)),0);
    Check(changes==1 && r.last.tracked,"pitch boundary chattered");feed(8,0);Check(changes==2,"pitch release threshold ignored");
    feed(0,5);for(int i=0;i<1000;++i)feed(0,4+.8f*std::sin(float(i)));
    Check(changes==3 && r.last.tracked,"bend boundary chattered");feed(11,2);Check(changes==3,"clearing one cue cancelled another active cue");
    feed(0,3);Check(changes==4,"bend release threshold ignored");
    std::cout<<"3000 boundary-jitter samples; exactly four expected phase changes\n";
}
void HybridRecovery(){
    HybridRun r(90);r.Hold(poses[0],0,40*deg,0,0,1);
    r.Hold(poses[0],0,40*deg,35);const float before=r.follower.Offset();
    for(int i=0;i<90;++i)r.Step(poses[0],70*deg,110*deg,35,0,false);
    Check(Error(before,r.follower.Offset())<.001f,"missing controller fell back to head yaw");
    r.Hold(poses[0],70*deg,110*deg,35,0,1);
    Check(Error(before,r.follower.Offset())<.001f,"tracking reacquisition realigned the body absolutely");
    for(int i=1;i<=90;++i)r.Step(poses[0],(70+20*i/90.f)*deg,(110+20*i/90.f)*deg,35);
    r.Hold(poses[0],90*deg,130*deg,35);
    Check(Error(r.follower.Offset(),before+20*deg)<.2f,"relative turn after recovery lost");
    const auto old=r.policy.Step({0,30,0,r.follower.Offset(),10,{},true,false,r.time,r.origin});
    Check(Error(old.targetYaw,r.follower.Offset())<.001f,"duplicate stamp changed heading");
    r.policy.Suspend();r.tracker.Suspend();r.Hold(poses[0],-40*deg,20*deg,35);
    Check(Error(r.follower.Offset(),before+20*deg)<.2f,"suspend/resume lost alignment");
    HybridBodyYaw p;HybridInput in{0,30,0,.8f,10,{.5f,1,true},true,true,1000,1};p.Step(in);
    in.origin=2;in.stampUs=2000;in.estimate={0,1,true};
    Check(Error(p.Step(in).targetYaw,0)<.001f,"origin reset retained the old correction");
    in.headDownDegrees=std::numeric_limits<float>::quiet_NaN();in.bodyOffset=.2f;in.stampUs=3000;
    Check(Error(p.Step(in).targetYaw,.2f)<.001f,"invalid posture did not hold body");
}
void HybridPosture(){
    cvr::body::BendTracker bend;HybridBodyYaw hybrid;bool activated=false;uint64_t stamp=1000000;
    for(int i=0;i<90;++i){float angle=45*deg*i/89;std::array<float,3> position{0,BendReach*(std::cos(angle)-1),-BendReach*std::sin(angle)};
        const auto measured=bend.Update(position,{0,0,0,1},1);
        const auto cmd=hybrid.Step({0,0,measured.angle,0,10,{0,1,true},true,true,stamp+=11111,1});
        activated|=cmd.tracked;
    }Check(activated,"physical lean with level HMD did not select tracking");
    for(int i=0;i<90;++i){float angle=45*deg*(1-i/89.f);auto measured=bend.Update({0,BendReach*(std::cos(angle)-1),-BendReach*std::sin(angle)},{0,0,0,1},1);
        hybrid.Step({0,0,measured.angle,0,10,{0,1,true},true,true,stamp+=11111,1});}
    Check(!hybrid.Tracked(),"standing up did not restore normal head cone");
    TrackedBodyYaw relative;relative.Reset(50*deg,false);
    for(int i=0;i<90;++i){auto f=Frame(poses[0],-60*deg,-60*deg,i+1,1000000+i*11111);Check(Error(relative.Update(f).yaw,50*deg)<.001f,"relative acquisition snapped toward low hands");}
    Check(relative.Ready(),"relative tracker never warmed");
}
