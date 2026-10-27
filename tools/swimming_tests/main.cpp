#include "Runtimes/SwimmingGesture.hpp"
#include "Hooks/InputPacketSequence.hpp"
#include "Runtimes/SwimmingMovement.hpp"
#include "Runtimes/LookDownCone.hpp"
#include "Runtimes/RoomscaleMovement.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <random>
using namespace cvr::swimming;
namespace {
void Check(bool ok,const char* message) { if(!ok) { std::cerr<<"FAIL: "<<message<<'\n';std::exit(1); } }
struct Rig {
    Breaststroke detector;
    Sample sample{{},{},{0,0,0,1},0,1000000,1,true};
    int strokes{};
    uint64_t firstForward{},firstStroke{},firstBoost{};
    float peak{};
    bool boosted{};
    Motion Tick(XrVector3f left,XrVector3f right,uint64_t step=20000,bool allowed=true) {
        sample.hands[0]=left;sample.hands[1]=right;++sample.sequence;sample.stampUs+=step;
        const auto out=detector.Update(sample,sample.stampUs,allowed);strokes+=out.stroke;
        if(out.forward>.1f && !firstForward)firstForward=sample.stampUs;
        if(out.stroke && !firstStroke)firstStroke=sample.stampUs;
        if(out.boost && !firstBoost)firstBoost=sample.stampUs;
        peak=std::max(peak,out.forward);boosted|=out.boost;
        Check(out.forward>=0 && out.forward<=1,"input axis outside native range");return out;
    }
    void Reach() { for(int i=0;i<7;++i)Tick({-.1f,-.30f,-.45f},{.1f,-.30f,-.45f}); }
    void Stroke(int mode=0) {
        Reach();
        for(int i=1;i<=10;++i) {
            const float t=float(i)/10;
            auto left=XrVector3f{-.1f-.15f*t,-.3f,-.45f+.06f*t};
            auto right=XrVector3f{.1f+.15f*t,-.3f,-.45f+.06f*t};
            if(mode==1)right={.1f,-.3f,-.45f};
            if(mode==2) { left.x=-.1f;right.x=.1f; }
            Tick(left,right,mode==3 ? 140000:20000);
        }
        for(int i=1;i<=12;++i) {
            const float t=float(i)/12;
            auto left=XrVector3f{-.25f+.02f*t,-.3f-.12f*t,-.39f+.29f*t};
            auto right=XrVector3f{.25f-.02f*t,-.3f-.12f*t,-.39f+.29f*t};
            if(mode==1)right={.1f,-.3f,-.45f};
            if(mode==2) { left.x=-.1f;right.x=.1f; }
            Tick(left,right,mode==3 ? 140000:20000);
        }
    }
    void Recover() {
        for(int i=1;i<=15;++i) {
            const float t=float(i)/15;
            Tick({-.23f+.13f*t,-.42f+.12f*t,-.1f-.35f*t},{.23f-.13f*t,-.42f+.12f*t,-.1f-.35f*t});
        }
    }
};
void Stroke() {
    Rig r;r.Stroke();Check(r.strokes==1,"one complete breaststroke was not recognized exactly once");
    Check(r.detector.Update(r.sample,r.sample.stampUs,true).forward>.9f,"stroke did not generate forward propulsion");
    r.Recover();r.Stroke();Check(r.strokes==2,"recovery did not rearm the next stroke");
    float forward=1;
    for(int i=0;i<80;++i)forward=r.Tick({-.23f,-.42f,-.1f},{.23f,-.42f,-.1f}).forward;
    Check(r.strokes==2 && forward==0,"holding hands back repeated or latched propulsion");
}
void Rejection() {
    for(int mode:{1,2,3}) { Rig r;r.Stroke(mode);Check(r.strokes==0 && r.peak==0,"one-handed/common-motion/slow gesture propelled the player"); }
    Rig r;r.Reach();
    for(int i=0;i<200;++i)r.Tick({-.1f,-.30f,-.45f},{.1f,-.30f,-.45f});
    Check(r.strokes==0,"stationary hands propelled the player");
    r.Stroke();Check(r.strokes==1,"waiting with arms extended lost the first deliberate stroke");
}
void HeadMotion() {
    Rig r;const XrVector3f hands[2]={{-.12f,-.3f,-.45f},{.12f,-.3f,-.45f}};
    for(int i=0;i<200;++i) {
        const float t=float(i)*.02f;
        r.sample.head={.12f*std::sin(t),.08f*std::cos(t),.10f*std::sin(t*.7f)};
        r.sample.orientation=MultiplyQuat({0,std::sin(t*.25f),0,std::cos(t*.25f)},
            {std::sin(t*.12f),0,0,std::cos(t*.12f)});
        XrVector3f reconstructed[2];
        for(int h=0;h<2;++h) {
            const auto local=RotateVector(ConjugateQuat(r.sample.orientation),Sub(hands[h],r.sample.head));
            reconstructed[h]=HandInTrackingSpace(r.sample.head,r.sample.orientation,local);
            Check(Length(Sub(reconstructed[h],hands[h]))<.00001f,"coherent HMD-local reconstruction moved a stationary hand");
        }
        r.Tick(reconstructed[0],reconstructed[1]);
    }
    Check(r.strokes==0,"head rotation/translation invented a swimming stroke");
    // Rotate/translate the whole tracking reference. The same physical stroke
    // remains recognizable; it is not hardwired to recenter forward.
    Rig source,turned;const XrQuaternionf yaw{0,std::sin(.7f),0,std::cos(.7f)};
    turned.sample.orientation=yaw;turned.sample.head={1,.2f,-2};
    auto step=[&](XrVector3f l,XrVector3f rr) {
        source.Tick(l,rr);turned.Tick(HandInTrackingSpace(turned.sample.head,yaw,l),HandInTrackingSpace(turned.sample.head,yaw,rr));
    };
    for(int i=0;i<7;++i)step({-.1f,-.3f,-.45f},{.1f,-.3f,-.45f});
    for(int i=1;i<=20;++i) { float t=float(i)/20;step({-.1f-.15f*t,-.3f,-.45f+.32f*t},{.1f+.15f*t,-.3f,-.45f+.32f*t}); }
    Check(source.strokes==1 && turned.strokes==1,"tracking reference yaw changed stroke recognition");
}
void TrackingLoss() {
    Rig r;r.Stroke();const auto count=r.strokes;
    for(int i=0;i<100;++i)Check(!r.detector.Update(r.sample,r.sample.stampUs+1000+i,true).stroke,"repeated hand packet counted twice");
    Check(r.strokes==count,"polling rate changed stroke count");
    Check(r.detector.Update(r.sample,r.sample.stampUs+250001,true).forward==0,"stale hands retained forward input");
    r.Recover();r.Stroke();r.sample.origin++;
    Check(r.detector.Update(r.sample,r.sample.stampUs,true).forward==0,"recenter retained an old impulse");
    r.Recover();r.Stroke();r.sample.valid=false;
    Check(r.detector.Update(r.sample,r.sample.stampUs,true).forward==0,"tracking loss retained movement");
    r.sample.valid=true;r.Recover();r.Stroke();
    Check(r.detector.Update(r.sample,r.sample.stampUs,false).forward==0,"leaving water/menu/manual cancel retained movement");
}
void WaterGate() {
    Check(InWater(1)&&InWater(2),"surface or underwater swimming excluded");
    for(int state:{-1,0,3,4,99})Check(!InWater(state),"dry land or climbing accepted as swimming");
    Check(StateFresh(10,10,1000,1010,2),"fresh native swimming state rejected");
    Check(!StateFresh(10,11,1000,1010,2),"new player consumed previous player's water state");
    Check(!StateFresh(10,10,1000,251001,2),"stopped state publisher retained swimming controls");
    Check(!StateFresh(10,10,1000,999,2),"future state stamp accepted");
    Check(PresenceFresh(10,10,1000,1010,0,true),"diving transition lost water posture");
    Check(!StateFresh(10,10,1000,1010,0),"transition incorrectly enabled native swimming controls");
    Check(!PresenceFresh(10,10,1000,1010,0,false),"dry land retained water posture");
    Check(!PresenceFresh(10,11,1000,1010,0,true),"new player inherited old submersion");
    Check(!PresenceFresh(10,10,1000,251001,0,true),"stale submersion retained water posture");
}
void SprintToggle() {
    SprintButton b;
    Check(b.Update(true,0,1000),"normal swim did not receive the LShift/L3 equivalent");
    for(uint64_t t=2000;t<10000;t+=1000)Check(b.Update(true,1,t),"held boost generated a release/retoggle while fast");
    Check(!b.Update(false,1,10000),"ending propulsion left the synthetic button held");
    Check(!b.Update(true,1,11000),"new stroke toggled already-fast swimming off");
    Check(b.Update(true,0,12000),"game leaving fast swim did not permit acceleration again");
    Check(b.Update(true,1,13000),"fast feedback dropped the ongoing hold");
    Check(!b.Update(true,0,14000) && !b.Update(true,0,73000),"re-entry release gap was lost");
    Check(b.Update(true,0,74000),"fast swim could not re-enter after interruption");
    Check(!b.Update(false,-1,75000),"loss of swimming context stuck L3");
}
void InputPackets() {
    struct Pad { uint16_t buttons{};uint8_t lt{},rt{};int16_t lx{},ly{},rx{},ry{}; };
    static_assert(sizeof(Pad)==12);
    cvr::input::InputPacketSequence<Pad> packets;Pad pad{};
    const auto idle=packets.Publish(pad);
    pad.ly=32767;const auto moving=packets.Publish(pad);
    Check(moving!=idle,"forward axis did not publish a new input packet");
    Check(packets.Publish(pad)==moving,"unchanged input generated extra packets");
    pad.ly=12000;const auto slowing=packets.Publish(pad);
    pad.ly=0;const auto stopped=packets.Publish(pad);
    Check(slowing>moving && stopped>slowing,"glide/stop axis changes reused the real pad's fixed packet number");
    pad.buttons=0x40;Check(packets.Publish(pad)>stopped,"sprint button edge was lost");
}
void WaterPosture() {
    using namespace cvr::roomscale;
    cvr::body::BendTracker tracker;
    for(int cycle=0;cycle<3;++cycle)for(int i=0;i<80;++i) {
        const float t=float(i)/79,angle=-1.1f*t;
        const auto b=tracker.Update({.2f*t,-.7f*t,-.5f*t},{std::sin(angle*.5f),0,0,std::cos(angle*.5f)},1,true);
        Check(b.angle==0 && b.forward==0 && b.right==0,"water accumulated a land bend or hinge displacement");
        Check(BodyFreeLookCone(true,5,75*t,15,45,1.2f*t)==5,"water cone widened with gaze or bend");
        Check(BodyFreeLookCone(true,17,75*t,15,45,1.2f*t)==17,"water slider ignored");
    }
    const auto after=tracker.Update({0,0,0},{0,0,0,1},1);
    Check(after.angle==0 && after.forward==0 && after.right==0,"water posture leaked into land on exit");
    Check(BodyFreeLookCone(false,5,40,15,45)==45,"land look-down cone was changed");
}
void PitchedStroke() {
    for(float pitch:{-1.4f,-.7f,.7f,1.4f}) {
        Rig transformed;const XrQuaternionf q{std::sin(pitch*.5f),0,0,std::cos(pitch*.5f)};
        transformed.sample.orientation=q;
        auto tick=[&](XrVector3f l,XrVector3f r) { transformed.Tick(RotateVector(q,l),RotateVector(q,r)); };
        for(int i=0;i<7;++i)tick({-.1f,-.3f,-.45f},{.1f,-.3f,-.45f});
        for(int i=1;i<=25;++i) {
            const float t=float(i)/25;
            tick({-.1f-.15f*t,-.3f,-.45f+.32f*t},{.1f+.15f*t,-.3f,-.45f+.32f*t});
        }
        Check(transformed.strokes==1,"looking up/down prevented a forward breaststroke");
    }
}
void Ascent() {
    AscendStroke d;Sample p{};p.valid=true;p.origin=1;p.stampUs=1000000;
    int count=0;float strength=0;
    auto tick=[&](float y,bool allowed=true) {
        p.hands[0]={-.3f,y,-.25f};p.hands[1]={.3f,y,-.25f};++p.sequence;p.stampUs+=20000;
        const auto out=d.Update(p,p.stampUs,allowed);count+=out.stroke;strength=out.forward;
    };
    for(int i=0;i<10;++i)tick(-.15f);
    for(int i=1;i<=22;++i)tick(-.15f-.4f*i/22);
    Check(count==1 && strength>.9f,"two downward hand pushes did not produce one ascent");
    for(int i=0;i<65;++i)tick(-.55f);
    Check(count==1 && strength==0,"ascent stuck or repeated with held hands");
    for(int i=1;i<=25;++i)tick(-.55f+.4f*i/25);
    for(int i=0;i<10;++i)tick(-.15f);
    for(int i=1;i<=22;++i)tick(-.15f-.4f*i/22);
    Check(count==2,"recovery did not rearm ascent");
    tick(-.55f,false);Check(strength==0,"ascent remained active outside water");
    p.orientation={0,0,0,0};tick(-.15f);
    Check(strength==0,"invalid HMD orientation retained ascent");p.orientation={0,0,0,1};
    // One arm, jumps and a moving head do not propel upward.
    d.Reset();count=0;
    for(int i=0;i<80;++i) {
        p.hands[0]={-.3f,-.15f,-.25f};p.hands[1]={.3f,-.15f,-.25f};
        p.head={0,.25f*std::sin(i*.1f),0};++p.sequence;p.stampUs+=20000;
        Check(!d.Update(p,p.stampUs,true).stroke,"head movement invented ascent");
    }
    // Live regression: lowering the head AND both head-relative simulator
    // hands looked like a downstroke even though the arms did not move.
    d.Reset();p.head={};
    for(int i=0;i<70;++i) {
        const float drop=i<12 ? 0:std::min(.5f,float(i-12)*.02f);
        p.head={0,-drop,0};p.hands[0]={-.3f,-.15f-drop,-.25f};p.hands[1]={.3f,-.15f-drop,-.25f};
        ++p.sequence;p.stampUs+=20000;
        Check(!d.Update(p,p.stampUs,true).stroke,"whole-body descent invented an ascent stroke");
    }
}
void Direction() {
    constexpr float dt=.01f;
    for(float yaw:{0.0f,1.4f,-2.2f})for(float pitch:{-1.5707963f,-.7f,0.0f,.7f,1.5707963f}) {
        const XrQuaternionf q{std::sin(pitch*.5f),0,0,std::cos(pitch*.5f)};
        const auto h=RotateVector(q,{0,0,-1});
        constexpr float buoyancy=-.003f;
        const Vector native{0,.04f*std::cos(pitch),.04f*std::sin(pitch)+buoyancy};
        const auto moved=Redirect(native,h,yaw,true);
        const Vector propulsion{moved.x,moved.y,moved.z-buoyancy};
        Check(std::abs(std::sqrt(Dot(propulsion,propulsion))-.04f)<1e-6f,"head steering changed propulsion speed");
        const Vector expected{-std::sin(yaw)*std::cos(pitch),std::cos(yaw)*std::cos(pitch),std::sin(pitch)};
        Check(Dot(propulsion,expected)>.03999f,"propulsion did not follow the full HMD direction");
        Check(moved.z==native.z,"native pitch was added a second time");
        const auto feedback=cvr::roomscale::PhysicalVelocity({native.x,native.y},{moved.x-native.x,moved.y-native.y},
            {moved.x/dt,moved.y/dt},dt);
        Check(std::hypot(moved.x/dt-feedback.x-native.x/dt,moved.y/dt-feedback.y-native.y/dt)<1e-5f,
            "redirected heading fed back as another acceleration");
        const auto surface=Redirect(native,h,yaw,false);
        Check(surface.z==native.z,"surface steering altered native buoyancy");
    }
    // Captured from PID17376 at pitch +0.6: the native request already rose.
    const Vector captured{-.0107320854f,-.0129228402f,.0124363117f};
    const auto moved=Redirect(captured,{0,.56464247f,-.82533561f},2.49f,true);
    Check(moved.z==captured.z && std::abs(Dot(moved,moved)-Dot(captured,captured))<1e-8f,
        "live underwater motion gained a second vertical impulse");
}
void WaterFeedback() {
    using cvr::roomscale::Vec2;using cvr::roomscale::PhysicalVelocity;
    constexpr float dt=.01f;Vec2 nativeVelocity{},feedback{},resolved{};float travel=0;
    // One 12cm roomscale movement in water, then a stationary headset. The
    // previous broken path integrated the physical velocity on every tick.
    for(int i=0;i<250;++i) {
        nativeVelocity=resolved-feedback;
        const Vec2 native=nativeVelocity*dt,physical{i<12 ? .01f:0.0f,0};
        const auto moved=native+physical;resolved=moved*(1/dt);
        feedback=PhysicalVelocity(native,physical,resolved,dt);travel+=moved.x;
    }
    Check(std::abs(travel-.12f)<.00001f && Dot(resolved,resolved)<1e-9f,"roomscale produced water momentum after head stopped");
    const Vec2 native{.02f,0},physical{.01f,0},stopped{};
    Check(Dot(PhysicalVelocity(native,physical,stopped,dt),PhysicalVelocity(native,physical,stopped,dt))<1e-9f,
        "a wall produced reverse swimming feedback");
}
void TimedStroke(Rig& r,float duration,int hz=90,float pitch=0,float noise=0,unsigned seed=1) {
    const XrQuaternionf q{std::sin(pitch*.5f),0,0,std::cos(pitch*.5f)};
    r.sample.orientation=q;
    std::mt19937 rng(seed);std::normal_distribution<float> jitter(0,noise);
    const uint64_t step=uint64_t(1000000/hz);
    auto tick=[&](float t) {
        XrVector3f hands[2]={{-.1f-.15f*t,-.3f,-.45f+.32f*t},{.1f+.15f*t,-.3f,-.45f+.32f*t}};
        for(auto& p:hands){p.x+=jitter(rng);p.y+=jitter(rng);p.z+=jitter(rng);p=RotateVector(q,p);}
        return r.Tick(hands[0],hands[1],step);
    };
    for(int i=0;i<hz/5+1;++i)tick(0);
    const int frames=std::max(1,int(duration*hz));
    for(int i=1;i<=frames;++i)tick(float(i)/frames);
    for(int i=0;i<hz/4;++i)tick(1);
}
void EarlyResponse() {
    for(int hz:{30,45,90,120}) {
        Rig r;TimedStroke(r,.6f,hz);
        Check(r.strokes==1 && r.firstForward && r.firstStroke,"timed stroke did not produce one completed impulse");
        Check(r.firstForward+150000<r.firstStroke,"movement still waits for the completed stroke");
    }
}
void SpeedModes() {
    Rig slow;TimedStroke(slow,1.2f);
    Check(slow.strokes==1 && slow.peak>.99f && !slow.boosted,"gentle pull did not give normal native forward input");
    Rig fast;TimedStroke(fast,.28f);
    Check(fast.strokes==1 && fast.boosted && fast.firstBoost<=fast.firstStroke,"strong pull did not request fast swimming during the stroke");
}
void PartialStop() {
    Rig r;r.Reach();
    for(int i=1;i<=12;++i) {const float t=float(i)/12;r.Tick({-.1f-.06f*t,-.3f,-.45f+.09f*t},{.1f+.06f*t,-.3f,-.45f+.09f*t});}
    Check(r.peak>.1f && r.strokes==0,"partial confirmed pull did not start early");
    Motion last{};
    for(int i=0;i<25;++i)last=r.Tick({-.16f,-.3f,-.36f},{.16f,-.3f,-.36f});
    Check(last.forward==0 && !last.boost && r.strokes==0,"aborted partial pull received a full glide");
}
void SpeedTransitions() {
    Rig r;TimedStroke(r,.28f);Check(r.boosted,"fast setup failed");
    // Return while the previous glide is active; the next gentle stroke must
    // remove its boost request without relying on a long stationary timeout.
    const uint64_t fastEnd=r.firstStroke+1150000;
    for(int i=1;i<=9;++i) {const float t=float(i)/9;r.Tick({-.25f+.15f*t,-.3f,-.13f-.32f*t},{.25f-.15f*t,-.3f,-.13f-.32f*t});}
    for(int i=0;i<6;++i)r.Tick({-.1f,-.3f,-.45f},{.1f,-.3f,-.45f});
    bool normalDuringOldGlide=false;
    for(int i=1;i<=55;++i) {
        const float t=float(i)/55;
        const auto out=r.Tick({-.1f-.15f*t,-.3f,-.45f+.32f*t},{.1f+.15f*t,-.3f,-.45f+.32f*t});
        normalDuringOldGlide|=r.sample.stampUs<fastEnd && out.forward>.1f && !out.boost;
    }
    const auto out=r.detector.Update(r.sample,r.sample.stampUs,true);
    Check(r.strokes==2 && out.forward>.9f && !out.boost,"gentle second stroke retained the previous fast intent");
    Check(normalDuringOldGlide,"boost dropped only after the previous glide expired");
}
void EarlyRejection() {
    Rig r;r.Reach();
    for(int i=0;i<1500;++i) {
        const float noise=.003f*std::sin(i*1.73f);
        const auto out=r.Tick({-.1f+noise,-.3f,-.45f-noise},{.1f-noise,-.3f,-.45f+noise},11111);
        Check(out.forward==0 && !out.boost,"stationary tracker jitter triggered early propulsion");
    }
    Rig tiny;tiny.Reach();
    for(int i=0;i<500;++i) {
        const float t=(1-std::cos(i*.15f))*.5f;
        Check(tiny.Tick({-.1f-.03f*t,-.3f,-.45f+.025f*t},{.1f+.03f*t,-.3f,-.45f+.025f*t}).forward==0,
              "small preparatory movements triggered early propulsion");
    }
}
void StrokeMatrix() {
    int cases=0;
    for(int hz:{30,45,90,120})for(float duration:{.28f,.38f,1.1f,1.6f})for(float pitch:{-.9f,0.f,.9f})for(unsigned seed:{1u,7u,91u}) {
        Rig r;TimedStroke(r,duration,hz,pitch,.002f,seed);
        Check(r.strokes==1,"pose/rate/jitter matrix missed or duplicated a stroke");
        Check(r.firstForward<r.firstStroke,"pose/rate/jitter matrix lost early movement");
        if(duration<=.28f)Check(r.boosted,"fast noisy stroke lost its boost");
        if(duration>=1.1f)Check(!r.boosted,"slow noisy stroke incorrectly accelerated");
        ++cases;
    }
    std::cout<<cases<<" rate/speed/pitch/jitter trajectories\n";
}
void FastExit() {
    // Live CanEnterFastSwimming uses 0.80, not the redscript fallback 0.90.
    // The old 0.85 cap passed a constant-only unit assertion but stayed fast.
    for(float threshold:{.8f,.9f})
        Check(ForwardAction(1,false,1)<=threshold,"normal intent cannot clear native latched sprint");
    Check(ForwardAction(1,false,1)>.5f,"fast exit needlessly stops propulsion");
    Check(ForwardAction(1,false,0)==1,"normal swim remains unnecessarily below full input");
    Check(ForwardAction(1,true,1)==1,"strong stroke loses fast input");
    Check(ForwardAction(1,false,1,true)==1,"gesture overrides physical sprint");
    Check(ForwardAction(0,false,1)==0 && ForwardAction(.4f,false,1)==.4f,"fast exit manufactures forward input");
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;const std::string test=argv[1];
    if(test=="stroke")Stroke();else if(test=="rejection")Rejection();else if(test=="head_motion")HeadMotion();
    else if(test=="tracking_loss")TrackingLoss();else if(test=="water_gate")WaterGate();else if(test=="sprint_toggle")SprintToggle();
    else if(test=="input_packets")InputPackets();else if(test=="water_posture")WaterPosture();
    else if(test=="pitched_stroke")PitchedStroke();else if(test=="ascent")Ascent();
    else if(test=="direction")Direction();else if(test=="water_feedback")WaterFeedback();
    else if(test=="early_response")EarlyResponse();else if(test=="speed_modes")SpeedModes();
    else if(test=="partial_stop")PartialStop();else if(test=="speed_transitions")SpeedTransitions();
    else if(test=="early_rejection")EarlyRejection();else if(test=="stroke_matrix")StrokeMatrix();
    else if(test=="fast_exit")FastExit();else return 2;
    std::cout<<"PASS "<<test<<'\n';
}
