#include "Natives/NativeFunctions.hpp"
#include "Anim/WheelGrab.hpp"
#include "Runtimes/Haptics.hpp"
#include "Runtimes/EngineHaptics.hpp"
#include "Utils/DebugGate.hpp"
#include "Core/VrCoreShared.hpp"
#include <RED4ext/Scripting/Utils.hpp>

// SetVRVehicleEngine(rpm, rpmMax): the engine's revs as the vehicle blackboard has them, once a frame from
// mods/cet/CyberpunkVRPort_Driving while the wheel grab is active. Felt in a hand on the wheel (Runtimes/Haptics.cpp).
void SetVRVehicleEngine(RED4ext::IScriptable*,RED4ext::CStackFrame* frame,void*,int64_t) {
    float rpm{},rpmMax{};
    RED4ext::GetParameter(frame,&rpm);RED4ext::GetParameter(frame,&rpmMax);frame->code++;
    cvr::haptics::SetVehicleEngine(rpm,rpmMax);
    // What the rev counter gives at idle and at the limit, to tune the beat curve (F10 > Diagnostics, once a second).
    if(cvr::RuntimeDiagnosticsEnabled()) {
        static uint64_t last=0;const uint64_t now=GetTickCount64();
        if(now-last>=1000) {
            last=now;
            const auto beat=cvr::haptics::EngineBeatFor(cvr::haptics::EngineLoad(rpm,rpmMax));
            Log("[engine-haptics] rpm=%.0f max=%.0f load=%.2f -> beat amp=%.2f rate=%.1f/s strength=%.2f\n",
                rpm,rpmMax,cvr::haptics::EngineLoad(rpm,rpmMax),beat.amplitude,1.0f/beat.periodSec,cvr::haptics::VehicleGain());
        }
    }
}

void GetVRWheelControlState(RED4ext::IScriptable*,RED4ext::CStackFrame* frame,int32_t* out,int64_t) {
    frame->code++;
    if(out)*out=cvr::anim::WheelControlState();
}
void SetVRWheelGamepadProfile(RED4ext::IScriptable*,RED4ext::CStackFrame* frame,bool* out,int64_t) {
    float inner{},outer{};
    RED4ext::GetParameter(frame,&inner);RED4ext::GetParameter(frame,&outer);frame->code++;
    const bool accepted=cvr::anim::WheelSetGamepadProfile(inner,outer);
    if(out)*out=accepted;
}
