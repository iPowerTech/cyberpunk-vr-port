#include "Anim/LeftHanded.hpp"
#include "Runtimes/Haptics.hpp"
#include "Runtimes/EngineHaptics.hpp"
#include <limits>
#include "Runtimes/Psvr2HapticChannel.hpp"
#include "Runtimes/HapticRouting.hpp"
#include "Runtimes/OpenXRManager.hpp"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
extern "C" {
int CyberpunkVR_TwoHandActive = 0;
float CyberpunkVR_WeaponKickDeg = 1;
extern int CyberpunkVR_Haptics;
extern float CyberpunkVR_HapticScale;
extern float CyberpunkVR_HapticSupportMul;
}
std::atomic<bool> g_isDriving{false};
namespace cvr::anim {
std::atomic<float> g_wheelBlendRight{0};
std::atomic<float> g_wheelBlendLeft{0};
}
struct Output { int hand; float amp; float seconds; };
std::vector<Output> output;
int OpenXRManager::ApplyHaptic(int hand, float amp, float sec) { output.push_back({hand,amp,sec}); return 0; }
int checks = 0;
void check(bool condition, const char* why) { ++checks; if (!condition) { std::cerr << why << '\n'; std::exit(1); } }
int main() {
    using namespace cvr::haptics;
    HapticRoute().BeginSession(false, false);
    // ENGINE HAPTICS (fork, Runtimes/EngineHaptics.hpp): off by default, only in a hand on the wheel, beats that
    // follow the revs. The upstream audio rumble these checks replace is not in the fork.
    check(VehicleGain()==0,"engine haptics are off by default");
    g_isDriving=true; cvr::anim::g_wheelBlendLeft=1; cvr::anim::g_wheelBlendRight=1;
    SetVehicleEngine(900,7100); Tick();
    check(output.empty(),"engine off by default: nothing even with both hands on the wheel");
    g_isDriving=false; cvr::anim::g_wheelBlendLeft=0; cvr::anim::g_wheelBlendRight=0; Clear();
    SetLegacyGain(1.25f); SetVehicleGain(1);
    Pulse(1,.45f,45); Pulse(1,1,90); Tick();
    check(output.size()==1 && output[0].hand==1 && output[0].amp==1 && output[0].seconds>.08f,"Quest swing merged and dispatched");
    g_isDriving=true; cvr::anim::g_wheelBlendLeft=1; cvr::anim::g_wheelBlendRight=1;
    SetVehicleEngine(900,7100); output.clear(); Tick();
    check(output.size()==1 && output[0].hand==0,"engine does not overwrite an active right swing");
    cvr::anim::g_wheelBlendLeft=0; cvr::anim::g_wheelBlendRight=0;
    Clear(); output.clear(); Shot(2,true); Tick();
    check(output.size()==2,"two-hand actual shot uses both hands");
    // The fork keeps the per-class shot profiles: the support hand plays the same profile times CyberpunkVR_HapticSupportMul.
    check(std::abs(output[0].amp/output[1].amp-CyberpunkVR_HapticSupportMul)<.001f,"support hand follows CyberpunkVR_HapticSupportMul");
    Clear(); output.clear(); Play(kMeleeParry,kRight); Pulse(1,.5f,45); Tick();
    check(output.size()==1 && output[0].amp==1 && output[0].seconds<.021f,"upstream parry outranks swing");
    Clear(); output.clear(); cvr::anim::g_wheelBlendLeft=1; SetVehicleEngine(900,7100); Tick();
    check(output.size()==1 && output[0].hand==0,"engine only in the hand on the wheel");
    const float idleAmp=output[0].amp;
    output.clear(); Tick(); check(output.empty(),"idle beats are spaced, not a continuous buzz");
    {
        const float nan=std::numeric_limits<float>::quiet_NaN();
        check(EngineLoad(nan,nan)==0 && EngineLoad(-5,7100)==0 && EngineLoad(9000,7100)==1 && EngineLoad(3550,0)==.5f,
              "revs outside the counter's range are bounded");
        const auto idle=EngineBeatFor(EngineLoad(900,7100)), full=EngineBeatFor(EngineLoad(7000,7100));
        check(full.amplitude>idle.amplitude*2.5f && full.periodSec<idle.periodSec/2.5f,"on the throttle the beats are denser and stronger");
        check(idle.pulseSec>=.014f && full.pulseSec>=.014f && full.pulseSec<full.periodSec,"every beat is playable and separate");
        check(EngineBeatFor(nan).amplitude==EngineBeatFor(0).amplitude,"an invalid load is idle");
    }
    {
        // the revs climb: after the ~80 ms follow the beats come faster and harder than at idle
        int beats=0; float last=0;
        for(int i=0;i<30;++i){SetVehicleEngine(7000,7100); Sleep(10); output.clear(); Tick();
            if(!output.empty()){++beats; last=output[0].amp;}}
        check(beats>=5 && last>idleAmp*2,"the engine follows the revs");
    }
    Clear(); output.clear(); SetVehicleEngine(900,7100); g_isDriving=false; Tick();
    check(output.empty(),"engine stops on vehicle exit even with fresh revs");
    g_isDriving=true; SetVehicleEngine(900,7100); Sleep(110); Tick(); check(output.empty(),"stale revs expire");
    Pulse(1,1,90); Sleep(110); Tick(); check(output.empty(),"stale swing expires");
    Clear(); Pulse(1,.8f,90); CyberpunkVR_Haptics=0; Tick();
    check(output.empty(),"master mute clears queued swing");
    CyberpunkVR_Haptics=1; Tick(); check(output.empty(),"unmute never replays old swing");
    SetVehicleEngine(900,7100); SetVehicleGain(0); Tick();
    check(output.empty(),"engine strength zero switches it off");
    Clear(); HapticRoute().Request(2); HapticRoute().BeginSession(true,true);
    auto& roadChannel=cvr::psvr2::Haptics();roadChannel.events.clear();
    SetVehicleGain(1); cvr::anim::g_wheelBlendLeft=1; cvr::anim::g_wheelBlendRight=1;
    SetVehicleEngine(3000,7100); output.clear(); Tick();
    check(output.empty(),"Toolkit engine never calls OpenXR alongside the bridge");
    check(roadChannel.events.size()==2 && roadChannel.events[0].hand==0 && roadChannel.events[1].hand==1,
          "Toolkit engine reaches both hands on the wheel");
    roadChannel.events.clear(); Clear(); cvr::anim::g_wheelBlendRight=0; SetVehicleEngine(3000,7100); Tick();
    check(roadChannel.events.size()==1 && roadChannel.events[0].hand==0,"Toolkit engine is per hand on the wheel");
    roadChannel.events.clear(); Clear(); SetVehicleEngine(3000,7100); g_isDriving=false; Tick();
    check(roadChannel.events.empty(),"Toolkit engine stops immediately on vehicle exit");
    g_isDriving=true; SetVehicleGain(0); Tick();
    check(roadChannel.events.empty(),"Toolkit engine switch mutes it");
    SetVehicleGain(1); roadChannel.events.clear(); Clear(); cvr::anim::g_wheelBlendRight=1;
    SetVehicleEngine(3000,7100); Play(kMeleeParry,kRight); Tick();
    check(roadChannel.events.size()==2 && roadChannel.events[1].duration<25,"parry remains above the engine on Toolkit");
    g_isDriving=false; cvr::anim::g_wheelBlendLeft=0; cvr::anim::g_wheelBlendRight=0;
    HapticRoute().BeginSession(false,false);
    Clear(); output.clear(); Play(kMagSeat,3); Tick();
    check(output.size()==2 && output[0].amp==output[1].amp && output[0].seconds<.03f,"magazine seated: a short tap in both hands");
    Clear(); output.clear(); Play(kChamber,2); Tick();
    check(output.size()==1 && output[0].hand==1,"round chambered by the thumb: only the gun hand");
    roadChannel.events.clear(); output.clear();
    HapticRoute().BeginSession(true,false); Clear(); Pulse(0,.6f,85); Tick();
    check(output.size()==1 && output[0].hand==0,"PSVR2 without bridge receives generic swing");
    Clear(); output.clear(); Play(kBeltTouch,kLeft); Clear(); Tick();
    check(output.empty(),"focus/session clear drops all envelope events");
    Clear(); output.clear(); Shot(1,false,0,4.0f,true); Tick();
    check(output.size()==1 && output[0].hand==0,"dual native shot is only in firing left hand");
    Clear(); output.clear(); Shot(1,true,1,4.0f,true); Tick();
    check(output.size()==1 && output[0].hand==1,"dual suppresses support-hand vibration even if stale two-hand flag is set");
    Clear(); output.clear(); Play(kUiClick,kLeft); Tick();
    check(output.size()==1 && output[0].hand==0,"0.1.9 VR-menu interaction haptic retained");
    Clear(); output.clear(); HapticRoute().BeginSession(true,true);
    auto& channel=cvr::psvr2::Haptics();channel.events.clear();
    Shot(1,false,0,1.0f,true);Shot(1,false,1,4.0f,true);Tick();
    check(output.empty() && channel.events.size()==2,"dual Toolkit shots never call native output");
    check(channel.events[0].hand==0 && channel.events[0].amplitude==.85f && channel.events[0].duration==75,"left Toolkit dual impact restored");
    check(channel.events[1].hand==1 && channel.events[1].amplitude==1 && channel.events[1].duration==125,"heavy right Toolkit dual impact restored");
    Shot(2,true,1,1.0f,false);Tick();
    check(output.empty() && channel.events.size()==2,"single Toolkit gun remains owned by EDS");
    Clear(); output.clear(); HapticRoute().BeginSession(false,false);
    cvr::anim::leftHanded=true; CyberpunkVR_TwoHandActive=1;
    Play(kMeleeHit,kLeft); Tick();
    check(output.size()==2,"upstream left-handed two-hand hit retains both actuators");
    Clear(); output.clear(); CyberpunkVR_TwoHandActive=0;
    Play(kMeleeSwing,kLeft); Tick();
    check(output.size()==1 && output[0].hand==0,"upstream swing event survives RC5 integration");
    std::cout << checks << " production haptic playback checks passed\n";
}
